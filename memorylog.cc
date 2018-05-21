/*
Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2018 Vitaliy Manushkin.

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "memorylog.hh"
#include <new>
#include "mt_ring_queue.hh"
#include <memory>
#include <stdarg.h>
#include <stdio.h>


namespace memorylog {


constexpr size_t PAGE_SIZE = 4096;
constexpr size_t RECORD_PREFIX_SIZE = 16;
constexpr size_t RECORD_ALIGNMENT = 16;

/* This is a magic string at the begining of each record */
static const char RECORD_PREFIX[RECORD_PREFIX_SIZE] = {
   '\n', 'i', 'P', 'a', 'o', '2', 'i', 'j',
    'S', 'a', 'h', 'b', 'e', '0', 'F', ' ',
};


template <uintptr_t ALIGNMENT, typename PTR_TYPE>
static PTR_TYPE ptr_align_up(PTR_TYPE value) {
    constexpr uintptr_t MASK = ALIGNMENT - 1;
    static_assert((ALIGNMENT & MASK) == 0, "invalid alignment");
    uintptr_t cvalue = reinterpret_cast<uintptr_t>(value);
    return reinterpret_cast<PTR_TYPE>((cvalue + MASK) & ~MASK);
}


class MemoryBufferChunk {
public:
    void reset() {
        fill_point = ptr_align_up<RECORD_ALIGNMENT>(
            reinterpret_cast<char*>(this) + sizeof(*this));
    }

    bool out_of_space(size_t chunk_size, size_t record_len) const {
        size_t space_left =
            chunk_size + reinterpret_cast<const char*>(this) - fill_point;
        return record_len + RECORD_PREFIX_SIZE > space_left;
    }

    size_t available_space(size_t chunk_size) const {
        return chunk_size + reinterpret_cast<const char*>(this) - fill_point;
    }

    bool empty() const {
        auto start_point = ptr_align_up<RECORD_ALIGNMENT>(
            reinterpret_cast<const char*>(this) + sizeof(*this));
        return fill_point == start_point;
    }

    void fill_up_to(char* new_fill_point) {
        fill_point = ptr_align_up<RECORD_ALIGNMENT>(new_fill_point);
    }

    char* get_fill_point() const {
        return fill_point;
    }

private:
    char* fill_point;
};


struct GlobalContext {
    std::unique_ptr<char[]> const BigBuffer;
    size_t const ChunkSize;
    size_t const TotalSize;
    RingPtrQueue<MemoryBufferChunk*, false> Queue;

    GlobalContext(size_t total_buffer_size, size_t chunk_size);
};


class TLSChunkHolder {
public:
    ~TLSChunkHolder();
    inline MemoryBufferChunk* reset(GlobalContext* ctx);
    inline MemoryBufferChunk* get(GlobalContext* ctx);

private:
    MemoryBufferChunk* Chunk = nullptr;
};



/* Global memory log context */
thread_local TLSChunkHolder CurrentChunk;
std::atomic<GlobalContext*> GlobalCtx(nullptr);


TLSChunkHolder::~TLSChunkHolder() {
    auto ctx = GlobalCtx.load(std::memory_order_relaxed);

    if (ctx != nullptr && Chunk != nullptr)
        ctx->Queue.enqueue(Chunk);
}


MemoryBufferChunk* TLSChunkHolder::reset(GlobalContext* ctx) {
    if (Chunk != nullptr)
        ctx->Queue.enqueue(Chunk);

    Chunk = ctx->Queue.dequeue();
    if (Chunk == nullptr)
        return nullptr;
    Chunk->reset();
    return Chunk;
}


MemoryBufferChunk* TLSChunkHolder::get(GlobalContext* ctx) {
    if (Chunk == nullptr) {
        Chunk = ctx->Queue.dequeue();
        if (Chunk == nullptr)
            return nullptr;
        Chunk->reset();
    }
    return Chunk;
}


GlobalContext::GlobalContext(size_t total_buffer_size, size_t chunk_size)
    : BigBuffer(new char[total_buffer_size])
    , ChunkSize(chunk_size)
    , TotalSize(total_buffer_size)
    , Queue(total_buffer_size / chunk_size)
{
    /* let's pre-allocate all memory pages */
    for (size_t i = 0; i < total_buffer_size; i += PAGE_SIZE)
        BigBuffer[i] = 0;

    /* put all chunks into the queue */
    for (size_t i = 0; i < total_buffer_size / chunk_size; ++i)
        Queue.enqueue(
            reinterpret_cast<MemoryBufferChunk*>(
                BigBuffer.get() + chunk_size * i));
}


