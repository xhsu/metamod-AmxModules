module;

#ifdef __INTELLISENSE__
#include <ranges>
#include <algorithm>
#endif

export module WpnIdAllocator;

#ifdef __INTELLISENSE__
import std;	// #MSVC_BUG_STDCOMPAT
#else
import std.compat;
#endif
import hlsdk;

import UtlArray;

import CBase;
import Message;


// Weapon Classify

inline constexpr std::array ONEHANDED_IDX{ WEAPON_P228, WEAPON_FIVESEVEN, WEAPON_USP, WEAPON_GLOCK18, WEAPON_DEAGLE, };
inline constexpr std::array DUALWIELDING_IDX{ WEAPON_ELITE, };
inline constexpr std::array SHOTGUN_IDX{ WEAPON_XM1014, WEAPON_M3, };
inline constexpr std::array SMG_IDX{ WEAPON_MAC10, WEAPON_UMP45, WEAPON_MP5N, WEAPON_TMP, WEAPON_P90, };
inline constexpr std::array ASSAULT_IDX{ WEAPON_AUG, WEAPON_GALIL, WEAPON_FAMAS, WEAPON_M4A1, WEAPON_SG552, WEAPON_AK47, };
inline constexpr std::array SNIPER_IDX{ WEAPON_SCOUT, WEAPON_SG550, WEAPON_AWP, WEAPON_G3SG1, };
inline constexpr std::array LMG_IDX{ WEAPON_M249, };
inline constexpr std::array MELEE_IDX{ WEAPON_KNIFE, };
inline constexpr std::array THROWABLE_IDX{ WEAPON_HEGRENADE, WEAPON_SMOKEGRENADE, WEAPON_FLASHBANG, };
inline constexpr std::array EQUIPMENT_IDX{ WEAPON_C4, };

inline constexpr auto PISTOLS_IDX = UTIL_MergeArray(ONEHANDED_IDX, DUALWIELDING_IDX);
inline constexpr auto FULL_AUTO_IDX = UTIL_MergeArray(SMG_IDX, ASSAULT_IDX, LMG_IDX);

template <size_t N>
[[nodiscard]] consteval uint32_t UTIL_BuildBitMaskForWeaponId(std::array<WeaponIdType, N> const& arr) noexcept
{
	return [&]<size_t... I>(std::index_sequence<I...>) noexcept {
		return (0u | ... | (1u << arr[I]));
	}(std::make_index_sequence<N>{});
}

inline constexpr auto ONEHANDED_IDX_BITSET = UTIL_BuildBitMaskForWeaponId(ONEHANDED_IDX);
inline constexpr auto PISTOLS_IDX_BITSET = UTIL_BuildBitMaskForWeaponId(PISTOLS_IDX);
inline constexpr auto FULL_AUTO_IDX_BITSET = UTIL_BuildBitMaskForWeaponId(FULL_AUTO_IDX);

// Math Tools

[[nodiscard]] constexpr int UTIL_FindRightmostZeroBit(std::unsigned_integral auto n)
{
	if (n == std::numeric_limits<decltype(n)>::max()) // All bits are 1 (e.g., for int, 0xFFFFFFFF)
		return -1;	// Or handle as an error/special case

	// Isolate the rightmost 0 bit by finding the rightmost set bit of n + 1
	auto const n_plus_1 = n + 1;
	auto const rightmostZero = n_plus_1 & (~n_plus_1 + 1);	// use negate op for signed number.

	// Find the position (0-indexed)
	if (rightmostZero == 0) // If n was 0xFFFFFFFF, n+1 would be 0
		return -1; // No 0 bit found (or handle as special case)

	if consteval
	{
		// constexpr log2(), before C++26.
		constexpr auto fnLog2 =
			[](this auto&& self, std::unsigned_integral auto val) noexcept -> decltype(val) {
				return val > 1 ? (1 + self(val >> 1)) : 0;
			};

		return static_cast<int>(fnLog2(rightmostZero));
	}
	else
	{
		// This one could be faster if dedicated FPU is involed.
		return static_cast<int>(std::log2(rightmostZero));
	}
}

