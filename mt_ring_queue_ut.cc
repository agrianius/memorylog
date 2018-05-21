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

#include "mt_ring_queue.hh"
#include <thread>
#include <atomic>

#include "ut_helpers.hh"
//#include <iostream>


using memorylog::RingPtrQueue;


TEST_GROUP(MT_RING_QUEUE) {};


TEST(MT_RING_QUEUE, ENQUEUE_DEQUEUE_ONE_ELEM) {
    RingPtrQueue<void*, false> queue(1);
    queue.enqueue((void*)1);
    auto elem = queue.dequeue();
    CHECK(elem == (void*)1);

    elem = queue.dequeue();
    CHECK(elem == nullptr);
}


TEST(MT_RING_QUEUE, ENQUEUE_DEQUEUE_10_ELEMS) {
    RingPtrQueue<void*, false> queue(10);

    for (uintptr_t i = 1; i <= 10; ++i)
        queue.enqueue((void*)i);

    for (uintptr_t i = 1; i <= 10; ++i) {
        auto elem = queue.dequeue();
        CHECK((void*)i == elem);
    }

    auto elem = queue.dequeue();
    CHECK(elem == nullptr);
}


TEST(MT_RING_QUEUE, ENQUEUE_DEQUEUE_1000000_ELEMS_IN_2_THEADS) {
    RingPtrQueue<void*, false> queue(1000000);

    auto enqueue_lambda = [&]() {
        for (uintptr_t i = 1; i <= 1000000; ++i)
            queue.enqueue((void*)i);
    };

    auto dequeue_lambda = [&]() {
        for (uintptr_t i = 1; i <= 1000000; ++i) {
            for (;;) {
                auto elem = queue.dequeue();
                if (elem == nullptr)
                    continue;
                CHECK((void*)i == elem);
                break;
            }
        }
    };

    /* dequeue thread is staring first intentionally */
    std::thread dequeue_thread(dequeue_lambda);
    std::thread enqueue_thread(enqueue_lambda);

    enqueue_thread.join();
    dequeue_thread.join();

    auto elem = queue.dequeue();
    CHECK(elem == nullptr);
}


TEST(MT_RING_QUEUE, MULTIPLE_PRODUCERS_MULTIPLE_CONSUMERS) {
    RingPtrQueue<void*, false> queue(1000000);
    SyncStart greenlight(10);
    std::atomic<uintptr_t> total_sum(0);
    std::atomic<uint8_t> active_producers;

    auto producer_lambda = [&](uintptr_t start_number = 1) {
        ++active_producers;
        greenlight.WaitForGreenLight();

        for (uintptr_t i = start_number; i < 1000 + start_number; ++i)
            queue.enqueue((void*)i);

        --active_producers;
    };

    auto consumer_lambda = [&]() {
        greenlight.WaitForGreenLight();

        uintptr_t local_sum = 0;
        for (;;) {
            auto elem = queue.dequeue();
            if (elem == nullptr) {
                if (active_producers.load(std::memory_order_acquire) == 0)
                    break;
            } else {
                local_sum += (uintptr_t)elem;
            }
        }
        auto elem = queue.dequeue();
        if (elem != nullptr)
            local_sum += (uintptr_t)elem;

        //std::cout << local_sum << std::endl;
        total_sum.fetch_add(local_sum, std::memory_order_seq_cst);
    };

    std::thread producers[5];
    std::thread consumers[5];

    for (uint8_t i = 0; i < 5; ++i) {
        producers[i] = std::thread(producer_lambda, 1 + 1000 * i);
        consumers[i] = std::thread(consumer_lambda);
    }

    greenlight.Start();

    for (uint8_t i = 0; i < 5; ++i) {
        producers[i].join();
        consumers[i].join();
    }

    CHECK_EQUAL(total_sum.load(std::memory_order_acquire), 12502500);
}
