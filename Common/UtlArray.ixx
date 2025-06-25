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

export template <auto lhs, auto rhs> [[nodiscard]]
consteval auto UTIL_ArraySetDiff() noexcept
{
	constexpr auto fnVectorMaker =
		[]() static consteval
	{
		auto l_cpy{ lhs };
		auto r_cpy{ rhs };

		std::ranges::sort(l_cpy);
		std::ranges::sort(r_cpy);

		std::vector<typename std::remove_cvref_t<decltype(lhs)>::value_type> r{};
		std::ranges::set_difference(l_cpy, r_cpy, std::back_inserter(r));

		return r;
	};

	auto const vec = fnVectorMaker();
	constexpr auto iSize = fnVectorMaker().size();	// #MSVC_BUG_CONSTEVAL
	std::array<typename std::remove_cvref_t<decltype(lhs)>::value_type, iSize> arr{};

	std::ranges::copy_n(vec.cbegin(), std::ssize(vec), arr.begin());

	return arr;
}

export template <typename T> [[nodiscard]]
auto UTIL_ArraySetDiff(std::span<T const> lhs, std::span<T const> rhs) noexcept
{
	std::set<T, std::ranges::less> const l_ordered{ std::from_range, lhs };	// #UPDATE_AT_CPP23 flat_set
	std::set<T, std::ranges::less> const r_ordered{ std::from_range, rhs };	// #UPDATE_AT_CPP23 flat_set

	std::vector<T> r{};
	std::ranges::set_difference(l_ordered, r_ordered, std::back_inserter(r));

	return r;
}