// Manager Interface

struct IItemSlotManager
{
	virtual int32_t OccupySlot(std::string_view szHud, int32_t iAmmoId, int32_t iAmmoMax, uint8_t bitsFlags = 0) noexcept = 0;
};

// Manager Template

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
	uint8_t m_iAllocatorHead{};

	CBasePlayer* m_pPlayer{};

	[[nodiscard]] bool AnyFreeSlot() const noexcept
	{
		auto const bitsResult = m_pPlayer->pev->weapons & T::SLOT_PROTOTYPES_BITMASK;
		return bitsResult != 0 && bitsResult != T::SLOT_PROTOTYPES_BITMASK;
	}

	[[nodiscard]] int32_t AllocSlot(this auto&& self, std::string_view szHud, int32_t iAmmoId, int32_t iAmmoMax, uint8_t bitsFlags = 0) noexcept
	{
		if ((self.m_pPlayer->pev->weapons & T::SLOT_PROTOTYPES_BITMASK) == T::SLOT_PROTOTYPES_BITMASK)
			return -1;	// All slots are currently held by the player.

		// Is this HUD somewhere already?
		auto const it = std::ranges::find(
			self.m_rgszSlotOccupied,
			szHud
		);

		if (it != self.m_rgszSlotOccupied.cend())
			return it - self.m_rgszSlotOccupied.cbegin();

		auto iCounter = 0u;
		for (++self.m_iAllocatorHead, self.m_iAllocatorHead %= T::SLOT_PROTOTYPES_IDX.size();
			(self.m_pPlayer->pev->weapons & (1u << T::SLOT_PROTOTYPES_IDX[self.m_iAllocatorHead])) != 0;
			++self.m_iAllocatorHead, self.m_iAllocatorHead %= T::SLOT_PROTOTYPES_IDX.size(), ++iCounter)
		{
			if (iCounter > (T::SLOT_PROTOTYPES_IDX.size() * 2))	[[unlikely]]	// Something must went wrong or our bitmask will filter it.
				return -1;	// No slot found.
		}

		auto const iId = std::to_underlying(T::SLOT_PROTOTYPES_IDX[self.m_iAllocatorHead]);

		gmsgWeaponList::Send(self.m_pPlayer->edict(),
			szHud.data(),
			(uint8_t)iAmmoId,
			(uint8_t)iAmmoMax,
			-1,		// Or the client will reject our message.
			-1,
			T::SLOT_ID,	// Slot ID, starting from 0
			self.m_iOrderInSlot++,
			iId,
			bitsFlags
		);

		// The client.dll rejects any number greater than 21.
		self.m_iOrderInSlot %= 21;

		if (self.m_iOrderInSlot < T::SLOT_ITEMS_COUNT + 1) [[unlikely]]
			self.m_iOrderInSlot = T::SLOT_ITEMS_COUNT + 1;

		// Record what is using this slot so we can free that later.
		self.m_rgszSlotOccupied[iId] = szHud;

		return iId;
	}
};

