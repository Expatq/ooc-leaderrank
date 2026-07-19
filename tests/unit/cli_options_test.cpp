#include <common/cli/cli_options.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using lr::common::CliOptions;

CliOptions Make(std::vector<std::string> args) {
	static std::vector<std::string> storage;
	static std::vector<char*> pointers;
	storage = std::move(args);
	pointers.clear();
	pointers.push_back(nullptr);
	for (std::string& arg : storage) {
		pointers.push_back(arg.data());
	}
	return CliOptions(static_cast<int>(pointers.size()), pointers.data());
}

TEST(CliOptions, ParsesOptionsFlagsAndPositionals) {
	std::string edges, workdir, budget;
	bool transpose = false;
	CliOptions opts = Make({"edges.csv", "--budget", "128M", "workdir", "--transpose"});
	opts.AddPositional("edges").StoreResult(&edges);
	opts.AddPositional("workdir").StoreResult(&workdir);
	opts.AddOption("--budget").StoreResult(&budget);
	opts.AddFlag("--transpose").StoreResult(&transpose);
	opts.Parse();
	EXPECT_EQ(edges, "edges.csv");
	EXPECT_EQ(workdir, "workdir");
	EXPECT_EQ(budget, "128M");
	EXPECT_TRUE(transpose);
}

TEST(CliOptions, ModifierOrderDoesNotMatter) {
	uint32_t threads = 1;
	CliOptions opts = Make({"--threads", "8"});
	opts.AddOption("--threads").StoreResult(&threads).Optional();
	opts.Parse();
	EXPECT_EQ(threads, 8u);
}

TEST(CliOptions, OptionalOptionKeepsDefault) {
	double eps = 1e-9;
	std::string workdir;
	CliOptions opts = Make({"workdir"});
	opts.AddPositional("workdir").StoreResult(&workdir);
	opts.AddOption("--eps").Optional().StoreResult(&eps);
	opts.Parse();
	EXPECT_DOUBLE_EQ(eps, 1e-9);
}

TEST(CliOptions, CustomParserIsApplied) {
	uint64_t budget = 0;
	CliOptions opts = Make({"--budget", "2K"});
	opts.AddOption("--budget").StoreResult(&budget, [](std::string_view text) -> uint64_t {
		return std::stoull(std::string(text.substr(0, text.size() - 1))) * 1024;
	});
	opts.Parse();
	EXPECT_EQ(budget, 2048u);
}

TEST(CliOptions, MissingFlagYieldsFalse) {
	bool transpose = true;
	CliOptions opts = Make({});
	opts.AddFlag("--transpose").StoreResult(&transpose);
	opts.Parse();
	EXPECT_FALSE(transpose);
}

TEST(CliOptions, OptionIsRequiredByDefault) {
	std::string budget;
	CliOptions opts = Make({});
	opts.AddOption("--budget").StoreResult(&budget);
	EXPECT_THROW(opts.Parse(), std::runtime_error);
}

TEST(CliOptions, MissingRequiredPositionalFails) {
	std::string workdir;
	CliOptions opts = Make({});
	opts.AddPositional("workdir").StoreResult(&workdir);
	EXPECT_THROW(opts.Parse(), std::runtime_error);
}

TEST(CliOptions, OptionWithoutValueFails) {
	std::string budget;
	CliOptions opts = Make({"--budget"});
	opts.AddOption("--budget").StoreResult(&budget);
	EXPECT_THROW(opts.Parse(), std::runtime_error);
}

TEST(CliOptions, UnknownArgumentFails) {
	std::string workdir;
	CliOptions opts = Make({"--mystery", "workdir"});
	opts.AddPositional("workdir").StoreResult(&workdir);
	EXPECT_THROW(opts.Parse(), std::runtime_error);
}

TEST(CliOptions, ExtraPositionalFails) {
	std::string workdir;
	CliOptions opts = Make({"workdir", "surprise"});
	opts.AddPositional("workdir").StoreResult(&workdir);
	EXPECT_THROW(opts.Parse(), std::runtime_error);
}

TEST(CliOptions, InvalidNumberFails) {
	uint32_t threads = 1;
	CliOptions opts = Make({"--threads", "12abc"});
	opts.AddOption("--threads").Optional().StoreResult(&threads);
	EXPECT_THROW(opts.Parse(), std::runtime_error);
}

TEST(CliOptions, UnboundTargetIsLogicError) {
	CliOptions opts = Make({});
	opts.AddOption("--budget");
	EXPECT_THROW(opts.Parse(), std::logic_error);
}

TEST(CliOptions, ModifierBeforeAnySpecIsLogicError) {
	CliOptions opts = Make({});
	EXPECT_THROW(opts.Required(), std::logic_error);
}

TEST(CliOptions, RequiredOnFlagIsLogicError) {
	CliOptions opts = Make({});
	EXPECT_THROW(opts.AddFlag("--transpose").Required(), std::logic_error);
}

TEST(CliOptions, BoolTargetOnOptionIsLogicError) {
	bool value = false;
	CliOptions opts = Make({});
	EXPECT_THROW(opts.AddOption("--verbose").StoreResult(&value), std::logic_error);
}

TEST(CliOptions, HelpFlagNameIsReserved) {
	CliOptions opts = Make({});
	EXPECT_THROW(opts.AddFlag("--help"), std::logic_error);
}

TEST(CliOptions, HelpPrintsAndExitsZero) {
	std::string budget;
	EXPECT_EXIT(
	    {
		    CliOptions opts = Make({"--help"});
		    opts.AddUsage("test tool");
		    opts.AddOption("--budget", "memory budget").StoreResult(&budget);
		    opts.Parse();
	    },
	    testing::ExitedWithCode(0), "");
}

} // namespace
