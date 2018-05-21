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

#include <CppUTest/MemoryLeakDetectorNewMacros.h>
#include <CppUTest/TestHarness.h>

#include "memorylog.hh"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits>
#include <algorithm>
#include <memory>
#include <string.h>
#include <thread>

#include "ut_helpers.hh"


TEST_GROUP(MEMORYLOG_WRITE) {
    void setup() {
        memorylog::initialize(256, 128);
    }

    void teardown() {
        memorylog::finalize();
    }
};

TEST_GROUP(MEMORYLOG_INIT) {
    void teardown() {
        memorylog::finalize();
    }
};

TEST_GROUP(MEMORYLOG_FORMAT_WRITE) {
    void setup() {
        memorylog::initialize(256, 128);
    }

    void teardown() {
        memorylog::finalize();
    }
};

static size_t get_file_length(FILE* file) {
    struct stat file_stat_buf;
    int res = fstat(fileno(file), &file_stat_buf);
    if (res == -1)
        throw "fstat";
    if (sizeof(file_stat_buf.st_size) > sizeof(size_t))
        if (file_stat_buf.st_size > (off_t)std::numeric_limits<size_t>::max())
            throw "file too large";
    return file_stat_buf.st_size;
}


static bool find_string(const char* filename, const char* string) {
    FILE* dumpfile = fopen(filename, "r");
    if (dumpfile == nullptr)
        throw "fopen";
    size_t file_len = get_file_length(dumpfile);
    std::unique_ptr<char[]> membuf(new char[file_len]);
    size_t read_result = fread(membuf.get(), file_len, 1, dumpfile);
    if (read_result != 1)
        throw "fread";

    auto found = std::search(
        membuf.get(), membuf.get() + file_len,
        string, string + strlen(string));
    return found != membuf.get() + file_len;
}


TEST(MEMORYLOG_FORMAT_WRITE, WRITE_MANY) {
    for (uint32_t i = 0; i < 100; ++i)
        CHECK(memorylog::format_write(
            "%s or %s, %u\n", "love me", "leave me", i));

    CHECK(memorylog::dump("log-dump3"));
    CHECK(find_string("log-dump3",
                      "\niPao2ijSahbe0F love me or leave me, 99\n"));
}


TEST(MEMORYLOG_FORMAT_WRITE, WRITE_ONCE) {
    CHECK(memorylog::format_write("%s or %s\n", "love me", "leave me"));
    CHECK(memorylog::dump("log-dump3"));
    CHECK(find_string("log-dump3", "\niPao2ijSahbe0F love me or leave me\n"));
}


TEST(MEMORYLOG_WRITE, WRITE_ONCE) {
    CHECK(memorylog::write("love me or leave me\n", 20));
    CHECK(memorylog::dump("log-dump1"));
    CHECK(find_string("log-dump1", "\niPao2ijSahbe0F love me or leave me\n"));
}


TEST(MEMORYLOG_WRITE, WRITE_2_THREADS) {
    SyncStart greenlight(2);
    
    auto thread_lambda = [&]() {
        greenlight.WaitForGreenLight();
        for (uint16_t i = 0; i < 100; ++i)
            CHECK(memorylog::write("love me or leave me\n", 20));
    };
    
    std::thread thr1(thread_lambda);
    std::thread thr2(thread_lambda);
    greenlight.Start();
    thr1.join();
    thr2.join();

    CHECK(memorylog::dump("log-dump2"));
    CHECK(find_string("log-dump2", "\niPao2ijSahbe0F love me or leave me\n"));
}


TEST(MEMORYLOG_WRITE, MESSAGE_TOO_BIG) {
    char buf[128];
    CHECK(!memorylog::write(buf, 128));
}


TEST(MEMORYLOG_INIT, BUFFER_TOO_SMALL) {
    CHECK(!memorylog::initialize(256, 16));
    CHECK(!memorylog::initialize(32, 16));
}


TEST(MEMORYLOG_INIT, INITIALIZE_TWICE) {
    CHECK(memorylog::initialize(256, 128));
    CHECK(!memorylog::initialize(256, 128));
}


TEST(MEMORYLOG_INIT, INITIALIZATION_PLUS_FINALIZATION) {
    for (uint8_t i = 0; i < 10; ++i) {
        CHECK(memorylog::initialize(256, 128));
        memorylog::finalize();
        memorylog::finalize();
    }
}


TEST(MEMORYLOG_INIT, CALL_AFTER_FINALIZATION) {
    CHECK(memorylog::initialize(256, 128));
    memorylog::finalize();

    for (uint16_t i = 0; i < 100; ++i)
        CHECK(!memorylog::write("love me or leave me\n", 20));
}


TEST(MEMORYLOG_INIT, CALL_BEFORE_INITIALIZATION) {
    for (uint16_t i = 0; i < 100; ++i)
        CHECK(!memorylog::write("love me or leave me\n", 20));
}
