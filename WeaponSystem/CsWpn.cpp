#ifdef __INTELLISENSE__
#include <algorithm>
#include <ranges>
#endif

#include <assert.h>

import std;
import hlsdk;

import UtlRandom;
import UtlString;

import FileSystem;
import Message;
import Models;
import PlayerItem;
import Prefab;
import Query;
import Uranus;

using std::strcpy;
using std::strcmp;	// #MSVC_BUG_STDCOMPAT

using std::array;
using std::span;
using std::string;
using std::string_view;
using std::vector;

using namespace std::literals;

consteval float UTIL_WeaponTimeBase() { return 0.f; }

#define LOUD_GUN_VOLUME             1000
#define NORMAL_GUN_VOLUME           600
#define QUIET_GUN_VOLUME            200

#define BIG_EXPLOSION_VOLUME        2048
#define NORMAL_EXPLOSION_VOLUME     1024
#define SMALL_EXPLOSION_VOLUME      512

#define BRIGHT_GUN_FLASH            512
#define NORMAL_GUN_FLASH            256
#define DIM_GUN_FLASH               128

static auto GetAnimsFromKeywords(string_view szModel, span<string_view const> rgszKeywords, span<string_view const> rgszMustInc = {}, span<string_view const> rgszMustExc = {}) noexcept -> vector<seq_timing_t const*>
{
	// #UPDATE_AT_CPP26 transparant at
	auto& ModelInfo = gStudioInfo.find(szModel)->second;
	static auto const fnCaselessCmp = [](char lhs, char rhs) static noexcept
	{
		if (lhs >= 'A' && lhs <= 'Z')
			lhs ^= 0b0010'0000;
		if (rhs >= 'A' && rhs <= 'Z')
			rhs ^= 0b0010'0000;

		return lhs == rhs;
	};

	auto const fnFilter =
		[&](std::remove_cvref_t<decltype(ModelInfo)>::value_type const& pr) noexcept
		{
			// At least one of the keywords must presented.
			bool bContains = false;
			for (auto&& szKeyword : rgszKeywords)
			{
				if (std::ranges::contains_subrange(pr.first, szKeyword, fnCaselessCmp))
				{
					bContains = true;
					break;
				}
			}

			if (!bContains)
				return false;

			// All of the blacklist must not be there.
			for (auto&& szBad : rgszMustExc)
			{
				if (std::ranges::contains_subrange(pr.first, szBad, fnCaselessCmp))
					return false;
			}

			// At least one of the prereq must be there.
			// Or the list is empty.
			for (auto&& szReq : rgszMustInc)
			{
				if (!std::ranges::contains_subrange(pr.first, szReq, fnCaselessCmp))
					return false;
			}

			return true;
		};

	return
		ModelInfo
		| std::views::filter(fnFilter)
		| std::views::transform([](auto& pr) static noexcept { return std::addressof(pr.second); })
		| std::ranges::to<std::vector>();
}

template <typename T>
struct CBasePistol : CPrefabWeapon
{
	uint16_t m_usFireEv{};

	qboolean UseDecrement() noexcept override { return true; }
	int iItemSlot() noexcept override { return T::DAT_SLOT + 1; }
	float GetMaxSpeed() noexcept override { return T::DAT_MAX_SPEED; }

	void Spawn() noexcept override
	{
		Precache();

		m_iId = T::PROTOTYPE_ID;
		g_engfuncs.pfnSetModel(edict(), T::MODEL_W);

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
			m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

		m_iDefaultAmmo = T::DAT_MAX_CLIP;
		m_fMaxSpeed = T::DAT_MAX_SPEED;	// From deagle
		m_flAccuracy = T::DAT_ACCY_INIT;

		if constexpr (requires { T::m_bBurstFire; })
		{
			static_assert(std::is_base_of_v<CBasePistol, T>);
			auto const CRTP = static_cast<T*>(this);

			CRTP->m_bBurstFire = false;
			m_iGlock18ShotsFired = 0;
			m_flGlock18Shoot = 0;
		}

		// Get ready to fall down
		FallInit();

		// extend
		__super::Spawn();
	}

