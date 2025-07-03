module;

#ifdef __INTELLISENSE__
#include <ranges>
#include <algorithm>
#endif

#include <string.h>

export module Ammo;

#ifdef __INTELLISENSE__
import std;
#else
import std.compat;	// #MSVC_BUG_STDCOMPAT
#endif
import hlsdk;

import UtlString;

export struct CAmmoInfo
{
	std::string_view m_szName{};
	uint8_t m_iAmmoId{};
	uint8_t m_iMax{};
	uint8_t m_iPenetrationPower{};
	uint16_t m_iPenetrationDistance{};
	float m_flCost{};

	[[nodiscard]] auto operator<=>(CAmmoInfo const& rhs) const noexcept
	{
		return _strnicmp(
			this->m_szName.data(),
			rhs.m_szName.data(),
			std::max(this->m_szName.length(), rhs.m_szName.length())
		) <=> 0;
	}

	[[nodiscard]] bool operator==(CAmmoInfo const& rhs) const noexcept
	{
		return _strnicmp(
			this->m_szName.data(),
			rhs.m_szName.data(),
			std::max(this->m_szName.length(), rhs.m_szName.length())
		) == 0;
	}
};

export [[nodiscard]] auto operator<=>(std::string_view lhs, CAmmoInfo const& rhs) noexcept
{
	return _strnicmp(
		lhs.data(),
		rhs.m_szName.data(),
		std::max(lhs.length(), rhs.m_szName.length())
	) <=> 0;
}

export [[nodiscard]] auto operator<=>(CAmmoInfo const& lhs, std::string_view rhs) noexcept
{
	// Three way cmp IS NOT commutative.
	// returning rhs <=> lhs will be a disaster.
	return _strnicmp(
		lhs.m_szName.data(),
		rhs.data(),
		std::max(lhs.m_szName.length(), rhs.length())
	) <=> 0;
}

export inline std::set<CAmmoInfo, std::less<>> gAmmoInfo	// #UPDATE_AT_CPP23 flat_set
{
	CAmmoInfo{ .m_szName{"338Magnum"},	.m_iAmmoId{1},	.m_iMax{30},	.m_iPenetrationPower{45},	.m_iPenetrationDistance{8000},	.m_flCost{125.f / 10.f},	},
	CAmmoInfo{ .m_szName{"762Nato"},	.m_iAmmoId{2},	.m_iMax{90},	.m_iPenetrationPower{39},	.m_iPenetrationDistance{5000},	.m_flCost{80.f / 30.f},	},
	CAmmoInfo{ .m_szName{"556NatoBox"},	.m_iAmmoId{3},	.m_iMax{200},	.m_iPenetrationPower{35},	.m_iPenetrationDistance{4000},	.m_flCost{60.f / 30.f},	},
	CAmmoInfo{ .m_szName{"556Nato"},	.m_iAmmoId{4},	.m_iMax{90},	.m_iPenetrationPower{35},	.m_iPenetrationDistance{4000},	.m_flCost{60.f / 30.f},	},
	CAmmoInfo{ .m_szName{"Buckshot"},	.m_iAmmoId{5},	.m_iMax{32},	.m_iPenetrationPower{0},	.m_iPenetrationDistance{0},		.m_flCost{65.f / 8.f},	},
	CAmmoInfo{ .m_szName{"45ACP"},		.m_iAmmoId{6},	.m_iMax{100},	.m_iPenetrationPower{15},	.m_iPenetrationDistance{500},	.m_flCost{25.f / 12.f},	},
	CAmmoInfo{ .m_szName{"57MM"},		.m_iAmmoId{7},	.m_iMax{100},	.m_iPenetrationPower{30},	.m_iPenetrationDistance{2000},	.m_flCost{50.f / 50.f},	},
	CAmmoInfo{ .m_szName{"50AE"},		.m_iAmmoId{8},	.m_iMax{35},	.m_iPenetrationPower{30},	.m_iPenetrationDistance{1000},	.m_flCost{40.f / 7.f},	},
	CAmmoInfo{ .m_szName{"357SIG"},		.m_iAmmoId{9},	.m_iMax{52},	.m_iPenetrationPower{25},	.m_iPenetrationDistance{800},	.m_flCost{50.f / 13.f},	},
	CAmmoInfo{ .m_szName{"9mm"},		.m_iAmmoId{10},	.m_iMax{120},	.m_iPenetrationPower{21},	.m_iPenetrationDistance{800},	.m_flCost{20.f / 30.f},	},
};

export inline
auto Ammo_Register(CAmmoInfo AmmoInfo) noexcept -> CAmmoInfo const*
{
	AmmoInfo.m_iAmmoId = static_cast<decltype(CAmmoInfo::m_iAmmoId)>(gAmmoInfo.size() + 1);	// Starting from 1.

	auto&& [it, bNew] = gAmmoInfo.emplace(std::move(AmmoInfo));

	return std::addressof(*it);
}

export [[nodiscard]] inline
auto Ammo_InfoByName(std::string_view sz) noexcept -> CAmmoInfo const*
{
	if (auto const it = gAmmoInfo.find(sz); it != gAmmoInfo.cend())
		return std::addressof(*it);

	return nullptr;
}

export [[nodiscard]] inline
auto Ammo_InfoByIndex(int iId) noexcept -> CAmmoInfo const*
{
	for (auto& AmmoInfo : gAmmoInfo)
	{
		if (AmmoInfo.m_iAmmoId == iId)
			return std::addressof(AmmoInfo);
	}

	return nullptr;
}