[[nodiscard]] constexpr
auto BuildInitSlotOccupied(std::span<WeaponIdType const> rgiOpenedSlots = {}) noexcept
	-> std::array<std::string_view, 31>
{
	std::array<std::string_view, 31> ret{
		"WEAPON_NONE",
		"WEAPON_P228",
		"WEAPON_NIL",
		"WEAPON_SCOUT",
		"WEAPON_HEGRENADE",
		"WEAPON_XM1014",
		"WEAPON_C4",
		"WEAPON_MAC10",
		"WEAPON_AUG",
		"WEAPON_SMOKEGRENADE",
		"WEAPON_ELITE",
		"WEAPON_FIVESEVEN",
		"WEAPON_UMP45",
		"WEAPON_SG550",
		"WEAPON_GALIL",
		"WEAPON_FAMAS",
		"WEAPON_USP",
		"WEAPON_GLOCK18",
		"WEAPON_AWP",
		"WEAPON_MP5N",
		"WEAPON_M249",
		"WEAPON_M3",
		"WEAPON_M4A1",
		"WEAPON_TMP",
		"WEAPON_G3SG1",
		"WEAPON_FLASHBANG",
		"WEAPON_DEAGLE",
		"WEAPON_SG552",
		"WEAPON_AK47",
		"WEAPON_KNIFE",
		"WEAPON_P90",
	};

	for (auto&& iId : rgiOpenedSlots)
		ret[iId] = "";

	return ret;
}


// Manager Classes

struct PistolSlotManager : ItemSlotManager<PistolSlotManager>
{
	static inline constexpr auto SLOT_ITEMS_COUNT = 6;
	static inline constexpr auto SLOT_ID = 1;
	static inline constexpr auto& SLOT_PROTOTYPES_IDX = PISTOLS_IDX;
	static inline constexpr auto SLOT_PROTOTYPES_BITMASK = PISTOLS_IDX_BITSET;

	decltype(BuildInitSlotOccupied()) m_rgszSlotOccupied = BuildInitSlotOccupied(SLOT_PROTOTYPES_IDX);

	using ItemSlotManager<PistolSlotManager>::AnyFreeSlot;
	using ItemSlotManager<PistolSlotManager>::AllocSlot;
};

inline constinit std::array<PistolSlotManager, 33> g_rgPlayerPistolMgrs{};

struct FullAutoSlotManager : ItemSlotManager<FullAutoSlotManager>
{
	static inline constexpr auto SLOT_ITEMS_COUNT = 6;
	static inline constexpr auto SLOT_ID = 0;
	static inline constexpr auto& SLOT_PROTOTYPES_IDX = FULL_AUTO_IDX;
	static inline constexpr auto SLOT_PROTOTYPES_BITMASK = FULL_AUTO_IDX_BITSET;

	decltype(BuildInitSlotOccupied()) m_rgszSlotOccupied = BuildInitSlotOccupied(SLOT_PROTOTYPES_IDX);

	using ItemSlotManager<FullAutoSlotManager>::AnyFreeSlot;
	using ItemSlotManager<FullAutoSlotManager>::AllocSlot;
};

inline constinit std::array<FullAutoSlotManager, 33> g_rgPlayerFullAutoMgrs{};

export [[nodiscard]]
auto PistolSlotMgr(CBasePlayer* pPlayer) noexcept -> PistolSlotManager*
{
	try
	{
		auto& mgr = g_rgPlayerPistolMgrs.at(pPlayer->entindex());
		mgr.m_pPlayer = pPlayer;

		return std::addressof(mgr);
	}
	catch (...)
	{
		return nullptr;
	}
}

export template <WeaponIdType PROTOTYPE_ID>
[[nodiscard]] auto GetBotSlotManager(CBasePlayer* pPlayer) noexcept
{
	try
	{
		if constexpr ((1u << PROTOTYPE_ID) & PistolSlotManager::SLOT_PROTOTYPES_BITMASK)
		{
			auto& mgr = g_rgPlayerPistolMgrs.at(pPlayer->entindex());
			mgr.m_pPlayer = pPlayer;

			return std::addressof(mgr);
		}
		else if constexpr ((1u << PROTOTYPE_ID) & FullAutoSlotManager::SLOT_PROTOTYPES_BITMASK)
		{
			auto& mgr = g_rgPlayerFullAutoMgrs.at(pPlayer->entindex());
			mgr.m_pPlayer = pPlayer;

			return std::addressof(mgr);
		}
		else
			static_assert(false, "Not supporting this weapon.");
	}
	catch (...)
	{
		return nullptr;
	}
}