	void Precache() noexcept override
	{
		g_engfuncs.pfnPrecacheModel(T::MODEL_V);
		GoldSrc::CacheStudioModelInfo(T::MODEL_V);
		g_engfuncs.pfnPrecacheModel(T::MODEL_W);
		g_engfuncs.pfnPrecacheModel(T::MODEL_P);

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			g_engfuncs.pfnPrecacheModel(T::MODEL_V_SHIELD);
			GoldSrc::CacheStudioModelInfo(T::MODEL_V_SHIELD);
			g_engfuncs.pfnPrecacheModel(T::MODEL_P_SHIELD);
		}

		for (auto&& file : T::SOUND_ALL)
			g_engfuncs.pfnPrecacheSound(file);

		m_iShellId = /*m_iShell =*/ g_engfuncs.pfnPrecacheModel(T::MODEL_SHELL);
		m_usFireEv = g_engfuncs.pfnPrecacheEvent(1, T::EV_FIRE);
	}

	qboolean GetItemInfo(ItemInfo* p) noexcept override
	{
		if constexpr (requires { T::FLAG_NO_ITEM_INFO; })
		{
			return false;
		}
		else
		{
			p->pszName = STRING(pev->classname);
			p->pszAmmo1 = T::DAT_AMMO_NAME;
			p->iMaxAmmo1 = T::DAT_AMMO_MAX;
			p->pszAmmo2 = nullptr;
			p->iMaxAmmo2 = -1;
			p->iMaxClip = T::DAT_MAX_CLIP;
			p->iSlot = T::DAT_SLOT;
			p->iPosition = T::DAT_SLOT_POS;
			p->iId = m_iId = T::PROTOTYPE_ID;
			p->iFlags = T::DAT_ITEM_FLAGS;
			p->iWeight = T::DAT_ITEM_WEIGHT;

			return true;
		}
	}

	qboolean Deploy() noexcept override
	{
		if constexpr (requires { T::m_bBurstFire; })
		{
			static_assert(std::is_base_of_v<CBasePistol, T>);
			auto const CRTP = static_cast<T*>(this);

			CRTP->m_bBurstFire = false;
			m_iGlock18ShotsFired = 0;
			m_flGlock18Shoot = 0;
		}

		m_flAccuracy = T::DAT_ACCY_INIT;
		m_fMaxSpeed = T::DAT_MAX_SPEED;

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			m_pPlayer->m_bShieldDrawn = false;
			m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;
		}

		if constexpr (requires { T::FLAG_DUAL_WIELDING; })
		{
			if (!(m_iClip & 1))
				m_iWeaponState |= WPNSTATE_ELITE_LEFT;
		}

		// See if shielded first
		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			static auto const ANIM_SHIELD_DRAW{
				GetAnimsFromKeywords(T::MODEL_V_SHIELD, T::ANIM_KW_DRAW)
			};

			assert(!ANIM_SHIELD_DRAW.empty());

			if (m_pPlayer->HasShield())
			{
				m_iWeaponState &= ~WPNSTATE_GLOCK18_BURST_MODE;
				m_iWeaponState &= ~WPNSTATE_USP_SILENCED;

				return DefaultDeploy(
					T::MODEL_V_SHIELD,
					T::MODEL_P_SHIELD,
					UTIL_GetRandomOne(ANIM_SHIELD_DRAW)->m_iSeqIdx,
					"shieldgun",
					UseDecrement() != false
				);
			}
		}

		// USP, unsilenced
		if constexpr (requires { T::FLAG_SECATK_SILENCER; })
		{
			static auto const ANIM_UNSIL_DRAW{
				GetAnimsFromKeywords(T::MODEL_V, T::ANIM_KW_DRAW, T::ANIM_KWEXC)
			};
			assert(!ANIM_UNSIL_DRAW.empty());

			if (!(m_iWeaponState & WPNSTATE_USP_SILENCED))
			{
				return DefaultDeploy(
					T::MODEL_V,
					T::MODEL_P,
					UTIL_GetRandomOne(ANIM_UNSIL_DRAW)->m_iSeqIdx,
					T::ANIM_3RD_PERSON,
					UseDecrement() != false
				);
			}
		}

		// Regular deploy. (and silenced status on USP)
		static auto const ANIM_DRAW{
			GetAnimsFromKeywords(T::MODEL_V, T::ANIM_KW_DRAW, {}, T::ANIM_KWEXC)
		};
		assert(!ANIM_DRAW.empty());

		return DefaultDeploy(
			T::MODEL_V,
			T::MODEL_P,
			UTIL_GetRandomOne(ANIM_DRAW)->m_iSeqIdx,
			T::ANIM_3RD_PERSON,
			UseDecrement() != false
		);
	}

	using CPrefabWeapon::SendWeaponAnim;	// Import the original one as well.
	// Addition: send wpn anim of the exact name. Won't random from a pool.
	inline auto SendWeaponAnim(string_view anim, bool bSkipLocal = false) const noexcept -> seq_timing_t const*
	{
		string_view szViewModel{ T::MODEL_V };

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			if (m_pPlayer->HasShield())
				szViewModel = T::MODEL_V_SHIELD;
		}

		try
		{
			// #UPDATE_AT_CPP26 transparant at
			auto const& AnimInfo =
				gStudioInfo.find(szViewModel)->second.find(anim)->second;

			m_pPlayer->pev->weaponanim = AnimInfo.m_iSeqIdx;

			if (bSkipLocal && g_engfuncs.pfnCanSkipPlayer(m_pPlayer->edict()))
				return std::addressof(AnimInfo);

			gmsgWeaponAnim::Send(m_pPlayer->edict(), AnimInfo.m_iSeqIdx, pev->body);
			return std::addressof(AnimInfo);
		}
		catch (...)
		{
			return nullptr;
		}

		std::unreachable();
	}

	// Addition
	bool ShieldSecondaryFire() noexcept
	{
		if (!m_pPlayer->HasShield())
			return false;

		seq_timing_t const* pAnimInfo{};

		if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
		{
			m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;
			pAnimInfo = SendWeaponAnim("shield_down", UseDecrement() != false);
			strcpy(m_pPlayer->m_szAnimExtention, "shieldgun");
			m_fMaxSpeed = T::DAT_MAX_SPEED;
			m_pPlayer->m_bShieldDrawn = false;
		}
		else
		{
			m_iWeaponState |= WPNSTATE_SHIELD_DRAWN;
			pAnimInfo = SendWeaponAnim("shield_up", UseDecrement() != false);
			strcpy(m_pPlayer->m_szAnimExtention, "shielded");
			m_fMaxSpeed = T::DAT_SHIELDED_SPEED;
			m_pPlayer->m_bShieldDrawn = true;
		}

		m_pPlayer->UpdateShieldCrosshair((m_iWeaponState & WPNSTATE_SHIELD_DRAWN) != WPNSTATE_SHIELD_DRAWN);
		m_pPlayer->ResetMaxSpeed();

		assert(pAnimInfo != nullptr);

		m_flNextSecondaryAttack = pAnimInfo->m_total_length;
		m_flNextPrimaryAttack = pAnimInfo->m_total_length;
		m_flTimeWeaponIdle = pAnimInfo->m_total_length + 0.1f;

		return true;
	}

	void SecondaryAttack() noexcept override
	{
		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			if (ShieldSecondaryFire())
				return;
		}

		if constexpr (requires { T::m_bBurstFire; })
		{
			if (m_iWeaponState & WPNSTATE_GLOCK18_BURST_MODE)
			{
				gmsgTextMsg::Send(m_pPlayer->edict(), HUD_PRINTCENTER, "#Switch_To_SemiAuto");
				m_iWeaponState &= ~WPNSTATE_GLOCK18_BURST_MODE;
			}
			else
			{
				gmsgTextMsg::Send(m_pPlayer->edict(), HUD_PRINTCENTER, "#Switch_To_BurstFire");
				m_iWeaponState |= WPNSTATE_GLOCK18_BURST_MODE;
			}

			m_flNextSecondaryAttack = 0.3f;
		}
		else if constexpr (requires { T::FLAG_SECATK_SILENCER; })
		{
			seq_timing_t const* pAnimInfo{};

			if (m_iWeaponState & WPNSTATE_USP_SILENCED)
			{
				m_iWeaponState &= ~WPNSTATE_USP_SILENCED;
				pAnimInfo = SendWeaponAnim("detach_silencer", UseDecrement() != false);
			}
			else
			{
				m_iWeaponState |= WPNSTATE_USP_SILENCED;
				pAnimInfo = SendWeaponAnim("add_silencer", UseDecrement() != false);
			}

			assert(pAnimInfo != nullptr);

			m_flNextPrimaryAttack
				= m_flNextSecondaryAttack
				= m_flTimeWeaponIdle = pAnimInfo->m_total_length;
		}
	}

	void PrimaryAttack() noexcept override
	{
		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			// LUNA: You can't shoot when shields up!
			if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
				return;
		}

		if constexpr (requires { T::m_bBurstFire; })
		{
			if (m_iWeaponState & WPNSTATE_GLOCK18_BURST_MODE)
				m_iGlock18ShotsFired = 0;
			else if (++m_iShotsFired > 1)
				return;
		}
		else
		{
			if (++m_iShotsFired > 1)
				return;
		}

		if (m_flLastFire)
		{
			m_flAccuracy = std::ranges::clamp(
				// Mark the time of this shot and determine the accuracy modifier based on the last shot fired...
				T::EXPR_ACCY(this),
				T::DAT_ACCY_RANGE.first,
				T::DAT_ACCY_RANGE.second
			);
		}

		m_flLastFire = gpGlobals->time;

		if (m_iClip <= 0)
		{
			if (m_fFireOnEmpty)
			{
				PlayEmptySound();
				m_flNextPrimaryAttack = 0.2f;
			}

			// #TODO_ZBOTS
			//if (TheBots)
			//{
			//	TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
			//}

			return;
		}

		--m_iClip;

		[[maybe_unused]] bool const bShootingLeft{ !!(m_iWeaponState & WPNSTATE_ELITE_LEFT) };
		if constexpr (requires { T::FLAG_DUAL_WIELDING; })
		{
			m_pPlayer->SetAnimation(bShootingLeft ? PLAYER_ATTACK1 : PLAYER_ATTACK2);
			m_iWeaponState ^= WPNSTATE_ELITE_LEFT;	// Flip the bit.
		}
		else
			m_pPlayer->SetAnimation(PLAYER_ATTACK1);

		m_pPlayer->m_iWeaponVolume = T::DAT_FIRE_VOLUME;
		m_pPlayer->m_iWeaponFlash = T::DAT_FIRE_FLASH;

		if constexpr (requires { T::FLAG_SECATK_SILENCER; })
		{
			if (!(m_iWeaponState & WPNSTATE_USP_SILENCED))
				m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
		}
		else
		{
			m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
		}

		[[maybe_unused]]
		auto const [vecFwd, vecRight, vecUp]
			= (m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle).AngleVectors();

		auto const vecDir = m_pPlayer->FireBullets3(
			m_pPlayer->GetGunPosition(),
			vecFwd,
			T::EXPR_SPREAD(this),
			T::DAT_EFF_SHOT_DIST,
			T::DAT_PENETRATION,
			T::DAT_BULLET_TYPE,
			std::lroundf(T::EXPR_DAMAGE(this)),
			T::DAT_RANGE_MODIFIER,
			m_pPlayer->pev,
			requires { T::FLAG_IS_PISTOL; },
			m_pPlayer->random_seed
		);

		Vector vecDummy{};
		g_engfuncs.pfnPlaybackEvent(
			FEV_NOTHOST,
			m_pPlayer->edict(),
			m_usFireEv,
			0,
			vecDummy, vecDummy,
			vecDir.x, vecDir.y,
			std::lroundf(m_pPlayer->pev->punchangle.pitch * 100.f), std::lroundf(m_pPlayer->pev->punchangle.yaw * 100.f),
			m_iClip == 0, m_iWeaponState & WPNSTATE_USP_SILENCED
		);

		m_flNextPrimaryAttack = m_flNextSecondaryAttack = T::DAT_FIRE_INTERVAL;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.0f;	// Change it to anim time of shooting anim.

		/*
		if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
		{
			// HEV suit - indicate out of ammo condition
			m_pPlayer->SetSuitUpdate("!HEV_AMO0", SUIT_SENTENCE, SUIT_REPEAT_OK);
		}
		*/

		if constexpr (requires { T::m_bBurstFire; })
		{
			if (m_iWeaponState & WPNSTATE_GLOCK18_BURST_MODE)
			{
				// Fire off the next two rounds
				m_iGlock18ShotsFired++;
				m_flGlock18Shoot = gpGlobals->time + 0.1f;
			}
		}

		T::EFFC_RECOIL(this);
	}

	void Reload() noexcept override
	{
		if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
			return;

		seq_timing_t const* pAnimInfo{ nullptr };

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			static auto const ANIM_SHIELD_RELOAD{
				GetAnimsFromKeywords(T::MODEL_V_SHIELD, T::ANIM_KW_RELOAD)
			};

			if (pAnimInfo == nullptr && m_pPlayer->HasShield())
			{
				assert(!ANIM_SHIELD_RELOAD.empty());
				pAnimInfo = UTIL_GetRandomOne(ANIM_SHIELD_RELOAD);
			}
		}

		// USP, unsil mode
		if constexpr (requires { T::FLAG_SECATK_SILENCER; })
		{
			static auto const ANIM_UNSIL_RELOAD{
				GetAnimsFromKeywords(T::MODEL_V, T::ANIM_KW_RELOAD, T::ANIM_KWEXC)
			};

			if (pAnimInfo == nullptr && !(m_iWeaponState & WPNSTATE_USP_SILENCED))
			{
				assert(!ANIM_UNSIL_RELOAD.empty());
				pAnimInfo = UTIL_GetRandomOne(ANIM_UNSIL_RELOAD);
			}
		}

		if (pAnimInfo == nullptr)
		{
			static auto const ANIM_RELOAD{
				GetAnimsFromKeywords(T::MODEL_V, T::ANIM_KW_RELOAD, {}, T::ANIM_KWEXC)
			};

			assert(!ANIM_RELOAD.empty());
			pAnimInfo = UTIL_GetRandomOne(ANIM_RELOAD);
		}

		if (DefaultReload(T::DAT_MAX_CLIP, pAnimInfo->m_iSeqIdx, pAnimInfo->m_total_length))
		{
			m_pPlayer->SetAnimation(PLAYER_RELOAD);
			m_flAccuracy = T::DAT_ACCY_INIT;
		}
	}

	void WeaponIdle() noexcept override
	{
		ResetEmptySound();
		m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);

		if (m_flTimeWeaponIdle > 0)
			return;

		// Dont sent idle anim to make the slide back - the last frame of shoot_last!
		if (m_iClip <= 0)
			return;

		seq_timing_t const* pAnimInfo{ nullptr };

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			static constexpr std::array SHIELD_KWEXC{ "shield"sv };
			static auto const ANIM_SHIELD_IDLE{
				GetAnimsFromKeywords(T::MODEL_V_SHIELD, T::ANIM_KW_IDLE, {}, SHIELD_KWEXC)	// Shield_Idle must be excluded.
			};

			if (pAnimInfo == nullptr && m_pPlayer->HasShield())
			{
				assert(!ANIM_SHIELD_IDLE.empty());

				if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
					pAnimInfo = std::addressof(gStudioInfo.find(T::MODEL_V_SHIELD)->second.find("shield_idle")->second);
				else
					pAnimInfo = UTIL_GetRandomOne(ANIM_SHIELD_IDLE);
			}
		}

		// USP, unsil mode
		if constexpr (requires { T::FLAG_SECATK_SILENCER; })
		{
			static auto const ANIM_UNSIL_IDLE{
				GetAnimsFromKeywords(T::MODEL_V, T::ANIM_KW_IDLE, T::ANIM_KWEXC)
			};

			if (pAnimInfo == nullptr && !(m_iWeaponState & WPNSTATE_USP_SILENCED))
			{
				assert(!ANIM_UNSIL_IDLE.empty());
				pAnimInfo = UTIL_GetRandomOne(ANIM_UNSIL_IDLE);
			}
		}

		if (pAnimInfo == nullptr)
		{
			static auto const ANIM_IDLE{
				GetAnimsFromKeywords(T::MODEL_V, T::ANIM_KW_IDLE, {}, T::ANIM_KWEXC)
			};

			assert(!ANIM_IDLE.empty());
			pAnimInfo = UTIL_GetRandomOne(ANIM_IDLE);
		}

		assert(pAnimInfo != nullptr);

		SendWeaponAnim(pAnimInfo->m_iSeqIdx, false);
		m_flTimeWeaponIdle = std::max(5.f, pAnimInfo->m_total_length);
	}
};

