#include <rank/lib/rank_job.hpp>

#include <common/cli/cli_options.hpp>
#include <common/core/byte_size.hpp>
#include <common/core/constants.hpp>
#include <common/io/peak_rss.hpp>

#include <format>
#include <iostream>

int main(int argc, char** argv) {
	try {
		lr::rank::RankConfig config;
		config.eps = lr::common::kDefaultEps;
		config.maxIterations = lr::common::kDefaultMaxIterations;

		lr::common::CliOptions opts(argc, argv);
		opts.AddUsage("run LeaderRank over a prepared grid-partitioned graph");
		opts.AddPositional("workdir", "partitioned graph directory").StoreResult(&config.workdir);
		opts.AddOption("--budget", "memory budget, e.g. 128M or 2G")
		    .StoreResult(&config.budgetBytes, lr::common::ByteSize::Parse);
		opts.AddOption("--out", "output ranks csv path").StoreResult(&config.outPath);
		opts.AddOption("--eps", "convergence threshold (default: 1e-9)").Optional().StoreResult(&config.eps);
		opts.AddOption("--max-iters", "iteration cap (default: 500)")
		    .Optional()
		    .StoreResult(&config.maxIterations);
		opts.Parse();

		lr::rank::RankJob job(config);
		const lr::rank::RankResult result = job.Run();
		if (!result.converged) {
			std::cerr << std::format(
			    "WARNING: did not converge within {} iterations, actual L1/N = {:.3e}\n",
			    result.iterations, result.finalDeltaPerVertex);
		}
		std::cout << std::format("done: iterations={} converged={} peak_rss_kib={}\n",
		                         result.iterations, result.converged, lr::common::PeakRss::Kib());
		return 0;
	} catch (const std::exception& error) {
		std::cerr << std::format("lr-rank: {}\n", error.what());
		return 1;
	}
}
