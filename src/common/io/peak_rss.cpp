#include "peak_rss.hpp"

#if defined(__linux__)
#include <fstream>
#include <string>
#include <string_view>
#elif defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace lr::common {

namespace {

constexpr int64_t kUnavailable = -1;

} // namespace

int64_t PeakRss::Kib() {
#if defined(__linux__)
	constexpr std::string_view kProcStatusPath = "/proc/self/status";
	constexpr std::string_view kVmHwmKey = "VmHWM:";
	std::ifstream status(std::string{kProcStatusPath});
	for (std::string line; std::getline(status, line);) {
		if (line.starts_with(kVmHwmKey)) {
			return std::stoll(line.substr(kVmHwmKey.size()));
		}
	}
	return kUnavailable;
#elif defined(__APPLE__)
	constexpr int64_t kBytesPerKibibyte = 1024;
	rusage usage{};
	if (getrusage(RUSAGE_SELF, &usage) != 0) {
		return kUnavailable;
	}
	return usage.ru_maxrss / kBytesPerKibibyte;
#else
	return kUnavailable;
#endif
}

} // namespace lr::common