bool initialize(size_t total_buffer_size, size_t chunk_size) {
    if (chunk_size <= RECORD_PREFIX_SIZE + 2)
        return false;

    if (total_buffer_size < chunk_size)
        return false;

    if (total_buffer_size % chunk_size != 0)
        return false;

    GlobalContext* new_ctx;
    try {
        new_ctx = new GlobalContext(total_buffer_size, chunk_size);
    } catch (...) {
        return false;
    }

    GlobalContext* old_ctx = nullptr;
    if (!GlobalCtx.compare_exchange_weak(old_ctx, new_ctx)) {
        delete new_ctx;
        return false;
    }

    return true;
}


void finalize() {
    auto old_ctx = GlobalCtx.exchange(nullptr, std::memory_order_release);
    delete old_ctx;
    CurrentChunk = TLSChunkHolder();
}


struct CallContext {
    GlobalContext* GCtx;
    MemoryBufferChunk* Chunk;
    char* PrefixPlace;
    char* RecordPlace;

    bool init(size_t record_size) {
        GCtx = GlobalCtx.load(std::memory_order_relaxed);

        if (GCtx == nullptr)
            return false;

        if (record_size > GCtx->ChunkSize - RECORD_PREFIX_SIZE)
            return false;

        Chunk = CurrentChunk.get(GCtx);
        if (Chunk == nullptr)
            return false;
        if (Chunk->out_of_space(GCtx->ChunkSize, record_size)) {
            Chunk = CurrentChunk.reset(GCtx);
            if (Chunk == nullptr)
                return false;
            if (Chunk->out_of_space(GCtx->ChunkSize, record_size))
                return false;
        }

        PrefixPlace = Chunk->get_fill_point();
        RecordPlace = PrefixPlace + RECORD_PREFIX_SIZE;

        memset(PrefixPlace, 0, RECORD_PREFIX_SIZE);
        return true;
    }

    bool reset_chunk(size_t record_size) {
        Chunk = CurrentChunk.reset(GCtx);
        if (Chunk == nullptr)
            return false;
        if (Chunk->out_of_space(GCtx->ChunkSize, record_size))
            return false;

        PrefixPlace = Chunk->get_fill_point();
        RecordPlace = PrefixPlace + RECORD_PREFIX_SIZE;

        memset(PrefixPlace, 0, RECORD_PREFIX_SIZE);
        return true;
    }

    void write_prefix() {
        // ensure a compiler does not reorder operations
        std::atomic_signal_fence(std::memory_order_seq_cst);
        memcpy(PrefixPlace, RECORD_PREFIX, RECORD_PREFIX_SIZE);
    }
};


bool write(const char* buf, size_t len) {
    CallContext ctx;
    if (!ctx.init(len))
        return false;
    memcpy(ctx.RecordPlace, buf, len);
    ctx.write_prefix();
    ctx.Chunk->fill_up_to(ctx.RecordPlace + len);
    return true;
}


bool format_write(const char* format, ...) {
    CallContext ctx;
    if (!ctx.init(2))
        return false;

  again:
    size_t space_available_for_record =
        ctx.Chunk->available_space(ctx.GCtx->ChunkSize) - RECORD_PREFIX_SIZE;

    va_list args;
    va_start(args, format);
    int bytes_written =
        vsnprintf(ctx.RecordPlace, space_available_for_record, format, args);
    if (bytes_written < 0)
        return false;
    if ((size_t)bytes_written > space_available_for_record) {
        if (!ctx.reset_chunk(bytes_written))
            return false;
        goto again;
    }

    ctx.write_prefix();
    ctx.Chunk->fill_up_to(ctx.RecordPlace + bytes_written);

    return true;
}


bool dump(const char* filename) {
    auto ctx = GlobalCtx.load(std::memory_order_relaxed);

    if (ctx == nullptr)
        return false;

    FILE* dumpfile = fopen(filename, "wa");
    if (dumpfile == nullptr)
        return false;

    size_t write_result =
        fwrite(ctx->BigBuffer.get(), ctx->TotalSize, 1, dumpfile);
    fclose(dumpfile);

    return write_result == 1;
}


}
