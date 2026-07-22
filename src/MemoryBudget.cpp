#include "cloakframe/MemoryBudget.hpp"

#include <algorithm>
#include <limits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#else
#include <unistd.h>
#endif

namespace cloakframe
{
    std::uint64_t physicalMemoryBytes() noexcept
    {
        static const std::uint64_t total = []
        {
#if defined(_WIN32)
            MEMORYSTATUSEX status{};
            status.dwLength = sizeof(status);
            return ::GlobalMemoryStatusEx(&status)
                       ? static_cast<std::uint64_t>(status.ullTotalPhys)
                       : 0ULL;
#elif defined(__APPLE__)
            std::uint64_t bytes = 0;
            std::size_t size = sizeof(bytes);
            return ::sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0
                       ? bytes
                       : 0ULL;
#else
            const long pages = ::sysconf(_SC_PHYS_PAGES);
            const long pageSize = ::sysconf(_SC_PAGE_SIZE);
            if (pages <= 0 || pageSize <= 0)
            {
                return 0ULL;
            }
            const auto unsignedPages = static_cast<std::uint64_t>(pages);
            const auto unsignedPageSize = static_cast<std::uint64_t>(pageSize);
            return unsignedPages > std::numeric_limits<std::uint64_t>::max() /
                                   unsignedPageSize
                       ? 0ULL
                       : unsignedPages * unsignedPageSize;
#endif
        }();
        return total;
    }

    std::uint64_t adaptiveMemoryBudget(const std::uint64_t minimum,
                                       const std::uint64_t maximum,
                                       const std::uint64_t divisor) noexcept
    {
        if (minimum >= maximum || divisor == 0)
        {
            return minimum;
        }
        const std::uint64_t total = physicalMemoryBytes();
        if (total == 0)
        {
            return minimum;
        }
        return std::clamp(total / divisor, minimum, maximum);
    }
}
