#include <common/io/peak_rss.hpp>

#include <cstdlib>
#include <print>
#include <vector>

constexpr static size_t kMibBytes = size_t{1} << 20;
constexpr static size_t kPageStride = 4096;
constexpr static size_t kDefaultMib = 256;

static void Escape(void* p) {
	asm volatile("" : : "g"(p) : "memory");
}

int main(int argc, char** argv) {
	const size_t mib = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : kDefaultMib;
	std::vector<char> hog(mib * kMibBytes);
	for (size_t i = 0; i < hog.size(); i += kPageStride) {
		hog[i] = 1;
	}
	Escape(hog.data());
	std::println("allocated_mib={} peak_rss_kib={}", mib, lr::common::PeakRss::Kib());
	return 0;
}
