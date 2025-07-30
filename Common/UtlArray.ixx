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

export template <auto fnFirstArrMaker, auto... fnRests>
[[nodiscard]] consteval auto UTIL_MakeArray() noexcept
{
	constexpr auto fnHelper =
		[]()
		{
			std::vector res{ std::from_range, fnFirstArrMaker() };

			if constexpr (sizeof...(fnRests))
				(res.append_range(fnRests()), ...);

			return res;
		};

	constexpr auto iSize = fnHelper().size();
	const auto dat = fnHelper();

	std::array<std::remove_cvref_t<decltype(fnHelper()[0])>, iSize> res{};
	std::ranges::copy_n(dat.cbegin(), iSize, res.begin());
	return res;
}

export template <auto fnFirstArrMaker, auto... fnRests>
[[nodiscard]] consteval auto UTIL_MakeArray2D() noexcept
{
	if constexpr (sizeof...(fnRests))
		static_assert(std::conjunction_v<std::is_same<std::remove_cvref_t<decltype(fnFirstArrMaker()[0])>, std::remove_cvref_t<decltype(fnRests()[0])>>...>);

	constexpr auto fnHelper =
		[]()
		{
			std::vector<std::span<std::remove_cvref_t<decltype(fnFirstArrMaker()[0])> const>>
				res{ fnFirstArrMaker(), fnRests()... };

			return res;
		};

	constexpr auto iSize = fnHelper().size();
	const auto dat = fnHelper();

	std::array<std::span<std::remove_cvref_t<decltype(fnFirstArrMaker()[0])> const>, iSize> res{};
	std::ranges::copy_n(dat.cbegin(), iSize, res.begin());

	return res;
}

export template <auto fnArrMaker1, auto fnArrMaker2> [[nodiscard]]
consteval auto UTIL_ArraySetDiff() noexcept
{
	constexpr auto fnVectorMaker =
		[]() static consteval
	{
		std::vector l_cpy{ std::from_range , fnArrMaker1() };
		std::vector r_cpy{ std::from_range , fnArrMaker2() };

		std::ranges::sort(l_cpy);
		std::ranges::sort(r_cpy);

		std::vector<typename std::remove_cvref_t<decltype(l_cpy)>::value_type> r{};
		std::ranges::set_difference(l_cpy, r_cpy, std::back_inserter(r));

		return r;
	};

	auto const vec = fnVectorMaker();
	constexpr auto iSize = fnVectorMaker().size();	// #MSVC_BUG_CONSTEVAL
	std::array<typename std::remove_cvref_t<decltype(fnArrMaker1())>::value_type, iSize> arr{};

	std::ranges::copy_n(vec.cbegin(), iSize, arr.begin());

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
