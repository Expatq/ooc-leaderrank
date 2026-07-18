#pragma once

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace lr::common {

template <typename Parser, typename T>
concept ParserFor = requires(const Parser& parser, std::string_view text) {
	{ parser(text) } -> std::convertible_to<T>;
};

class CliOptions {
public:
	CliOptions(int argc, char** argv);

	CliOptions& AddUsage(std::string description);
	CliOptions& AddOption(std::string name, std::string help = "");
	CliOptions& AddPositional(std::string name, std::string help = "");
	CliOptions& AddFlag(std::string name, std::string help = "");

	CliOptions& Required();
	CliOptions& Optional();

	template <typename T>
	CliOptions& StoreResult(T* target) {
		if constexpr (std::is_same_v<T, bool>) {
			if (LastSpec().kind != Kind::Flag) {
				FailBoolOption();
			}
			LastSpec().flagTarget = target;
			return *this;
		} else {
			if (LastSpec().kind == Kind::Flag) {
				throw std::logic_error(LastSpec().name + ": flag target must be bool*");
			}
			return StoreResult(target, ParseAs<T>);
		}
	}

	template <typename T, ParserFor<T> Parser>
	CliOptions& StoreResult(T* target, Parser parser) {
		static_assert(!std::is_same_v<T, bool>, "bool options are not supported, use AddFlag instead");
		RequireNotFlag("StoreResult(target, parser)");
		LastSpec().assign = [target, parser](std::string_view raw) {
			*target = parser(raw);
		};
		return *this;
	}

	void Parse();

private:
	enum class Kind { Option,
		              Positional,
		              Flag };

	struct Spec {
		Kind kind;
		std::string name;
		std::string help;
		bool required;
		std::function<void(std::string_view)> assign;
		bool* flagTarget = nullptr;
	};

private:
	template <typename T>
	static T ParseAs(std::string_view text) {
		if constexpr (std::is_same_v<T, std::string>) {
			return std::string(text);
		} else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
			T value{};
			const auto [end, errCode] =
			    std::from_chars(text.data(), text.data() + text.size(), value);
			if (errCode != std::errc{} || end != text.data() + text.size()) {
				throw std::runtime_error("not a valid integer");
			}
			return value;
		} else if constexpr (std::is_floating_point_v<T>) {
			size_t consumedChars = 0;
			const double parsedValue = std::stod(std::string(text), &consumedChars);
			if (consumedChars != text.size()) {
				throw std::runtime_error("not a valid floating point number");
			}
			return static_cast<T>(parsedValue);
		} else {
			static_assert(false, "no default parser for this type, pass one to StoreResult(target, parser)");
		}
	}

private:
	CliOptions& AddSpec(Kind kind, std::string name, std::string help);
	Spec& LastSpec();

	void RequireNotFlag(std::string_view method);
	[[noreturn]] void FailBoolOption();

	void Assign(const Spec& spec, std::string_view value);
	std::optional<std::string> ConsumeValue(std::string_view name);
	bool ConsumeFlag(std::string_view name);

	std::string UsageLine() const;
	std::string Help() const;
	[[noreturn]] void Fail(const std::string& message) const;

private:
	std::vector<std::string> Tokens_;
	std::vector<bool> Consumed_;
	std::vector<Spec> Specs_;
	std::string ProgramName_;
	std::string Description_;
};

} // namespace lr::common
