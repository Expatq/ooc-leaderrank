#include "cli_options.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>

namespace lr::common {

namespace {

constexpr static std::string_view kOptionPrefix = "--";
constexpr static std::string_view kHelpFlag = "--help";

std::string SpecLabel(bool positional, bool takesValue, const std::string& name) {
	if (positional) {
		return std::format("<{}>", name);
	}
	return takesValue ? std::format("{} <value>", name) : name;
}

} // namespace

CliOptions::CliOptions(int argc, char** argv) {
	Tokens_.reserve(argc > 1 ? argc - 1 : 0);
	for (int i = 1; i < argc; ++i) {
		Tokens_.emplace_back(argv[i]);
	}
	Consumed_.assign(Tokens_.size(), false);

	ProgramName_ = "<tool>";
	if (argc > 0 && argv[0] != nullptr) {
		const std::string_view path(argv[0]);
		const auto slashPos = path.find_last_of('/');
		ProgramName_ = std::string(slashPos == std::string::npos ? path : path.substr(slashPos + 1));
	}
}

CliOptions& CliOptions::AddUsage(std::string description) {
	Description_ = std::move(description);
	return *this;
}

CliOptions& CliOptions::AddOption(std::string name, std::string help) {
	return AddSpec(Kind::Option, std::move(name), std::move(help));
}

CliOptions& CliOptions::AddPositional(std::string name, std::string help) {
	return AddSpec(Kind::Positional, std::move(name), std::move(help));
}

CliOptions& CliOptions::AddFlag(std::string name, std::string help) {
	return AddSpec(Kind::Flag, std::move(name), std::move(help));
}

CliOptions& CliOptions::Required() {
	RequireNotFlag("Required()");
	LastSpec().required = true;
	return *this;
}

CliOptions& CliOptions::Optional() {
	RequireNotFlag("Optional()");
	LastSpec().required = false;
	return *this;
}

void CliOptions::Parse() {
	for (const Spec& spec : Specs_) {
		if (spec.kind == Kind::Flag ? spec.flagTarget == nullptr : !spec.assign) {
			throw std::logic_error(std::format("{} has no StoreResult() target bound", spec.name));
		}
	}

	for (const std::string& token : Tokens_) {
		if (token == kHelpFlag) {
			std::cout << Help();
			std::exit(0);
		}
	}

	for (const Spec& spec : Specs_) {
		if (spec.kind == Kind::Flag) {
			*spec.flagTarget = ConsumeFlag(spec.name);
		} else if (spec.kind == Kind::Option) {
			if (const auto value = ConsumeValue(spec.name)) {
				Assign(spec, *value);
			} else if (spec.required) {
				Fail(std::format("missing required option {}", spec.name));
			}
		}
	}

	std::vector<std::string> leftovers;
	for (size_t i = 0; i < Tokens_.size(); ++i) {
		if (!Consumed_[i] && !Tokens_[i].starts_with(kOptionPrefix)) {
			leftovers.push_back(Tokens_[i]);
			Consumed_[i] = true;
		}
	}
	size_t next = 0;
	for (const Spec& spec : Specs_) {
		if (spec.kind != Kind::Positional) {
			continue;
		}
		if (next < leftovers.size()) {
			Assign(spec, leftovers[next++]);
		} else if (spec.required) {
			Fail(std::format("missing required argument <{}>", spec.name));
		}
	}
	if (next < leftovers.size()) {
		Fail(std::format("unexpected argument: {}", leftovers[next]));
	}

	for (size_t i = 0; i < Tokens_.size(); ++i) {
		if (!Consumed_[i]) {
			Fail(std::format("unknown argument: {}", Tokens_[i]));
		}
	}
}

CliOptions& CliOptions::AddSpec(Kind kind, std::string name, std::string help) {
	if (name == kHelpFlag) {
		throw std::logic_error("--help is reserved");
	}
	Specs_.push_back({kind, std::move(name), std::move(help), kind != Kind::Flag, {}, nullptr});
	return *this;
}

CliOptions::Spec& CliOptions::LastSpec() {
	if (Specs_.empty()) {
		throw std::logic_error("no option registered yet, call AddOption | AddPositional | AddFlag first");
	}
	return Specs_.back();
}

void CliOptions::RequireNotFlag(std::string_view method) {
	if (LastSpec().kind == Kind::Flag) {
		throw std::logic_error(std::format("{}: {} is not applicable to flags", LastSpec().name, method));
	}
}

void CliOptions::FailBoolOption() {
	throw std::logic_error(std::format("{}: bool options are not supported, use AddFlag instead", LastSpec().name));
}

void CliOptions::Assign(const Spec& spec, std::string_view value) {
	try {
		spec.assign(value);
	} catch (const std::exception& error) {
		Fail(std::format("invalid value for {}: '{}' ({})", spec.name, value, error.what()));
	}
}

std::optional<std::string> CliOptions::ConsumeValue(std::string_view name) {
	for (size_t i = 0; i < Tokens_.size(); ++i) {
		if (Consumed_[i] || Tokens_[i] != name) {
			continue;
		}
		if (i + 1 >= Tokens_.size() || Consumed_[i + 1]) {
			Fail(std::format("option {} requires a value", name));
		}
		Consumed_[i] = true;
		Consumed_[i + 1] = true;
		return Tokens_[i + 1];
	}
	return std::nullopt;
}

bool CliOptions::ConsumeFlag(std::string_view name) {
	for (size_t i = 0; i < Tokens_.size(); ++i) {
		if (!Consumed_[i] && Tokens_[i] == name) {
			Consumed_[i] = true;
			return true;
		}
	}
	return false;
}

std::string CliOptions::UsageLine() const {
	std::string out = std::format("usage: {}", ProgramName_);
	for (const Spec& spec : Specs_) {
		const std::string label = SpecLabel(spec.kind == Kind::Positional, spec.kind == Kind::Option, spec.name);
		out += spec.required ? std::format(" {}", label) : std::format(" [{}]", label);
	}
	out += " [--help]";
	return out;
}

std::string CliOptions::Help() const {
	std::string out = UsageLine() + "\n";
	if (!Description_.empty()) {
		out += "\n" + Description_ + "\n";
	}

	std::vector<std::pair<std::string, std::string>> rows;
	for (const Spec& spec : Specs_) {
		std::string help = spec.help;
		if (spec.kind == Kind::Option && spec.required) {
			help += help.empty() ? "(required)" : " (required)";
		}
		rows.emplace_back(SpecLabel(spec.kind == Kind::Positional, spec.kind == Kind::Option, spec.name), std::move(help));
	}
	rows.emplace_back(std::string(kHelpFlag), "show this help and exit");

	size_t width = 0;
	for (const auto& [label, help] : rows) {
		width = std::max(width, label.size());
	}
	out += "\n";
	for (const auto& [label, help] : rows) {
		out += std::format("  {:<{}}  {}\n", label, width, help);
	}
	return out;
}

void CliOptions::Fail(const std::string& message) const {
	throw std::runtime_error(std::format("{}\n{}\n(run with --help for details)", message, UsageLine()));
}

} // namespace lr::common
