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

#pragma once
#include <stddef.h>
#include <atomic>
#include <string.h>
#include <memory>
#include <malloc.h>


/* This is a multiple producers multiple consumers lock-free queue.
 * No strong order guarantee but in practice it is ordered in most cases.
 * It is almost wait-free, but there is a very little chance for live-lock. */


namespace memorylog {


template <typename PTR_TYPE>
struct Deleter {
    static inline void delete_pointer(PTR_TYPE elem) {
        delete elem;
    }
};

template <>
struct Deleter<void*> {
    static inline void delete_pointer(void* elem) {
        free(elem);
    }
};


template <typename PTR_TYPE = void*, bool DELETE_ELEMS = true>
class RingPtrQueue {
public:
    RingPtrQueue(size_t size)
        : Size(size)
        , ElemSemaphore(size)
        , Buffer(new std::atomic<PTR_TYPE>[size])
    {
        memset(&Buffer[0], 0, sizeof(PTR_TYPE) * size);
    }


    ~RingPtrQueue() {
        if (DELETE_ELEMS)
            for (;;) {
                PTR_TYPE elem = dequeue();
                if (elem == nullptr)
                    break;
                Deleter<PTR_TYPE>::delete_pointer(elem);
            }
    }


    bool enqueue(PTR_TYPE elem) {
        if (!book_space())
            return false;

        /* Ok, there is a space for a new element, let's find it */
        for (;;) {
            size_t slot = Tail.fetch_add(1, std::memory_order_seq_cst);
            slot %= Size;

            PTR_TYPE expect = nullptr;
            bool put = Buffer[slot].compare_exchange_strong(
                expect, elem, std::memory_order_seq_cst);

            if (put) {
                ElemSemaphore.fetch_sub(1, std::memory_order_seq_cst);
                return true;
            }
        }
    }


    PTR_TYPE dequeue() {
        if (!book_elem())
            return nullptr;

        /* Ok, there is at least one element in the buffer, let's find it */
        for (;;) {
            size_t slot = Head.fetch_add(1, std::memory_order_seq_cst);
            slot %= Size;

            PTR_TYPE elem =
                Buffer[slot].exchange(nullptr, std::memory_order_seq_cst);

            if (elem != nullptr) {
                SpaceSemaphore.fetch_sub(1, std::memory_order_seq_cst);
                return elem;
            }
        }
    }


private:
    bool book_space() {
        size_t let_me_pass =
            SpaceSemaphore.fetch_add(1, std::memory_order_seq_cst) + 1;
        if (let_me_pass <= Size)
            return true;
        SpaceSemaphore.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }


    bool book_elem() {
        size_t let_me_pass =
            ElemSemaphore.fetch_add(1, std::memory_order_seq_cst) + 1;
        if (let_me_pass <= Size)
            return true;
        ElemSemaphore.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }


    size_t const Size;
    std::atomic<size_t> SpaceSemaphore = {0};
    std::atomic<size_t> ElemSemaphore;
    std::atomic<size_t> Head = {0};
    std::atomic<size_t> Tail = {0};
    std::unique_ptr<std::atomic<PTR_TYPE>[]> const Buffer;
};


} // namespace memorylog
