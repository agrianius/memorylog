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
#include <atomic>


class SyncStart {
public:
    SyncStart(size_t number_of_agents)
        : NumberOfAgents(number_of_agents)
    {}

    void WaitForGreenLight() {
        ++NumberOfWaitingAgents;
        while (!GreenLight)
            ;
    }

    void Start() {
        for (;;) {
            auto ready = NumberOfWaitingAgents.load(std::memory_order_acquire);
            if (ready == NumberOfAgents)
                break;
        }
        GreenLight.store(true, std::memory_order_release);
    }

protected:
    size_t NumberOfAgents;
    std::atomic<size_t> NumberOfWaitingAgents = {0};
    std::atomic<bool> GreenLight = {false};
};