struct G18C_VER2 : CBasePistol<G18C_VER2>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_GLOCK18;

	static inline constexpr char MODEL_V[] = "models/v_glock18.mdl";
	static inline constexpr char MODEL_V_SHIELD[] = "models/shield/v_shield_glock18.mdl";
	static inline constexpr char MODEL_W[] = "models/w_glock18.mdl";
	static inline constexpr char MODEL_P[] = "models/p_glock18.mdl";
	static inline constexpr char MODEL_P_SHIELD[] = "models/shield/p_shield_glock18.mdl";
	static inline constexpr char MODEL_SHELL[] = "models/pshell.mdl";

	static inline constexpr char ANIM_3RD_PERSON[] = "onehanded";
	static inline constexpr std::array ANIM_KW_IDLE{ "idle"sv, };
	static inline constexpr std::array ANIM_KW_SHOOT{ "shoot"sv, "fire"sv, };
	static inline constexpr std::array ANIM_KW_RELOAD{ "reload"sv, };
	static inline constexpr std::array ANIM_KW_DRAW{ "draw"sv, "deploy"sv, };
	static inline constexpr std::array ANIM_KWEXC{ "unsil"sv };	// Served as 'unsilenced' identifier on USP.
	//static inline constexpr std::array ANIM_KWINC{""};

	static inline constexpr std::array SOUND_ALL
	{
		"weapons/glock18-1.wav",
		"weapons/glock18-2.wav",
		"weapons/clipout1.wav",
		"weapons/clipin1.wav",
		"weapons/sliderelease1.wav",
		"weapons/slideback1.wav",
		"weapons/357_cock1.wav",
		"weapons/de_clipin.wav",
		"weapons/de_clipout.wav",
	};

	static inline constexpr char EV_FIRE[] = "events/glock18.sc";

	static inline constexpr auto DAT_ACCY_INIT = 0.9f;
	static inline constexpr auto DAT_ACCY_RANGE = std::pair{ 0.6f, 0.9f };
	static inline constexpr auto DAT_MAX_CLIP = 20;
	static inline constexpr auto DAT_MAX_SPEED = 250.f;
	static inline constexpr auto DAT_SHIELDED_SPEED = 180.f;
	static inline constexpr char DAT_AMMO_NAME[] = "9mm";
	static inline constexpr auto DAT_AMMO_MAX = 120;
	static inline constexpr auto DAT_SLOT = 1;
	static inline constexpr auto DAT_SLOT_POS = 2;
	static inline constexpr auto DAT_ITEM_FLAGS = 0;
	static inline constexpr auto DAT_ITEM_WEIGHT = 5;
	static inline constexpr auto DAT_FIRE_VOLUME = NORMAL_GUN_VOLUME;
	static inline constexpr auto DAT_FIRE_FLASH = NORMAL_GUN_FLASH;
	static inline constexpr auto DAT_EFF_SHOT_DIST = 4096.f;
	static inline constexpr auto DAT_PENETRATION = 1;
	static inline constexpr auto DAT_BULLET_TYPE = BULLET_PLAYER_9MM;
	static inline constexpr auto DAT_RANGE_MODIFIER = 0.75f;
	static inline constexpr auto DAT_FIRE_INTERVAL = 0.2f - 0.05f;

	static inline constexpr auto EXPR_DAMAGE = [](CBasePlayerWeapon const* pWeapon) static noexcept {
		return 25.f;
	};
	static inline constexpr auto EXPR_ACCY = [](CBasePlayerWeapon const* pWeapon) static noexcept {
		return pWeapon->m_flAccuracy - (0.325f - (gpGlobals->time - pWeapon->m_flLastFire)) * 0.275f;
	};
	static inline constexpr auto EXPR_SPREAD = [](CBasePlayerWeapon const* pWeapon) static noexcept {
		if (pWeapon->m_iWeaponState & WPNSTATE_GLOCK18_BURST_MODE)
		{
			if (!(pWeapon->m_pPlayer->pev->flags & FL_ONGROUND))
				return 1.2f * (1.f - pWeapon->m_flAccuracy);
			else if (pWeapon->m_pPlayer->pev->velocity.LengthSquared2D() > 0)
				return 0.185f * (1.f - pWeapon->m_flAccuracy);
			else if (pWeapon->m_pPlayer->pev->flags & FL_DUCKING)
				return 0.095f * (1.f - pWeapon->m_flAccuracy);
			else
				return 0.3f * (1.f - pWeapon->m_flAccuracy);
		}
		else
		{
			if (!(pWeapon->m_pPlayer->pev->flags & FL_ONGROUND))
				return 1.f * (1.f - pWeapon->m_flAccuracy);
			else if (pWeapon->m_pPlayer->pev->velocity.LengthSquared2D() > 0)
				return 0.165f * (1.f - pWeapon->m_flAccuracy);
			else if (pWeapon->m_pPlayer->pev->flags & FL_DUCKING)
				return 0.075f * (1.f - pWeapon->m_flAccuracy);
			else
				return 0.1f * (1.f - pWeapon->m_flAccuracy);
		}
	};
	static inline constexpr auto EFFC_RECOIL = [](CBasePlayerWeapon const* pWeapon) static noexcept {
		//pWeapon->m_pPlayer->pev->punchangle.pitch -= 2;
		// G18 in CS doesn't have any recoil at all.
	};

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
	//static inline constexpr auto FLAG_NO_ITEM_INFO = true;
	//static inline constexpr auto FLAG_DUAL_WIELDING = true;	// Emulate vanilla elites.
	//static inline constexpr auto FLAG_SECATK_SILENCER = true;	// Emulate vanilla USP
	bool m_bBurstFire{};	// A flag to emulate vanilla g18
};

