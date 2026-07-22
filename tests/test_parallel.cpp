#include "cloakframe/OrderedParallel.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
    void testConsumesInOrder()
    {
        const std::size_t count = 200;
        std::atomic<bool> cancelled{false};
        std::vector<std::size_t> consumed;

        cloakframe::processOrdered<std::size_t>(
            count, 8, 8, cancelled,
            [](std::size_t index)
            {
                std::this_thread::sleep_for(std::chrono::microseconds((index * 7) % 300));
                return index * 2;
            },
            [&](std::size_t index, std::size_t &&value)
            {
                assert(value == index * 2);
                consumed.push_back(index);
            });

        assert(consumed.size() == count);
        for (std::size_t i = 0; i < count; ++i)
        {
            assert(consumed[i] == i);
        }
    }

    void testRespectsInFlightBound()
    {
        const std::size_t count = 100;
        const std::size_t maxInFlight = 3;
        std::atomic<bool> cancelled{false};
        std::atomic<int> active{0};
        std::atomic<int> peakActive{0};
        std::size_t consumedCount = 0;

        cloakframe::processOrdered<int>(
            count, 8, maxInFlight, cancelled,
            [&](std::size_t index)
            {
                const int now = ++active;
                int peak = peakActive.load();
                while (now > peak && !peakActive.compare_exchange_weak(peak, now))
                {
                }
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                --active;
                return static_cast<int>(index);
            },
            [&](std::size_t, int &&)
            {
                ++consumedCount;
            });

        assert(consumedCount == count);
        assert(peakActive.load() <= static_cast<int>(maxInFlight));
    }

    void testStopsAfterCancellation()
    {
        const std::size_t count = 10000;
        std::atomic<bool> cancelled{false};
        std::size_t consumedCount = 0;

        cloakframe::processOrdered<int>(
            count, 4, 4, cancelled,
            [](std::size_t index)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                return static_cast<int>(index);
            },
            [&](std::size_t, int &&)
            {
                ++consumedCount;
                if (consumedCount == 20)
                {
                    cancelled.store(true, std::memory_order_release);
                }
            });

        assert(consumedCount >= 20);
        assert(consumedCount < count);
    }

    void testSingleThreadAndEmpty()
    {
        std::atomic<bool> cancelled{false};
        std::size_t consumedCount = 0;

        cloakframe::processOrdered<int>(
            0, 4, 4, cancelled,
            [](std::size_t) { return 0; },
            [&](std::size_t, int &&) { ++consumedCount; });
        assert(consumedCount == 0);

        cloakframe::processOrdered<int>(
            5, 1, 1, cancelled,
            [](std::size_t index) { return static_cast<int>(index); },
            [&](std::size_t index, int &&value)
            {
                assert(static_cast<std::size_t>(value) == index);
                ++consumedCount;
            });
        assert(consumedCount == 5);
    }
}

int main()
{
    testConsumesInOrder();
    std::puts("ordered consumption: ok");
    testRespectsInFlightBound();
    std::puts("in-flight bound: ok");
    testStopsAfterCancellation();
    std::puts("cancellation: ok");
    testSingleThreadAndEmpty();
    std::puts("edge cases: ok");
    std::puts("all parallel tests passed");
    return 0;
}
