module;

#ifdef __INTELLISENSE__
#include <ranges>
#endif

export module UtlString;

import std;

export auto UTIL_Split(std::string_view const& s, char const* delimiters) noexcept -> std::vector<std::string_view>
{
	std::vector<std::string_view> ret{};

	for (auto lastPos = s.find_first_not_of(delimiters, 0), pos = s.find_first_of(delimiters, lastPos);
		s.npos != pos || s.npos != lastPos;
		lastPos = s.find_first_not_of(delimiters, pos), pos = s.find_first_of(delimiters, lastPos)
		)
	{
		ret.emplace_back(s.substr(lastPos, pos - lastPos));
	}

	return ret;
}

export template <typename T>
constexpr T UTIL_StrToNum(const std::string_view& sz) noexcept
{
	[[maybe_unused]] int iBase = 10;

	// Negative number doesn't supported.
	if (sz.starts_with("0x") || sz.starts_with("0X"))
		iBase = 16;
	else if (sz.starts_with("0o") || sz.starts_with("0O"))
		iBase = 8;
	else if (sz.starts_with("0b") || sz.starts_with("0B"))
		iBase = 2;

	[[maybe_unused]] std::ptrdiff_t const src = iBase == 10 ? 0 : 2;

	if constexpr (std::is_enum_v<T>)
	{
		if (std::underlying_type_t<T> ret{}; std::from_chars(sz.data() + src, sz.data() + sz.size(), ret, iBase).ec == std::errc{})
			return static_cast<T>(ret);
	}
	else if constexpr (std::is_integral_v<T>)
	{
		if (T ret{}; std::from_chars(sz.data() + src, sz.data() + sz.size(), ret, iBase).ec == std::errc{})
			return ret;
	}
	else if constexpr (std::is_floating_point_v<T>)
	{
		if (T ret{}; std::from_chars(sz.data(), sz.data() + sz.size(), ret).ec == std::errc{})
			return ret;
	}
	else
	{
		static_assert(false, "<T> must be an enum type or an arithmetic type.");
	}

	return T{};
}

export int UTIL_GetStringType(const char* src) noexcept	// [0 - string] [1 - integer] [2 - float]
{
	// is multi char ?
	if (*src <= 0)
		return 0;

	// is '-' or digit ?
	if (*src == '-' || std::isdigit(*src))
	{
		// "1"
		if (std::isdigit(*src) && !*(src + 1))
			return 1;

		++src; // next char

		// "-a" or "0a"
		if (!std::isdigit(*src) && *src != '.')
			return 0;

		while (*src)
		{
			// "1." or "-1."
			if (*src == '.')
			{
				++src; // next char

				// we need a digit, "1." not a float
				if (!*src)
					return 0;

				while (*src)
				{
					// "1.a"
					if (!std::isdigit(*src))
						return 0;

					++src;
				}
				// float value
				return 2;
			}

			// "10a" not a integer
			if (!std::isdigit(*src))
				return 0;

			++src; // next char
		}

		// integer value
		return 1;
	}

	return 0;
}

export void UTIL_ReplaceAll(std::string* str, const std::string_view& from, const std::string_view& to) noexcept
{
	if (from.empty())
		return;

	std::size_t start_pos = 0;
	while ((start_pos = str->find(from, start_pos)) != str->npos)
	{
		str->replace(start_pos, from.length(), to);
		start_pos += to.length();	// In case 'to' contains 'from', like replacing 'x' with 'yx'
	}
}

export struct sv_less_t final
{
	static constexpr bool operator()(std::string_view const& lhs, std::string_view const& rhs) noexcept
	{
		return lhs < rhs;
	}

	using is_transparent = int;
};

export struct sv_iless_t final
{
	struct nocase_compare
	{
		static bool operator()(char c1, char c2) noexcept
		{
			return std::tolower(c1) < std::tolower(c2);
		}
	};

	static bool operator()(std::string_view const& lhs, std::string_view const& rhs) noexcept
	{
		return std::ranges::lexicographical_compare(lhs, rhs, nocase_compare{});
	}

	using is_transparent = int;
};
