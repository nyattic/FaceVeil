#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace cloakframe
{
    template <typename Result, typename Produce, typename Consume>
    void processOrdered(std::size_t itemCount,
                        unsigned threadCount,
                        std::size_t maxInFlight,
                        const std::atomic<bool> &cancelled,
                        Produce produce,
                        Consume consume)
    {
        if (itemCount == 0)
        {
            return;
        }
        threadCount = std::max(1U, threadCount);
        maxInFlight = std::max<std::size_t>(1, maxInFlight);

        std::mutex mutex;
        std::condition_variable produceCv;
        std::condition_variable consumeCv;
        std::size_t nextIndex = 0;
        std::size_t consumedCount = 0;
        std::map<std::size_t, Result> ready;
        bool stopped = false;

        auto worker = [&]
        {
            for (;;)
            {
                std::size_t index = 0;
                {
                    std::unique_lock lock(mutex);
                    produceCv.wait(lock, [&]
                    {
                        return stopped || nextIndex >= itemCount ||
                               nextIndex < consumedCount + maxInFlight;
                    });
                    if (stopped || nextIndex >= itemCount)
                    {
                        return;
                    }
                    index = nextIndex++;
                }
                if (cancelled.load(std::memory_order_acquire))
                {
                    std::lock_guard lock(mutex);
                    stopped = true;
                    produceCv.notify_all();
                    consumeCv.notify_all();
                    return;
                }
                Result result = produce(index);
                {
                    std::lock_guard lock(mutex);
                    ready.emplace(index, std::move(result));
                    consumeCv.notify_all();
                }
            }
        };

        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        for (unsigned i = 0; i < threadCount; ++i)
        {
            workers.emplace_back(worker);
        }

        {
            std::unique_lock lock(mutex);
            while (consumedCount < itemCount)
            {
                consumeCv.wait(lock, [&]
                {
                    return stopped || ready.contains(consumedCount);
                });
                const auto it = ready.find(consumedCount);
                if (it == ready.end())
                {
                    break;
                }
                auto node = ready.extract(it);
                lock.unlock();
                consume(consumedCount, std::move(node.mapped()));
                lock.lock();
                ++consumedCount;
                produceCv.notify_all();
            }
            stopped = true;
            produceCv.notify_all();
        }

        for (auto &thread: workers)
        {
            thread.join();
        }
    }
}
