#ifdef __INTELLISENSE__
#include <ranges>
#include <algorithm>
#endif

import std;
import hlsdk;

import CBase;
import Message;

using std::int32_t;
using std::uint8_t;

template <typename T>
struct ItemSlotManager
{
	// Requires:
	// static constexpr SLOT_ITEMS_COUNT -> int
	// static constexpr SLOT_ID -> int
	// m_rgszSlotOccupied -> std::array<std::string_view, 31>

	// How will client.dll HUD sort your weapons?
	// There's 6 pistols in vanilla CS.
	uint8_t m_iOrderInSlot{ T::SLOT_ITEMS_COUNT + 1 };

	CBasePlayer* m_pPlayer{};

	constexpr int32_t NextFreeSlot(this auto&& self) /*const*/ noexcept
	{
		auto const it = std::ranges::find_if(
			self.m_rgszSlotOccupied,
			&std::string_view::empty
		);

		if (it == self.m_rgszSlotOccupied.cend())
			return -1;

		return it - self.m_rgszSlotOccupied.cbegin();
	}

	int32_t OccupySlot(this auto&& self, std::string_view szHud, int32_t iAmmoId, int32_t iAmmoMax, uint8_t bitsFlags = 0) noexcept
	{
		auto const iId = self.NextFreeSlot();

		if (iId < 0)
			return -1;

		gmsgWeaponList::Send(self.m_pPlayer->edict(),
			szHud.data(),
			(uint8_t)iAmmoId,
			(uint8_t)iAmmoMax,
			0,
			0,
			T::SLOT_ID,	// Slot ID, starting from 0
			self.m_iOrderInSlot++,
			iId,
			bitsFlags
		);

		// By the time we reach 128, the original order was certainly fully overrided.
		self.m_iOrderInSlot %= 128;

		if (self.m_iOrderInSlot < T::SLOT_ITEMS_COUNT + 1)	[[unlikely]]
			self.m_iOrderInSlot = T::SLOT_ITEMS_COUNT + 1;

		// Record what is using this slot so we can free that later.
		self.m_rgszSlotOccupied[iId] = szHud;

		return iId;
	}

	int32_t FreeSlot(this auto&& self, std::string_view szHud) noexcept
	{
		auto const it = std::ranges::find(
			self.m_rgszSlotOccupied,
			szHud
		);

		if (it == self.m_rgszSlotOccupied.cend())
			return -1;

		auto const iId = it - self.m_rgszSlotOccupied.cbegin();
		self.m_rgszSlotOccupied[iId] = "";

		return iId;
	}
};

struct PistolSlotManager : ItemSlotManager<PistolSlotManager>
{
	static inline constexpr auto SLOT_ITEMS_COUNT = 6;
	static inline constexpr auto SLOT_ID = 1;

	std::array<std::string_view, 31> m_rgszSlotOccupied
	{
		"WEAPON_NONE",
		"",						// WEAPON_P228
		"WEAPON_NIL",
		"WEAPON_SCOUT",
		"WEAPON_HEGRENADE",
		"WEAPON_XM1014",
		"WEAPON_C4",
		"WEAPON_MAC10",
		"WEAPON_AUG",
		"WEAPON_SMOKEGRENADE",
		"WEAPON_ELITE",			// Does not consider as a regular pistol. (onehanded)
		"",						// WEAPON_FIVESEVEN
		"WEAPON_UMP45",
		"WEAPON_SG550",
		"WEAPON_GALIL",
		"WEAPON_FAMAS",
		"",						// WEAPON_USP
		"",						// WEAPON_GLOCK18
		"WEAPON_AWP",
		"WEAPON_MP5N",
		"WEAPON_M249",
		"WEAPON_M3",
		"WEAPON_M4A1",
		"WEAPON_TMP",
		"WEAPON_G3SG1",
		"WEAPON_FLASHBANG",
		"",						// WEAPON_DEAGLE
		"WEAPON_SG552",
		"WEAPON_AK47",
		"WEAPON_KNIFE",
		"WEAPON_P90",
	};
};

static std::array<PistolSlotManager, 33> g_rgPlayerPistolMgrs{};

auto PistolSlotMgr(CBasePlayer* pPlayer) noexcept -> PistolSlotManager*
{
	auto& mgr = g_rgPlayerPistolMgrs.at(pPlayer->entindex());
	mgr.m_pPlayer = pPlayer;

	return std::addressof(mgr);
}

inline constexpr std::array PISTOLS_IDX{ WEAPON_P228, WEAPON_ELITE, WEAPON_FIVESEVEN, WEAPON_USP, WEAPON_GLOCK18, WEAPON_DEAGLE, };
inline constexpr std::array SHOTGUN_IDX{ WEAPON_XM1014, WEAPON_M3, };
inline constexpr std::array SMG_IDX{ WEAPON_MAC10, WEAPON_UMP45, WEAPON_MP5N, WEAPON_TMP, WEAPON_P90, };
inline constexpr std::array ASSAULT_IDX{ WEAPON_AUG, WEAPON_GALIL, WEAPON_FAMAS, WEAPON_M4A1, WEAPON_SG552, WEAPON_AK47, };
inline constexpr std::array SNIPER_IDX{ WEAPON_SCOUT, WEAPON_SG550, WEAPON_AWP, WEAPON_G3SG1, };
inline constexpr std::array LMG_IDX{ WEAPON_M249, };
inline constexpr std::array MELEE_IDX{ WEAPON_KNIFE, };
inline constexpr std::array THROWABLE_IDX{ WEAPON_HEGRENADE, WEAPON_SMOKEGRENADE, WEAPON_FLASHBANG, };
inline constexpr std::array EQUIPMENT_IDX{ WEAPON_C4, };

template <typename T, size_t I1, size_t I2, size_t... I_rests>
consteval auto MergeArray(std::array<T, I1> const& array1, std::array<T, I2> const& array2, std::array<T, I_rests> const&... rests)
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
		return MergeArray(merged, rests...);
	}
}

inline constexpr auto FULL_AUTO_IDX = MergeArray(SMG_IDX, ASSAULT_IDX, LMG_IDX);
