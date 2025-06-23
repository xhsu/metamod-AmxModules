export module UtlArray;

import std;

export template <typename T, size_t I1, size_t I2, size_t... I_rests> [[nodiscard]]
constexpr auto UTIL_MergeArray(std::array<T, I1> const& array1, std::array<T, I2> const& array2, std::array<T, I_rests> const&... rests) noexcept
{
	auto merged = [&] <size_t... Is1, size_t... Is2>(std::index_sequence<Is1...>, std::index_sequence<Is2...>)
	{
		return std::array{ array1[Is1]..., array2[Is2]... };
	}
	(std::make_index_sequence<I1>{}, std::make_index_sequence<I2>{});

	std::ranges::sort(merged);

	if constexpr (sizeof...(rests) == 0)
	{
		return merged;
	}
	else
	{
		return UTIL_MergeArray(merged, rests...);
	}
}
