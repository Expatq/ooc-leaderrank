#include "meta.hpp"

#include <charconv>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace lr::grid {

namespace {

constexpr static char kKeyValueSeparator = '=';

template <typename MetaRef, typename Action>
void VisitFields(MetaRef* meta, Action action) {
	action("transpose", &meta->transpose);
	action("threads_planned", &meta->threadsPlanned);
	action("max_id", &meta->scheme.maxId);
	action("partitions", &meta->scheme.partitions);
	action("vertices", &meta->vertices);
	action("edges_raw", &meta->edgesRaw);
	action("edges", &meta->edges);
	action("dropped_self_loops", &meta->droppedSelfLoops);
	action("dropped_duplicates", &meta->droppedDuplicates);
	action("max_out_degree", &meta->maxOutDegree);
	action("max_in_degree", &meta->maxInDegree);
}

uint64_t GetValue(const std::map<std::string, uint64_t, std::less<>>& values, const std::string& path, std::string_view key) {
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
	VisitFields(&meta, [&values, &path](std::string_view key, auto* field) {
		using Field = std::remove_reference_t<decltype(*field)>;
		const uint64_t value = GetValue(values, path, key);
		if constexpr (!std::is_same_v<Field, uint64_t>) {
			if (value > std::numeric_limits<Field>::max()) {
				throw std::runtime_error(std::format("{}: value out of range for {}", path, key));
			}
		}
		*field = static_cast<Field>(value);
	});
	if (meta.scheme.partitions == 0) {
		throw std::runtime_error(std::format("{}: partitions must be positive", path));
	}
	meta.scheme = PartitionScheme::Create(meta.scheme.maxId, meta.scheme.partitions);
	return meta;
}

void Meta::Save(const WorkdirRoot& workdir) const {
	const std::string path = workdir.MetaFile().string();
	std::ofstream output(path, std::ios::trunc);
	if (!output) {
		throw std::runtime_error(std::format("cannot create {}", path));
	}
	VisitFields(this, [&output](std::string_view key, const auto* field) {
		output << std::format("{}={}\n", key, *field);
	});
	if (!output.flush()) {
		throw std::runtime_error(std::format("cannot write {}", path));
	}
}

} // namespace lr::grid
