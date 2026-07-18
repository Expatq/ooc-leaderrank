#include <prepare/lib/prepare_job.hpp>

#include <common/cli/cli_options.hpp>
#include <common/core/byte_size.hpp>
#include <common/io/peak_rss.hpp>

#include <format>
#include <iostream>

int main(int argc, char** argv) {
	try {
		lr::prepare::PrepareConfig config;
		lr::common::CliOptions opts(argc, argv);

		opts.AddUsage("build a grid-partitioned graph from an edge list");
		opts.AddPositional("edges", "input edge list (csv)").StoreResult(&config.edgesPath);
		opts.AddPositional("workdir", "output directory").StoreResult(&config.workdir);
		opts.AddOption("--budget", "memory budget, e.g. 128M or 2G")
		    .StoreResult(&config.budgetBytes, lr::common::ByteSize::Parse);
		opts.AddOption("--threads", "worker thread count (default: 1)")
		    .Optional()
		    .StoreResult(&config.threadsPlanned);
		opts.AddFlag("--transpose", "also build the transposed adjacency").StoreResult(&config.transpose);
		opts.Parse();

		lr::prepare::PrepareJob job(config, &std::cout);
		job.Run();
		
		std::cout << std::format("done: peak_rss_kib={}\n", lr::common::PeakRss::Kib());
		return 0;
	} catch (const std::exception& error) {
		std::cerr << std::format("lr-prepare: {}\n", error.what());
		return 1;
	}
}