template void LINK_ENTITY_TO_CLASS<G18C_VER2>(entvars_t* pev) noexcept;

struct USP2 : CBasePistol<USP2>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_USP;

	static inline constexpr char MODEL_V[] = "models/v_usp.mdl";
	static inline constexpr char MODEL_V_SHIELD[] = "models/shield/v_shield_usp.mdl";
	static inline constexpr char MODEL_W[] = "models/w_usp.mdl";
	static inline constexpr char MODEL_P[] = "models/p_usp.mdl";
	static inline constexpr char MODEL_P_SHIELD[] = "models/shield/p_shield_usp.mdl";
	static inline constexpr char MODEL_SHELL[] = "models/pshell.mdl";

	static inline constexpr char ANIM_3RD_PERSON[] = "onehanded";
	static inline constexpr std::array ANIM_KW_IDLE{ "idle"sv, };
	static inline constexpr std::array ANIM_KW_SHOOT{ "shoot"sv, "fire"sv, };
	static inline constexpr std::array ANIM_KW_RELOAD{ "reload"sv, };
	static inline constexpr std::array ANIM_KW_DRAW{ "draw"sv, "deploy"sv, };
	static inline constexpr std::array ANIM_KWEXC{ "unsil"sv };	// Served as 'unsilenced' identifier on USP.
	//static inline constexpr std::array ANIM_KWINC{""};

	static inline constexpr std::array SOUND_ALL
	{
		"weapons/usp1.wav",
		"weapons/usp2.wav",
		"weapons/usp_unsil-1.wav",
		"weapons/usp_clipout.wav",
		"weapons/usp_clipin.wav",
		"weapons/usp_silencer_on.wav",
		"weapons/usp_silencer_off.wav",
		"weapons/usp_sliderelease.wav",
		"weapons/usp_slideback.wav",
	};

	static inline constexpr char EV_FIRE[] = "events/usp.sc";

	static inline constexpr auto DAT_ACCY_INIT = 0.92f;
	static inline constexpr auto DAT_ACCY_RANGE = std::pair{ 0.6f, 0.92f };
	static inline constexpr auto DAT_MAX_CLIP = 12;
	static inline constexpr auto DAT_MAX_SPEED = 250.f;
	static inline constexpr auto DAT_SHIELDED_SPEED = 180.f;
	static inline constexpr char DAT_AMMO_NAME[] = "45acp";
	static inline constexpr auto DAT_AMMO_MAX = 100;
	static inline constexpr auto DAT_SLOT = 1;
	static inline constexpr auto DAT_SLOT_POS = 4;
	static inline constexpr auto DAT_ITEM_FLAGS = 0;
	static inline constexpr auto DAT_ITEM_WEIGHT = 5;
	static inline constexpr auto DAT_FIRE_VOLUME = BIG_EXPLOSION_VOLUME;
	static inline constexpr auto DAT_FIRE_FLASH = DIM_GUN_FLASH;
	static inline constexpr auto DAT_EFF_SHOT_DIST = 4096.f;
	static inline constexpr auto DAT_PENETRATION = 1;
	static inline constexpr auto DAT_BULLET_TYPE = BULLET_PLAYER_45ACP;
	static inline constexpr auto DAT_RANGE_MODIFIER = 0.79f;
	static inline constexpr auto DAT_FIRE_INTERVAL = 0.225f - 0.075f;

	static inline constexpr auto EXPR_DAMAGE = [](CBasePlayerWeapon const* pWeapon) static noexcept {
		return (pWeapon->m_iWeaponState & WPNSTATE_USP_SILENCED) ? 30.f : 34.f;
	};
	static inline constexpr auto EXPR_ACCY = [](CBasePlayerWeapon const* pWeapon) static noexcept {
		return pWeapon->m_flAccuracy - (0.3f - (gpGlobals->time - pWeapon->m_flLastFire)) * 0.275f;
	};
	static inline constexpr auto EXPR_SPREAD = [](CBasePlayerWeapon const* pWeapon) static noexcept {
		if (pWeapon->m_iWeaponState & WPNSTATE_USP_SILENCED)
		{
			if (!(pWeapon->m_pPlayer->pev->flags & FL_ONGROUND))
				return 1.3f * (1.f - pWeapon->m_flAccuracy);
			else if (pWeapon->m_pPlayer->pev->velocity.Length2D() > 0)
				return 0.25f * (1.f - pWeapon->m_flAccuracy);
			else if (pWeapon->m_pPlayer->pev->flags & FL_DUCKING)
				return 0.125f * (1.f - pWeapon->m_flAccuracy);
			else
				return 0.15f * (1.f - pWeapon->m_flAccuracy);
		}
		else
		{
			if (!(pWeapon->m_pPlayer->pev->flags & FL_ONGROUND))
				return 1.2f * (1.f - pWeapon->m_flAccuracy);
			else if (pWeapon->m_pPlayer->pev->velocity.Length2D() > 0)
				return 0.225f * (1.f - pWeapon->m_flAccuracy);
			else if (pWeapon->m_pPlayer->pev->flags & FL_DUCKING)
				return 0.08f * (1.f - pWeapon->m_flAccuracy);
			else
				return 0.1f * (1.f - pWeapon->m_flAccuracy);
		}
	};
	static inline constexpr auto EFFC_RECOIL = [](CBasePlayerWeapon const* pWeapon) static noexcept {
		pWeapon->m_pPlayer->pev->punchangle.pitch -= 2;
	};

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
	//static inline constexpr auto FLAG_NO_ITEM_INFO = true;
	//static inline constexpr auto FLAG_DUAL_WIELDING = true;	// Emulate vanilla elites.
	static inline constexpr auto FLAG_SECATK_SILENCER = true;	// Emulate vanilla USP
	//bool m_bBurstFire{};										// A flag to emulate vanilla g18
};

template void LINK_ENTITY_TO_CLASS<USP2>(entvars_t* pev) noexcept;
