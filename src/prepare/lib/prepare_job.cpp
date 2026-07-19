#include "prepare_job.hpp"

#include <common/core/constants.hpp>
#include <common/thread/thread_pool.hpp>
#include <grid/layout.hpp>
#include <grid/meta.hpp>

#include <filesystem>
#include <format>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace lr::prepare {

PrepareJob::PrepareJob(PrepareConfig config, std::ostream* progress)
    : Config_(std::move(config)), Progress_(progress) {}

PrepareReport PrepareJob::Run() {
	PrepareReport report{};
	common::ThreadPool pool(Config_.threadsPlanned);

	report.scan = common::EdgeScanner(Config_.edgesPath, &pool).Run();
	if (report.scan.edgesRaw == 0) {
		throw std::runtime_error("input file contains no edges");
	}
	const uint64_t vertexSpan = uint64_t{report.scan.maxId} + 1;
	if (vertexSpan > common::kSparseIdRatioLimit * report.scan.edgesRaw) {
		throw std::runtime_error(
		    std::format("ids are sparse: max_id+1 = {} with only {} edges — renumber vertices "
		                "densely",
		                vertexSpan, report.scan.edgesRaw));
	}
	Report(std::format("scan: max_id={} edges={}", report.scan.maxId, report.scan.edgesRaw));

	const grid::PartitionPlanner planner(Config_.budgetBytes, Config_.threadsPlanned);
	report.scheme = planner.Plan(report.scan.maxId, report.scan.edgesRaw);
	Report(std::format("partitioning: P={} interval={}", report.scheme.partitions, report.scheme.intervalSize));

	RecreateWorkdir();
	const grid::WorkdirRoot root(Config_.workdir);
	{
		EdgeScatterer scatterer(report.scheme, root, planner.UsableBytes(), &pool);
		report.scatter = scatterer.Run(Config_.edgesPath, Config_.transpose);
	}
	Report(std::format("scatter: edges={} self_loops_dropped={}", report.scatter.edgesWritten, report.scatter.droppedSelfLoops));

	{
		BlockAssembler assembler(report.scheme, root, planner.UsableBytes(), &pool);
		report.assembly = assembler.Run();
	}
	Report(std::format("assemble: duplicates_dropped={} max_in_degree={}", report.assembly.droppedDuplicates, report.assembly.maxInDegree));

	{
		DegreeBuilder degrees(report.scheme, root, planner.IoChunkBytes(), &pool);
		report.degrees = degrees.Run();
	}
	Report(std::format("degrees: vertices={} max_out_degree={}", report.degrees.vertices, report.degrees.maxOutDegree));

	WriteMeta(report);
	return report;
}

void PrepareJob::RecreateWorkdir() const {
	const grid::WorkdirRoot root(Config_.workdir);
	std::filesystem::remove_all(root.Path());
	std::filesystem::create_directories(root.DegreesDir().Path());
	std::filesystem::create_directories(root.PresentDir().Path());
}

void PrepareJob::WriteMeta(const PrepareReport& report) const {
	grid::Meta meta;
	meta.scheme = report.scheme;
	meta.transpose = Config_.transpose ? 1 : 0;
	meta.threadsPlanned = Config_.threadsPlanned;
	meta.vertices = report.degrees.vertices;
	meta.edgesRaw = report.scan.edgesRaw;
	meta.edges = report.scatter.edgesWritten - report.assembly.droppedDuplicates;
	meta.droppedSelfLoops = report.scatter.droppedSelfLoops;
	meta.droppedDuplicates = report.assembly.droppedDuplicates;
	meta.maxOutDegree = report.degrees.maxOutDegree;
	meta.maxInDegree = report.assembly.maxInDegree;
	meta.Save(grid::WorkdirRoot(Config_.workdir));
}

void PrepareJob::Report(const std::string& line) const {
	if (Progress_ != nullptr) {
		*Progress_ << line << '\n';
		Progress_->flush();
	}
}

} // namespace lr::prepare
