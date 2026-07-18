#pragma once

#include "grid_builder.hpp"

#include <common/csv/csv_reader.hpp>
#include <grid/partition.hpp>

#include <cstdint>
#include <iosfwd>
#include <string>

namespace lr::prepare {

struct PrepareConfig {
	std::string edgesPath;
	std::string workdir;
	uint64_t budgetBytes = 0;
	uint32_t threadsPlanned = 1;
	bool transpose = false;
};

struct PrepareReport {
	grid::PartitionScheme scheme;
	common::ScanResult scan;
	ScatterResult scatter;
	AssembleResult assembly;
	DegreesResult degrees;
};

class PrepareJob {
public:
	PrepareJob(PrepareConfig config, std::ostream* progress);

	PrepareReport Run();

private:
	void RecreateWorkdir() const;
	void WriteMeta(const PrepareReport& report) const;
	void Report(const std::string& line) const;

	PrepareConfig Config_;
	std::ostream* Progress_;
};

} // namespace lr::prepare
