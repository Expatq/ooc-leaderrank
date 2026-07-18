#include "meta.hpp"

#include <charconv>
#include <format>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lr::grid {

namespace {

constexpr char kKeyValueSeparator = '=';

uint64_t GetValue(const std::map<std::string, uint64_t, std::less<>>& values,
                  const std::string& path, std::string_view key) {
	const auto found = values.find(key);
	if (found == values.end()) {
		throw std::runtime_error(std::format("{}: missing key {}", path, key));
	}
	return found->second;
}

} // namespace

Meta Meta::Load(const WorkdirRoot& workdir) {
	const std::string path = workdir.MetaFile().string();
	std::ifstream input(path);
	if (!input) {
		throw std::runtime_error(std::format("cannot open {}", path));
	}
	std::map<std::string, uint64_t, std::less<>> values;
	for (std::string line; std::getline(input, line);) {
		if (line.empty()) {
			continue;
		}
		const size_t cut = line.find(kKeyValueSeparator);
		if (cut == std::string::npos) {
			throw std::runtime_error(std::format("{}: line without '=': {}", path, line));
		}
		uint64_t value = 0;
		const char* begin = line.data() + cut + 1;
		const char* end = line.data() + line.size();
		const auto [parsedEnd, errorCode] = std::from_chars(begin, end, value);
		if (errorCode != std::errc{} || parsedEnd != end) {
			throw std::runtime_error(std::format("{}: not a number in line: {}", path, line));
		}
		values.emplace(line.substr(0, cut), value);
	}
	Meta meta;
	meta.transpose = static_cast<uint32_t>(GetValue(values, path, "transpose"));
	meta.threadsPlanned = static_cast<uint32_t>(GetValue(values, path, "threads_planned"));
	meta.maxId = static_cast<uint32_t>(GetValue(values, path, "max_id"));
	meta.intervalSize = static_cast<uint32_t>(GetValue(values, path, "interval_size"));
	meta.partitions = static_cast<uint32_t>(GetValue(values, path, "partitions"));
	meta.vertices = GetValue(values, path, "vertices");
	meta.edgesRaw = GetValue(values, path, "edges_raw");
	meta.edges = GetValue(values, path, "edges");
	meta.droppedSelfLoops = GetValue(values, path, "dropped_self_loops");
	meta.droppedDuplicates = GetValue(values, path, "dropped_duplicates");
	meta.maxOutDegree = static_cast<uint32_t>(GetValue(values, path, "max_out_degree"));
	meta.maxInDegree = static_cast<uint32_t>(GetValue(values, path, "max_in_degree"));
	return meta;
}

void Meta::Save(const WorkdirRoot& workdir) const {
	const std::string path = workdir.MetaFile().string();
	std::ofstream output(path, std::ios::trunc);
	if (!output) {
		throw std::runtime_error(std::format("cannot create {}", path));
	}
	output << std::format("transpose={}\n", transpose);
	output << std::format("threads_planned={}\n", threadsPlanned);
	output << std::format("max_id={}\n", maxId);
	output << std::format("interval_size={}\n", intervalSize);
	output << std::format("partitions={}\n", partitions);
	output << std::format("vertices={}\n", vertices);
	output << std::format("edges_raw={}\n", edgesRaw);
	output << std::format("edges={}\n", edges);
	output << std::format("dropped_self_loops={}\n", droppedSelfLoops);
	output << std::format("dropped_duplicates={}\n", droppedDuplicates);
	output << std::format("max_out_degree={}\n", maxOutDegree);
	output << std::format("max_in_degree={}\n", maxInDegree);
	if (!output.flush()) {
		throw std::runtime_error(std::format("cannot write {}", path));
	}
}

PartitionScheme Meta::Scheme() const {
	return PartitionScheme{maxId, partitions, intervalSize};
}

} // namespace lr::grid
