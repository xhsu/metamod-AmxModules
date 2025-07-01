
#include <assert.h>	// #UPDATE_AT_CPP26 contract assert

#ifdef __INTELLISENSE__
import std;
#else
import std.compat;
#endif
import hlsdk;

import UtlArray;
import UtlRandom;
import UtlString;

import CBase;
import FileSystem;
import GameRules;
import Message;
import Models;
import PlayerItem;
import Prefab;
import Query;
import Task;
import Uranus;
import ZBot;

using std::strcpy;
using std::strcmp;	// #MSVC_BUG_STDCOMPAT

using std::array;
using std::span;
using std::string;
using std::string_view;
using std::vector;

using namespace std::literals;

enum EWeaponTaskFlags2 : std::uint64_t
{
	TASK_KEY_MONITOR_LMB = (1ull << 0),
	TASK_KEY_MONITOR_RMB = (1ull << 1),
	TASK_KEY_MONITOR_R = (1ull << 2),

	TASK_ALL_MONITORS = 0b1111'1111,

	TASK_BEHAVIOR_DRAW = (1ull << 8),
	TASK_BEHAVIOR_RELOAD = (1ull << 9),
	TASK_BEHAVIOR_SHOOT = (1ull << 11),
	TASK_BEHAVIOR_SPECIAL = (1ull << 12),
	TASK_BEHAVIOR_HOLSTER = (1ull << 13),

	TASK_ALL_BEHAVIORS = TASK_ALL_MONITORS << 8,

	TASK_ANIMATION = (1ull << 16),	// Always exclusive.

	TASK_ALL_WEAKS = TASK_ALL_BEHAVIORS << 8,

	TASK_MATERIALIZED_UNSET_OWNER = (1ull << 24),

	TASK_ALL_MATERIALIZED = TASK_ALL_WEAKS << 8,
};

extern edict_t* CreateGunSmoke(CBasePlayer* pPlayer, bool bIsPistol, bool bShootingLeft) noexcept;

extern Vector2D CS_FireBullets3(
	CBasePlayer* pAttacker, CBasePlayerItem* pInflictor,
	Vector const& vecSrcOfs, float flSpread, float flDistance,
	int iPenetration, int iBulletType, float flDamage, float flRangeModifier) noexcept;

[[nodiscard]] static auto GetAnimsFromKeywords(
	string_view szModel, span<string_view const> rgszKeywords,
	span<span<string_view const> const> rgrgszMustInc = {}, span<string_view const> rgszMustExc = {}) noexcept -> vector<seq_timing_t const*>
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

			// At least one keyword from each row must be presented.
			// ['word1' (OR) 'word2']
			// (AND)
			// ['word3']
			for (auto&& rgszMustInc : rgrgszMustInc)
			{
				// At least one of the prereq must be there.
				// Or the list is empty.
				bContains = rgszMustInc.empty();
				for (auto&& szReq : rgszMustInc)
				{
					if (std::ranges::contains_subrange(pr.first, szReq, fnCaselessCmp))
					{
						bContains = true;
						break;
					}
				}

				if (!bContains)
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

[[nodiscard]] static vector<string> CollectSounds(string_view szPrefix) noexcept
{
	vector<string> rgszSoundPaths{};

	for (int i = 0; i < 128; ++i)
	{
		auto const szPath = std::format("sound/{}{}.wav", szPrefix, i);

		if (FileSystem::m_pObject->FileExists(szPath.c_str()))
			rgszSoundPaths.emplace_back(std::format("{}{}.wav", szPrefix, i));
	}

	return rgszSoundPaths;
}

template <typename CWeapon, typename AnimDat>
struct CAnimationGroup final
{
	[[nodiscard]] static constexpr auto GetGenericExclusions() noexcept -> span<string_view const>
	{
		if constexpr (requires { AnimDat::EXCLUSION; })
			return AnimDat::EXCLUSION;
		else
			return {};
	}
	[[nodiscard]] static constexpr auto GetGenericInclusions() noexcept -> span<string_view const>
	{
		if constexpr (requires { AnimDat::INCLUSION; })
			return AnimDat::INCLUSION;
		else
			return {};
	}
	[[nodiscard]] static constexpr auto GetUnsilKeywords() noexcept -> span<string_view const>
	{
		if constexpr (requires { AnimDat::KEYWORD_UNSIL; })
			return AnimDat::KEYWORD_UNSIL;
		else
			return {};
	}
	[[nodiscard]] static constexpr auto GetShieldKeywords() noexcept -> span<string_view const>
	{
		if constexpr (requires { AnimDat::KEYWORD_SHIELD; })
			return AnimDat::KEYWORD_SHIELD;
		else
			return {};
	}

#ifdef _DEBUG
	static inline vector<seq_timing_t const*> const* m_pRegulars{};
	static inline vector<seq_timing_t const*> const* m_pUnsil{};
	static inline vector<seq_timing_t const*> const* m_pShield{};
#endif

	static auto operator()(CBasePlayerWeapon* pWeapon) noexcept -> seq_timing_t const*
	{
		static auto const REGULAR{
			GetAnimsFromKeywords(CWeapon::MODEL_V, AnimDat::KEYWORD, std::array{ GetGenericInclusions(), }, GetGenericExclusions())
		};
		assert(!REGULAR.empty()); m_pRegulars = &REGULAR;
		auto pResult{ UTIL_GetRandomOne(REGULAR) };

		// Unsil
		if constexpr (requires { CWeapon::FLAG_SECATK_SILENCER; })
		{
			static const auto UNSIL_INC = vector{ GetGenericInclusions(), GetUnsilKeywords(), };
			static const auto UNSIL_EXC = UTIL_ArraySetDiff(GetGenericExclusions(), GetUnsilKeywords());
			static auto const ANIM_UNSIL{
				GetAnimsFromKeywords(CWeapon::MODEL_V, AnimDat::KEYWORD, UNSIL_INC, UNSIL_EXC)
			};
			assert(!ANIM_UNSIL.empty()); m_pUnsil = &ANIM_UNSIL;

			if (!(pWeapon->m_iWeaponState & WPNSTATE_USP_SILENCED))
				pResult = UTIL_GetRandomOne(ANIM_UNSIL);
		}
		// Shield, will override unsil.
		if constexpr (requires { CWeapon::FLAG_CAN_HAVE_SHIELD; })
		{
			static const auto SHIELD_INC = vector{ GetGenericInclusions(), GetShieldKeywords(), };
			static const auto SHIELD_EXC = UTIL_ArraySetDiff(GetGenericExclusions(), GetShieldKeywords());
			static auto const ANIM_SHIELD{
				GetAnimsFromKeywords(CWeapon::MODEL_V_SHIELD, AnimDat::KEYWORD, SHIELD_INC, SHIELD_EXC)
			};
			assert(!ANIM_SHIELD.empty()); m_pShield = &ANIM_SHIELD;

			if (pWeapon->m_pPlayer->HasShield())
				pResult = UTIL_GetRandomOne(ANIM_SHIELD);
		}

		// Overwrite Inspector
		if constexpr (requires { { std::invoke(AnimDat::Inspector, pWeapon, &pResult) } -> std::convertible_to<bool>; })
		{
			// Using std::invoke to support class member function pointer
			std::invoke(AnimDat::Inspector, pWeapon, &pResult);
		}

		assert(pResult != nullptr);
		return pResult;
	}
};

template <typename T>
struct CBasePistol : CPrefabWeapon
{
	uint16_t m_usFireEv{}, m_usFireEv2{};

	qboolean UseDecrement() noexcept override { return true; }
	int iItemSlot() noexcept override { return T::DAT_SLOT + 1; }
	void Think() noexcept final
	{
		pev->nextthink = 0.1f;
		if (!m_pPlayer)
			m_Scheduler.Think();
	}
	void ItemPostFrame() noexcept final { if (m_pPlayer) m_Scheduler.Think(); }

	void Spawn() noexcept override
	{
		Precache();

		m_iId = T::PROTOTYPE_ID;
		g_engfuncs.pfnSetModel(this->edict(), T::MODEL_W);

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
			m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

		m_iDefaultAmmo = T::DAT_MAX_CLIP;
		m_fMaxSpeed = T::DAT_MAX_SPEED;	// From deagle
		m_flAccuracy = T::DAT_ACCY_INIT;

		if constexpr (requires { T::m_bBurstFire; })
		{
			CRTP()->m_bBurstFire = false;
			m_iGlock18ShotsFired = 0;
			m_flGlock18Shoot = 0;
		}

		// Get ready to fall down
		this->FallInit();

		// extend
		__super::Spawn();

		m_Scheduler.Policy() = ESchedulerPolicy::UNORDERED;
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

		for (auto&& file : CRTP()->EXPR_FIRING_SND())
			g_engfuncs.pfnPrecacheSound(file.c_str());

		m_iShellId = /*m_iShell =*/ g_engfuncs.pfnPrecacheModel(T::MODEL_SHELL);

		if constexpr (requires { T::FLAG_DUAL_WIELDING; })
		{
			m_usFireEv = g_engfuncs.pfnPrecacheEvent(1, T::EV_FIRE_R);	// This goes first.
			m_usFireEv2 = g_engfuncs.pfnPrecacheEvent(1, T::EV_FIRE_L);
		}
		else
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
			p->iId = T::PROTOTYPE_ID;
			p->iFlags = T::DAT_ITEM_FLAGS;
			p->iWeight = T::DAT_ITEM_WEIGHT;

			return true;
		}
	}

	Task Task_Deploy(seq_timing_t const* pAnim) noexcept
	{
		if (!CanDeploy())
			co_return;

		m_pPlayer->TabulateAmmo();

		m_pPlayer->pev->viewmodel = MAKE_STRING(T::MODEL_V);
		m_pPlayer->pev->weaponmodel = MAKE_STRING(T::MODEL_P);

		model_name = m_pPlayer->pev->viewmodel;
		strcpy(m_pPlayer->m_szAnimExtention, T::ANIM_3RD_PERSON);
		m_Scheduler.Enroll(Task_WeaponAnim(pAnim), TASK_ANIMATION, true);

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			if (m_pPlayer->HasShield())
			{
				m_pPlayer->pev->viewmodel = MAKE_STRING(T::MODEL_V_SHIELD);
				m_pPlayer->pev->weaponmodel = MAKE_STRING(T::MODEL_P_SHIELD);

				strcpy(m_pPlayer->m_szAnimExtention, "shieldgun");
			}
		}

		m_pPlayer->m_flNextAttack = pAnim->m_total_length;
		m_flTimeWeaponIdle = pAnim->m_total_length;
		m_flLastFireTime = 0.0f;
		m_flDecreaseShotsFired = gpGlobals->time;

		m_pPlayer->m_iFOV = DEFAULT_FOV;
		m_pPlayer->pev->fov = DEFAULT_FOV;
		m_pPlayer->m_iLastZoom = DEFAULT_FOV;
		m_pPlayer->m_bResumeZoom = false;

		co_await m_pPlayer->m_flNextAttack;

		// Start ItemPostFrame and Idle here.
		m_Scheduler.Enroll(Task_ItemPostFrame(), TASK_ALL_MONITORS, true);
	}

	Task Task_ResumeZoom(float const AWAIT) const noexcept
	{
		m_pPlayer->m_bResumeZoom = true;

		co_await AWAIT;

		if (m_pPlayer->m_bResumeZoom)
		{
			m_pPlayer->m_iFOV = m_pPlayer->m_iLastZoom;
			m_pPlayer->pev->fov = (float)m_pPlayer->m_iFOV;

			if (m_pPlayer->m_iFOV == m_pPlayer->m_iLastZoom)
			{
				// return the fade level in zoom.
				m_pPlayer->m_bResumeZoom = false;
			}
		}
	}

	Task Task_EjectBrassLate(float const AWAIT) noexcept
	{
		m_pPlayer->m_flEjectBrass = AWAIT;

		co_await AWAIT;

		if (m_pPlayer->m_flEjectBrass != 0 && m_pPlayer->m_flEjectBrass <= gpGlobals->time)
		{
			m_pPlayer->m_flEjectBrass = 0;
			EjectBrassLate();
		}
	}

	Task Task_ItemPostFrame() noexcept
	{
		for (;; co_await TaskScheduler::NextFrame::Rank[0])
		{
			if (!(m_pPlayer->pev->button & IN_ATTACK))
			{
				m_flLastFireTime = 0;
			}

			// Allowing player to shield up during reload.
			if (m_pPlayer->HasShield())
			{
				if (m_fInReload && (m_pPlayer->pev->button & IN_ATTACK2))
				{
					SecondaryAttack();
					m_pPlayer->pev->button &= ~IN_ATTACK2;

					m_fInReload = false;
					m_Scheduler.Delist(TASK_BEHAVIOR_RELOAD);

					m_pPlayer->m_flNextAttack = 0;
				}
			}

			if (HasSecondaryAttack() && (m_pPlayer->pev->button & IN_ATTACK2)
				&& !m_Scheduler.Exist(TASK_ALL_BEHAVIORS)
				&& !m_pPlayer->m_bIsDefusing // In-line: I think it's fine to block secondary attack, when defusing. It's better then blocking speed resets in weapons.
				)
			{
				SecondaryAttack();
				m_pPlayer->pev->button &= ~IN_ATTACK2;
			}
			else if ((m_pPlayer->pev->button & IN_ATTACK)
				&& !m_Scheduler.Exist(TASK_ALL_BEHAVIORS))
			{
				if ((m_iClip == 0 && T::DAT_AMMO_NAME != nullptr)
					|| (T::DAT_MAX_CLIP == WEAPON_NOCLIP && !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]))
				{
					m_fFireOnEmpty = true;
				}

				m_pPlayer->TabulateAmmo();

				// Can't shoot during the freeze period
				// Always allow firing in single player
				if ((m_pPlayer->m_bCanShoot && g_pGameRules->IsMultiplayer() && !g_pGameRules->IsFreezePeriod() && !m_pPlayer->m_bIsDefusing)
					|| !g_pGameRules->IsMultiplayer())
				{
					// don't fire underwater
					if (m_pPlayer->pev->waterlevel == 3 && (T::DAT_ITEM_FLAGS & ITEM_FLAG_NOFIREUNDERWATER))
					{
						PlayEmptySound();

						m_flNextPrimaryAttack = 0.15f;
						m_Scheduler.Enroll(
							[]() static noexcept -> Task { co_await 0.15f; }(),
							TASK_BEHAVIOR_SHOOT,
							true
						);
					}
					else
					{
						PrimaryAttack();
					}
				}
			}
			else if ((m_pPlayer->pev->button & IN_RELOAD) && T::DAT_MAX_CLIP != WEAPON_NOCLIP
				&& !m_fInReload && !m_Scheduler.Exist(TASK_ALL_BEHAVIORS))
			{
				if (!(m_iWeaponState & WPNSTATE_SHIELD_DRAWN))
				{
					// reload when reload is pressed, or if no buttons are down and weapon is empty.
					Reload();
				}
			}

			// Idle
			else if (!(m_pPlayer->pev->button & (IN_ATTACK | IN_ATTACK2)))
			{
				// no fire buttons down

				// The following code prevents the player from tapping the firebutton repeatedly
				// to simulate full auto and retaining the single shot accuracy of single fire
				if (m_bDelayFire)
				{
					m_bDelayFire = false;

					if (m_iShotsFired > 15)
						m_iShotsFired = 15;

					m_flDecreaseShotsFired = gpGlobals->time + 0.4f;
				}

				m_fFireOnEmpty = false;

				// if it's a pistol then set the shots fired to 0 after the player releases a button
				if constexpr (requires { T::FLAG_IS_PISTOL; })
				{
					m_iShotsFired = 0;
				}
				else
				{
					if (m_iShotsFired > 0 && m_flDecreaseShotsFired < gpGlobals->time)
					{
						m_flDecreaseShotsFired = gpGlobals->time + 0.0225f;
						--m_iShotsFired;

						// Reset accuracy
						if (m_iShotsFired == 0)
							m_flAccuracy = T::DAT_ACCY_INIT;
					}
				}

				if (!IsUseable() && !m_Scheduler.Exist(TASK_ALL_BEHAVIORS))
				{
#if 0
					// weapon isn't useable, switch.
					if (!(T::DAT_ITEM_FLAGS & ITEM_FLAG_NOAUTOSWITCHEMPTY) && g_pGameRules->GetNextBestWeapon(m_pPlayer, this))
					{
						m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.3f;
						continue;
					}
#endif
				}
				else
				{
					if (!(m_iWeaponState & WPNSTATE_SHIELD_DRAWN))
					{
						// weapon is useable. Reload if empty and weapon has waited as long as it has to after firing
						if (!m_iClip && !(T::DAT_ITEM_FLAGS & ITEM_FLAG_NOAUTORELOAD) && !m_Scheduler.Exist(TASK_ALL_BEHAVIORS | TASK_ANIMATION))
						{
							Reload();
							continue;
						}
					}
				}

				if (!m_Scheduler.Exist(TASK_ANIMATION))
					WeaponIdle();
			}
		}
	}

	qboolean Deploy() noexcept override
	{
		if constexpr (requires { T::m_bBurstFire; })
		{
			CRTP()->m_bBurstFire = false;
			m_iGlock18ShotsFired = 0;
			m_flGlock18Shoot = 0;
		}

		m_flAccuracy = T::DAT_ACCY_INIT;
		m_fMaxSpeed = T::DAT_MAX_SPEED;

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			m_pPlayer->m_bShieldDrawn = false;
			m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

			if constexpr (requires { T::FLAG_SECATK_SILENCER; })
			{
				if (m_pPlayer->HasShield())
					m_iWeaponState &= ~WPNSTATE_USP_SILENCED;
			}
		}

		if constexpr (requires { T::FLAG_DUAL_WIELDING; })
		{
			if (!(m_iClip & 1))
				m_iWeaponState |= WPNSTATE_ELITE_LEFT;
		}

		static constexpr CAnimationGroup<T, typename T::AnimDat_Draw> AnimSelector{};

		m_Scheduler.Enroll(
			Task_Deploy(AnimSelector(this)),
			TASK_BEHAVIOR_DRAW | TASK_ANIMATION,
			true
		);
		return true;
	}

	using CPrefabWeapon::SendWeaponAnim;	// Import the original one as well.
	// Addition: send wpn anim of the exact name. Won't random from a pool.
	inline auto SendWeaponAnim(string_view anim, int iBody = 0, bool bSkipLocal = false) const noexcept -> seq_timing_t const*
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

			gmsgWeaponAnim::Send(m_pPlayer->edict(), AnimInfo.m_iSeqIdx, iBody);
			return std::addressof(AnimInfo);
		}
		catch (...)
		{
			return nullptr;
		}

		std::unreachable();
	}

	Task Task_WeaponAnim(string_view anim, int iBody = 0, bool bSkipLocal = false) const noexcept
	{
		if (auto pAnim = SendWeaponAnim(anim, iBody, bSkipLocal))
		{
			// It's absolutely none of our business that whether can we attack or so.
			// That's something should be decided in other tasks.
			co_await pAnim->m_total_length;
		}

		co_return;
	}

	Task Task_WeaponAnim(seq_timing_t const* pAnim, int iBody = 0) const noexcept
	{
		if (pAnim)
		{
			m_pPlayer->pev->weaponanim = pAnim->m_iSeqIdx;
			gmsgWeaponAnim::Send(m_pPlayer->edict(), pAnim->m_iSeqIdx, iBody);

			co_await pAnim->m_total_length;
		}

		co_return;
	}

	void SecondaryAttack() noexcept override
	{
		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			if (m_pPlayer->HasShield())
			{
				if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
				{
					m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;
					m_Scheduler.Enroll(Task_WeaponAnim("shield_down"), TASK_BEHAVIOR_SPECIAL | TASK_ANIMATION, true);
					strcpy(m_pPlayer->m_szAnimExtention, "shieldgun");
					m_fMaxSpeed = T::DAT_MAX_SPEED;
					m_pPlayer->m_bShieldDrawn = false;
				}
				else
				{
					m_iWeaponState |= WPNSTATE_SHIELD_DRAWN;
					m_Scheduler.Enroll(Task_WeaponAnim("shield_up"), TASK_BEHAVIOR_SPECIAL | TASK_ANIMATION, true);
					strcpy(m_pPlayer->m_szAnimExtention, "shielded");
					m_fMaxSpeed = T::DAT_SHIELDED_SPEED;
					m_pPlayer->m_bShieldDrawn = true;
				}

				m_pPlayer->UpdateShieldCrosshair((m_iWeaponState & WPNSTATE_SHIELD_DRAWN) != WPNSTATE_SHIELD_DRAWN);
				m_pPlayer->ResetMaxSpeed();

				// Ignore all other abilities.
				return;
			}
		}

		if constexpr (requires { T::m_bBurstFire; })
		{
			m_Scheduler.Enroll(
				[](CBasePistol* self) static noexcept -> Task
				{
					if (self->m_iWeaponState & WPNSTATE_GLOCK18_BURST_MODE)
					{
						gmsgTextMsg::Send(self->m_pPlayer->edict(), HUD_PRINTCENTER, "#Switch_To_SemiAuto");
						self->m_iWeaponState &= ~WPNSTATE_GLOCK18_BURST_MODE;
					}
					else
					{
						gmsgTextMsg::Send(self->m_pPlayer->edict(), HUD_PRINTCENTER, "#Switch_To_BurstFire");
						self->m_iWeaponState |= WPNSTATE_GLOCK18_BURST_MODE;
					}

					// Prevent quick clicking
					co_await 0.3f;

				}(this),
				TASK_BEHAVIOR_SPECIAL,
				true
			);
		}
		else if constexpr (requires { T::FLAG_SECATK_SILENCER; })
		{
			m_Scheduler.Enroll(
				[](CBasePistol* self) static noexcept -> Task
				{
					auto const pSilencerAnim = self->SendWeaponAnim(
						(self->m_iWeaponState & WPNSTATE_USP_SILENCED) ? "detach_silencer" : "add_silencer"
					);
					assert(pSilencerAnim != nullptr);

					co_await pSilencerAnim->m_total_length;

					// Invert the flag.
					self->m_iWeaponState ^= WPNSTATE_USP_SILENCED;

				}(this),
				TASK_BEHAVIOR_SPECIAL | TASK_ANIMATION,
				true
			);
		}
	}

	void EjectBrass(bool bShootingLeft) const noexcept
	{
		auto&& [fwd, right, up] = m_pPlayer->pev->v_angle.AngleVectors();
		auto constexpr soundType = (T::PROTOTYPE_ID == WEAPON_XM1014 || T::PROTOTYPE_ID == WEAPON_M3) ? TE_BOUNCE_SHOTSHELL : TE_BOUNCE_SHELL;
		auto const vecOrigin = m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs + up * -9 + fwd * 16;

		if (bShootingLeft)
		{
			MsgPVS(SVC_TEMPENTITY, vecOrigin);
			WriteData(TE_MODEL);
			WriteData(vecOrigin + right * -7);
			WriteData(
				m_pPlayer->pev->velocity
					+ UTIL_Random<float>(50, 70) * right
					+ UTIL_Random<float>(100, 150) * up
					+ 25 * fwd
			);
			WriteData(msg_angle_t{ pev->angles.yaw });
			WriteData((uint16_t)m_iShellId);
			WriteData(soundType);
			WriteData(static_cast<uint8_t>(50));
			MsgEnd();
		}
		else
		{
			gmsgBrass::Region<MSG_PVS>(vecOrigin,
				vecOrigin,
				m_pPlayer->pev->velocity
					+ UTIL_Random<float>(50, 70) * right
					+ UTIL_Random<float>(100, 150) * up
					+ 25 * fwd,
				pev->angles.yaw,
				m_iShellId,
				soundType,
				(std::uint8_t)m_pPlayer->entindex()
			);
		}
	}

	Task Task_ShootingEffects(bool bShootingLeft) const noexcept
	{
		if constexpr (requires { T::FLAG_DUAL_WIELDING; })
		{
			// Fuck CS left hand issue.
			bShootingLeft = !bShootingLeft;
		}

		CRTP()->EFFC_SND_FIRING();

		co_await TaskScheduler::NextFrame::Rank[0];
		if (!m_pPlayer->IsAlive())
			co_return;

		if (!IsSilenced())
		{
			UTIL_DLight(m_pPlayer->GetGunPosition(), 4.5f, { 255, 150, 15 }, 8, 60);

			co_await TaskScheduler::NextFrame::Rank[0];
			if (!m_pPlayer->IsAlive())
				co_return;
		}

		EjectBrass(bShootingLeft);

		co_await TaskScheduler::NextFrame::Rank[0];
		if (!m_pPlayer->IsAlive())
			co_return;

		CreateGunSmoke(m_pPlayer, requires { T::FLAG_IS_PISTOL; }, bShootingLeft);	// CS got right hand issue.
	}

	Task Task_BurstFire() noexcept
	{
		if constexpr (requires { T::m_bBurstFire; })
		{
			CRTP()->m_bBurstFire = true;

			for (int i = 0; i < T::DAT_BURST_FIRE_COUNT; ++i)
			{
				WeaponFire();

				if (!m_iClip)
					break;

				co_await T::DAT_BURST_FIRE_INVERVAL;
			}

			CRTP()->m_bBurstFire = false;

			co_await (T::DAT_BURST_FIRE_INVERVAL * 10.f);
		}
		else
			co_return;
	}

	void WeaponFire() noexcept
	{
		if (m_flLastFire)
		{
			m_flAccuracy = std::ranges::clamp(
				// Mark the time of this shot and determine the accuracy modifier based on the last shot fired...
				CRTP()->EXPR_ACCY(),
				CRTP()->DAT_ACCY_RANGE.first,
				CRTP()->DAT_ACCY_RANGE.second
			);
		}

		m_flLastFire = gpGlobals->time;

		if (m_iClip <= 0)
		{
			PlayEmptySound();
			m_flNextPrimaryAttack = 0.2f;
			m_Scheduler.Enroll([]() static noexcept -> Task { co_await 0.2f; }(), TASK_BEHAVIOR_SHOOT, true);

			if (ZBot::Manager())
				ZBot::Manager()->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);

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

		m_pPlayer->m_iWeaponVolume = CRTP()->DAT_FIRE_VOLUME;
		m_pPlayer->m_iWeaponFlash = CRTP()->DAT_FIRE_FLASH;

		if (!IsSilenced())
			m_pPlayer->pev->effects |= EF_MUZZLEFLASH;

		[[maybe_unused]]
		auto const [vecFwd, vecRight, vecUp]
			= (m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle).AngleVectors();

		if constexpr (requires { T::FLAG_DUAL_WIELDING; })
		{
			CS_FireBullets3(
				m_pPlayer, this,
				bShootingLeft ? (vecRight * -5) : (vecRight * 5),
				CRTP()->EXPR_SPREAD(), CRTP()->DAT_EFF_SHOT_DIST, CRTP()->DAT_PENETRATION,
				CRTP()->DAT_BULLET_TYPE, CRTP()->EXPR_DAMAGE(), CRTP()->DAT_RANGE_MODIFIER
			);
		}
		else
		{
			CS_FireBullets3(
				m_pPlayer, this,
				g_vecZero, CRTP()->EXPR_SPREAD(), CRTP()->DAT_EFF_SHOT_DIST, CRTP()->DAT_PENETRATION,
				CRTP()->DAT_BULLET_TYPE, CRTP()->EXPR_DAMAGE(), CRTP()->DAT_RANGE_MODIFIER
			);
		}

		// LUNA:
		// PBE can be consider as the composition of the following:
		// I. First personal effect
			// [X] Muzzle flash & DLight - MSG
			// [X] Gun animation - MSG
			// [X] Shell ejection - EjectBrassLate()
			// [X] Smoking gun - ENT
			// [X] Sound - ev_hldm.cpp
		// II. Traceline - Make our own FireBullets function?
			// [X] Trace effect - MSG
			// [X] Bullet hole - MSG
			// [X] Debris - MSG - DSHGFHDS
			// [ ] Smoke by tex color - ENT
			// [X] Sound of bullet hit
			// [ ] Bullet flyby whizz noise.

		bool bShouldUseRegularFireInterval = true;
		if constexpr (requires { T::m_bBurstFire; })
			bShouldUseRegularFireInterval = !CRTP()->m_bBurstFire;

		if (bShouldUseRegularFireInterval)
		{
			m_flNextPrimaryAttack = m_flNextSecondaryAttack = CRTP()->DAT_FIRE_INTERVAL;
			m_Scheduler.Enroll([]() static noexcept -> Task { co_await T::DAT_FIRE_INTERVAL; }(), TASK_BEHAVIOR_SHOOT, true);
		}

		static constexpr CAnimationGroup<T, typename T::AnimDat_Shoot> ShootAnimSelector{};
		auto pShootingAnim = ShootAnimSelector(this);

		if constexpr (requires { typename T::AnimDat_ShootOther; })
		{
			if (bShootingLeft)
			{
				static constexpr CAnimationGroup<T, typename T::AnimDat_ShootOther> ShootOtherAnimSelector{};
				pShootingAnim = ShootOtherAnimSelector(this);
			}
		}
		if constexpr (requires { typename T::AnimDat_ShootOneEmpty; })
		{
			if (m_iClip == 1)
			{
				static constexpr CAnimationGroup<T, typename T::AnimDat_ShootOneEmpty> ShootOneLastAnimSelector{};
				pShootingAnim = ShootOneLastAnimSelector(this);
			}
		}
		if constexpr (requires { typename T::AnimDat_ShootLast; })
		{
			if (m_iClip == 0)
			{
				static constexpr CAnimationGroup<T, typename T::AnimDat_ShootLast> ShootLastAnimSelector{};
				pShootingAnim = ShootLastAnimSelector(this);
			}
		}

		m_flTimeWeaponIdle = pShootingAnim->m_total_length;	// Change it to anim time of shooting anim.
		m_Scheduler.Enroll(Task_WeaponAnim(pShootingAnim), TASK_ANIMATION, true);

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
				++m_iGlock18ShotsFired;
				m_flGlock18Shoot = gpGlobals->time + 0.1f;
			}
		}

		CRTP()->EFFC_RECOIL();

		m_Scheduler.Enroll(Task_ShootingEffects(bShootingLeft));	// Can't be overwrite by others.
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
			{
				m_Scheduler.Enroll(Task_BurstFire(), TASK_BEHAVIOR_SHOOT, true);
				return;
			}
		}

		if (++m_iShotsFired > 1)
			return;

		WeaponFire();
	}

	qboolean PlayEmptySound() noexcept override
	{
		if constexpr (requires { T::FLAG_IS_PISTOL; })
			g_engfuncs.pfnEmitSound(m_pPlayer->edict(), CHAN_WEAPON, "weapons/dryfire_pistol.wav", 0.8f, ATTN_NORM, SND_FL_NONE, PITCH_NORM);
		else
			g_engfuncs.pfnEmitSound(m_pPlayer->edict(), CHAN_WEAPON, "weapons/dryfire_rifle.wav", 0.8f, ATTN_NORM, SND_FL_NONE, PITCH_NORM);

		return true;
	}

	Task Task_Reload(seq_timing_t const* pReloadAnim) noexcept
	{
		if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
			co_return;

		auto const j = std::min(T::DAT_MAX_CLIP - m_iClip, m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]);
		if (!j)
			co_return;

		m_pPlayer->m_flNextAttack = pReloadAnim->m_fz_end;

		ReloadSound();
		SendWeaponAnim(pReloadAnim->m_iSeqIdx);

		m_fInReload = true;
		m_flTimeWeaponIdle = pReloadAnim->m_total_length;

		// complete the reload.
		co_await pReloadAnim->m_fz_end;

		// Add them to the clip
		m_iClip += j;
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= j;

		m_pPlayer->TabulateAmmo();
		m_fInReload = false;

		// Block the idle anim.
		co_await std::max(0.f, pReloadAnim->m_total_length - pReloadAnim->m_fz_end);
	}

	void Reload() noexcept override
	{
		if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
			return;

		static constexpr CAnimationGroup<T, typename T::AnimDat_Reload> AnimSelector{};
		auto const pAnimInfo = AnimSelector(this);

		m_Scheduler.Enroll(Task_Reload(pAnimInfo), TASK_BEHAVIOR_RELOAD | TASK_ANIMATION, true);
		if (m_Scheduler.Exist(TASK_BEHAVIOR_RELOAD))	// If no reload is happening, it wont pass the test here.
		{
			m_pPlayer->SetAnimation(PLAYER_RELOAD);
			m_flAccuracy = T::DAT_ACCY_INIT;
		}
	}

	void WeaponIdle() noexcept override
	{
		//ResetEmptySound();	// LUNA: useless call.
		m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);

		if (m_flTimeWeaponIdle > 0 || m_Scheduler.Exist(TASK_ANIMATION))
			return;

		// Dont sent idle anim to make the slide back - the last frame of shoot_last!
		if (m_iClip <= 0)
			return;

		static constexpr CAnimationGroup<T, typename T::AnimDat_Idle> AnimSelector{};
		auto pAnimInfo = AnimSelector(this);

		if constexpr (requires { typename T::AnimDat_ShieldedIdle; })
		{
			static constexpr CAnimationGroup<T, typename T::AnimDat_ShieldedIdle> ShieldedAnimSelector{};
			if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
				pAnimInfo = ShieldedAnimSelector(this);
		}
		else if constexpr (requires { typename T::AnimDat_OneEmptyIdle; })
		{
			static constexpr CAnimationGroup<T, typename T::AnimDat_OneEmptyIdle> OneEmptyAnimSelector{};
			if (m_iClip == 1)
				pAnimInfo = OneEmptyAnimSelector(this);
		}

		m_flTimeWeaponIdle = std::max(5.f, pAnimInfo->m_total_length);
		m_Scheduler.Enroll(
			[](seq_timing_t const* pAnimInfo, CBasePlayer* pPlayer, int iBody = 0) static noexcept -> Task
			{
				pPlayer->pev->weaponanim = pAnimInfo->m_iSeqIdx;
				gmsgWeaponAnim::Send(pPlayer->edict(), pAnimInfo->m_iSeqIdx, iBody);

				co_await std::max(5.f, pAnimInfo->m_total_length);
			}(pAnimInfo, m_pPlayer),
			TASK_ANIMATION,
			true
		);
	}

	void Holster(int skiplocal = 0) noexcept override
	{
		__super::Holster(skiplocal);
		m_Scheduler.Clear();
	}

public: // Materializing weapon, like CWeaponBox

	int16_t m_iWeaponBoxStoredAmmo{};

	void Drop() noexcept override
	{
		// From CBasePlayer::DropPlayerItem

		if (m_pPlayer->m_bIsVIP)
		{
			gmsgTextMsg::Send(m_pPlayer->edict(), HUD_PRINTCENTER, "#Weapon_Cannot_Be_Dropped");
			return;
		}
		else if (m_pPlayer->HasShield())
		{
			m_pPlayer->DropShield();
			return;
		}

		if (!CanDrop())
		{
			gmsgTextMsg::Send(m_pPlayer->edict(), HUD_PRINTCENTER, "#Weapon_Cannot_Be_Dropped");
			return;
		}

		// take item off hud
		m_pPlayer->pev->weapons &= ~(1 << T::PROTOTYPE_ID);

		// No more weapon
		if ((m_pPlayer->pev->weapons & ~(1 << WEAPON_SUIT)) == 0)
			m_pPlayer->m_iHideHUD |= HIDEHUD_WEAPONS;

#ifdef _DEBUG
		if (g_pGameRules)	// #FIXME crash when exiting game?
#endif
		g_pGameRules->GetNextBestWeapon(m_pPlayer, this);
		g_engfuncs.pfnMakeVectors(m_pPlayer->pev->angles);

		if constexpr (T::DAT_SLOT == 0)
			m_pPlayer->m_bHasPrimary = false;

		if (FClassnameIs(pev, "weapon_c4"))
		{
			// Refer to ReGameDLL for BOT stuff. #TODO_PIW_C4
		}

		// From CWeaponBox::Spawn and packPlayerWeapon()

		pev->angles = Angles{};
		pev->solid = SOLID_BBOX;
		pev->movetype = MOVETYPE_BOUNCE;
		pev->friction = 0.7f;
		pev->gravity = 1.4f;
		pev->owner = m_pPlayer->edict();	// Preventing self-touching for a sec.

		if (m_pPlayer->IsAlive())
			pev->velocity = m_pPlayer->pev->velocity + m_pPlayer->pev->v_angle.Front() * 400;
		else
			pev->velocity = m_pPlayer->pev->velocity;	// Drop along with the dead guy.

		g_engfuncs.pfnSetModel(edict(), T::MODEL_W);
		g_engfuncs.pfnSetSize(edict(), Vector{ -16, -16, -0.5f }, Vector{ 16, 16, 0.5f });
		g_engfuncs.pfnSetOrigin(edict(), m_pPlayer->GetGunPosition() + m_pPlayer->pev->v_angle.Front() * 70);

		// From CWeaponBox::PackWeapon

		if (m_pPlayer)
		{
			if (m_pPlayer->m_pActiveItem == this)
			{
				Holster();
			}

			if (!m_pPlayer->RemovePlayerItem(this))
			{
				// failed to unhook the weapon from the player!
				assert(false);
				return;
			}
		}

		pev->spawnflags |= SF_NORESPAWN;
		//pWeapon->pev->movetype = MOVETYPE_NONE;
		//pWeapon->pev->solid = SOLID_NOT;
		pev->effects = 0;
		//pWeapon->pev->modelindex = 0;
		//pWeapon->pev->model = 0;
		//pWeapon->pev->owner = ENT(pev);
		//pWeapon->SetThink(nullptr);
		//pWeapon->SetTouch(nullptr);
		pev->aiment = nullptr;	// LUNA: fuck my life..
		pev->vuser4.z = 9527;	// LUNA: a marker to PM_MOVE so it won't block player walking.

		// From ::packPlayerItem

		bool bAnotherWeaponUsingSameAmmo = false;
		for (CBasePlayerWeapon* pWeapon : Query::all_weapons_belongs_to(m_pPlayer))
		{
			if (pWeapon == this)
				continue;

			if (pWeapon->m_iPrimaryAmmoType == this->m_iPrimaryAmmoType)
			{
				bAnotherWeaponUsingSameAmmo = true;
				break;
			}
		}

		if (T::DAT_ITEM_FLAGS & ITEM_FLAG_EXHAUSTIBLE || !bAnotherWeaponUsingSameAmmo)
		{
			auto const iAmmoIndex = m_pPlayer->GetAmmoIndex(T::DAT_AMMO_NAME);
			assert(iAmmoIndex == m_iPrimaryAmmoType);

			if (iAmmoIndex != -1)
			{
				// From pWeaponBox->PackAmmo

				m_iWeaponBoxStoredAmmo += m_pPlayer->m_rgAmmo[iAmmoIndex];

				m_pPlayer->m_rgAmmo[iAmmoIndex] = 0;
			}
		}

		m_pPlayer = nullptr;

		this->SetTouch(&T::Touch_Materialized);
		m_Scheduler.Enroll(Task_UnsetOwner(), TASK_MATERIALIZED_UNSET_OWNER, true);
#ifdef _DEBUG
		m_Scheduler.Enroll(Task_PrintInfo());
#endif
		//TaskScheduler::Enroll(
		//	[](EHANDLE<CBaseMaterializableWeapon> self) static noexcept -> Task
		//{
		//	for (; (bool)self;)
		//	{
		//		g_engfuncs.pfnServerPrint(
		//			std::format("eff: {}, model: {}\n", self->pev->effects, STRING(self->pev->model)).c_str()
		//		);
		//
		//		co_await 0.01f;
		//
		//		MsgBroadcast(SVC_TEMPENTITY);
		//		WriteData(TE_SPARKS);
		//		WriteData(self->pev->origin);
		//		MsgEnd();
		//
		//		co_await 0.1f;
		//	}
		//}(this)
		//);
	}

	Task Task_UnsetOwner() const noexcept
	{
		co_await 0.5f;
		pev->owner = nullptr;
	}

	// To Remove weapon
	// Ham_Weapon_RetireWeapon
	// Ham_RemovePlayerItem
	// Ham_Item_Kill

	void Touch_Nonplayer(CBaseEntity* pOther) noexcept
	{
		if (pev->flags & FL_ONGROUND)
		{
			pev->velocity *= 0.95f;
		}
	}

#ifdef _DEBUG
	Task Task_PrintInfo() const noexcept
	{
		for (;; co_await 0.1f)
		{
			//g_engfuncs.pfnServerPrint(
			//	std::format("SOLID: {}\n", (int)pev->solid).c_str()
			//);
		}
	}
#endif

	void Touch_Materialized(CBaseEntity* pOther) noexcept
	{
		// From CWeaponBox::Touch

		if (!pOther->IsPlayer())
		{
			// only players may touch a weaponbox.
			return Touch_Nonplayer(pOther);
		}

		if (!pOther->IsAlive())
		{
			// no dead guys.
			return Touch_Nonplayer(pOther);
		}

		CBasePlayer* pPlayer = static_cast<CBasePlayer*>(pOther);

		if (pPlayer->m_bIsVIP || pPlayer->m_bShieldDrawn)
			return;

		// LUNA: Extention by ReGameDLL.
		pPlayer->OnTouchingWeapon(this);

		if constexpr ((T::PROTOTYPE_ID == WEAPON_ELITE || T::DAT_SLOT == 0))
		{
			if (pPlayer->HasShield())
				return Touch_Nonplayer(pOther);
		}

		bool bPickedUp = false;

		if (pPlayer->IsBot())
		{
			// Allowing this weapon? #TODO_PIW_BOT
		}

		if (FClassnameIs(this->pev, "weapon_c4"))
		{
			// refer to ReGameDLL #TODO_PIW_C4
		}

		if constexpr (T::DAT_SLOT == 0 || T::DAT_SLOT == 1)
		{
			// Hud slot is 1 less than actual slot.
			if (pPlayer->m_rgpPlayerItems[T::DAT_SLOT + 1] == nullptr
				&& pPlayer->AddPlayerItem(this))
			{
				this->AttachToPlayer(pPlayer);
				g_engfuncs.pfnEmitSound(pPlayer->edict(), CHAN_ITEM, "items/gunpickup2.wav", VOL_NORM, ATTN_NORM, SND_FL_NONE, PITCH_NORM);

				if (m_iWeaponBoxStoredAmmo > 0)
				{
					pPlayer->GiveAmmo(m_iWeaponBoxStoredAmmo, (char*)T::DAT_AMMO_NAME, T::DAT_AMMO_MAX);
					m_iWeaponBoxStoredAmmo = 0;
				}

				bPickedUp = true;
			}

		}
		else if constexpr (T::DAT_SLOT == 3)
		{
			// #TODO_PIW_GRENADE
		}

		// From materialzed CWeaponBox to invisible weapon.
		if (bPickedUp)
		{
			// Is this already done in AttachToPlayer() ?
		}
	}

	void AttachToPlayer(CBasePlayer* pPlayer) noexcept override
	{
		__super::AttachToPlayer(pPlayer);
		this->m_Scheduler.Delist(TASK_ALL_MATERIALIZED);
	}

public:
	constexpr bool IsSilenced() const noexcept
	{
		if constexpr (requires { T::FLAG_SECATK_SILENCER; })
		{
			if (m_iWeaponState & (WPNSTATE_USP_SILENCED | WPNSTATE_M4A1_SILENCED))
				return true;
		}

		if constexpr (T::PROTOTYPE_ID == WEAPON_TMP)
			return true;
		else
			return false;
	}

private:
	__forceinline [[nodiscard]] T* CRTP() noexcept { static_assert(std::is_base_of_v<CBasePistol, T>); return static_cast<T*>(this); }
	__forceinline [[nodiscard]] T const* CRTP() const noexcept { static_assert(std::is_base_of_v<CBasePistol, T>); return static_cast<T const*>(this); }
};

struct CPistolGlock : CBasePistol<CPistolGlock>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_GLOCK18;

	static inline constexpr char MODEL_V[] = "models/v_glock18.mdl";
	static inline constexpr char MODEL_V_SHIELD[] = "models/shield/v_shield_glock18.mdl";
	static inline constexpr char MODEL_W[] = "models/w_glock18.mdl";
	static inline constexpr char MODEL_P[] = "models/p_glock18.mdl";
	static inline constexpr char MODEL_P_SHIELD[] = "models/shield/p_shield_glock18.mdl";
	static inline constexpr char MODEL_SHELL[] = "models/pshell.mdl";

	static inline constexpr char ANIM_3RD_PERSON[] = "onehanded";

	struct AnimDat_Idle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "shield"sv, };
	};
	struct AnimDat_ShieldedIdle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr std::array KEYWORD_SHIELD{ "shield"sv, };
	};
	struct AnimDat_Shoot final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };	// G18 gets a weird anim sequence.
		static inline constexpr auto EXCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_ShootLast final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto INCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_Draw final {
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, };
	};
	struct AnimDat_Reload final {
		static inline constexpr std::array KEYWORD{ "reload"sv, };
	};

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
	static inline constexpr char DAT_HUD_NAME[] = "weapon_glock18";
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
	static inline constexpr auto DAT_BURST_FIRE_INVERVAL = 0.05f;
	static inline constexpr auto DAT_BURST_FIRE_COUNT = 3;

	static inline constexpr float EXPR_DAMAGE() noexcept {
		return 25.f;
	}
	inline float EXPR_ACCY() const noexcept {
		return m_flAccuracy - (0.325f - (gpGlobals->time - m_flLastFire)) * 0.275f;
	}
	inline float EXPR_SPREAD() const noexcept
	{
		if (m_iWeaponState & WPNSTATE_GLOCK18_BURST_MODE)
		{
			if (!(m_pPlayer->pev->flags & FL_ONGROUND))
				return 1.2f * (1.f - m_flAccuracy);
			else if (m_pPlayer->pev->velocity.LengthSquared2D() > 0)
				return 0.185f * (1.f - m_flAccuracy);
			else if (m_pPlayer->pev->flags & FL_DUCKING)
				return 0.095f * (1.f - m_flAccuracy);
			else
				return 0.3f * (1.f - m_flAccuracy);
		}
		else
		{
			if (!(m_pPlayer->pev->flags & FL_ONGROUND))
				return 1.f * (1.f - m_flAccuracy);
			else if (m_pPlayer->pev->velocity.LengthSquared2D() > 0)
				return 0.165f * (1.f - m_flAccuracy);
			else if (m_pPlayer->pev->flags & FL_DUCKING)
				return 0.075f * (1.f - m_flAccuracy);
			else
				return 0.1f * (1.f - m_flAccuracy);
		}
	}
	static inline auto EXPR_FIRING_SND() noexcept -> span<string const> {
		static const std::array rgszSounds{ "weapons/glock18-2.wav"s };
		return rgszSounds;
	}
	static inline constexpr void EFFC_RECOIL() noexcept {
		//pWeapon->m_pPlayer->pev->punchangle.pitch -= 2;
		// G18 in CS doesn't have any recoil at all.
	};
	inline void EFFC_SND_FIRING() const noexcept {
		g_engfuncs.pfnEmitSound(edict(), CHAN_WEAPON,
			UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(),
			VOL_NORM, ATTN_NORM, SND_FL_NONE, 94 + UTIL_Random(0, 0xf)
		);
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
	//static inline constexpr auto FLAG_NO_ITEM_INFO = true;
	//static inline constexpr auto FLAG_DUAL_WIELDING = true;	// Emulate vanilla elites.
	//static inline constexpr auto FLAG_SECATK_SILENCER = true;	// Emulate vanilla USP
	bool m_bBurstFire{};	// A flag to emulate vanilla g18
};

template void LINK_ENTITY_TO_CLASS<CPistolGlock>(entvars_t* pev) noexcept;

struct CPistolUSP : CBasePistol<CPistolUSP>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_USP;

	static inline constexpr char MODEL_V[] = "models/v_usp.mdl";
	static inline constexpr char MODEL_V_SHIELD[] = "models/shield/v_shield_usp.mdl";
	static inline constexpr char MODEL_W[] = "models/w_usp.mdl";
	static inline constexpr char MODEL_P[] = "models/p_usp.mdl";
	static inline constexpr char MODEL_P_SHIELD[] = "models/shield/p_shield_usp.mdl";
	static inline constexpr char MODEL_SHELL[] = "models/pshell.mdl";

	static inline constexpr char ANIM_3RD_PERSON[] = "onehanded";

	struct AnimDat_Idle final
	{
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		//static inline constexpr std::array<std::string_view, 0> KEYWORD_SHIELD{  };
		static inline constexpr std::array KEYWORD_UNSIL{ "unsil"sv, };

		//static inline constexpr std::array INCLUSION{};
		static inline constexpr auto EXCLUSION = UTIL_MergeArray(/*KEYWORD_SHIELD, */KEYWORD_UNSIL, std::array{ "shield"sv, });
	};
	struct AnimDat_ShieldedIdle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr std::array KEYWORD_SHIELD{ "shield"sv, };
	};
	struct AnimDat_Shoot final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr std::array KEYWORD_UNSIL{ "unsil"sv, };

		static inline constexpr auto EXCLUSION = UTIL_MergeArray(/*KEYWORD_SHIELD, */KEYWORD_UNSIL, std::array{ "last"sv, "empty"sv, }, std::array{ "shield"sv, });
	};
	struct AnimDat_ShootLast final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr std::array KEYWORD_UNSIL{ "unsil"sv, };

		static inline constexpr auto INCLUSION = std::array{ "last"sv, "empty"sv, };
		static inline constexpr auto EXCLUSION = UTIL_MergeArray(/*KEYWORD_SHIELD, */KEYWORD_UNSIL, std::array{ "shield"sv, });
	};
	struct AnimDat_Draw final {
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, };
		static inline constexpr std::array KEYWORD_UNSIL{ "unsil"sv, };

		static inline constexpr auto EXCLUSION = UTIL_MergeArray(/*KEYWORD_SHIELD, */KEYWORD_UNSIL, std::array{ "shield"sv, });
	};
	struct AnimDat_Reload final {
		static inline constexpr std::array KEYWORD{ "reload"sv, };
		static inline constexpr std::array KEYWORD_UNSIL{ "unsil"sv, };

		static inline constexpr auto EXCLUSION = UTIL_MergeArray(/*KEYWORD_SHIELD, */KEYWORD_UNSIL, std::array{ "shield"sv, });
	};

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
	static inline constexpr char DAT_HUD_NAME[] = "weapon_usp";
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

	inline float EXPR_DAMAGE() const noexcept {
		return (m_iWeaponState & WPNSTATE_USP_SILENCED) ? 30.f : 34.f;
	};
	inline float EXPR_ACCY() const noexcept {
		return m_flAccuracy - (0.3f - (gpGlobals->time - m_flLastFire)) * 0.275f;
	};
	inline float EXPR_SPREAD() const noexcept
	{
		if (m_iWeaponState & WPNSTATE_USP_SILENCED)
		{
			if (!(m_pPlayer->pev->flags & FL_ONGROUND))
				return 1.3f * (1.f - m_flAccuracy);
			else if (m_pPlayer->pev->velocity.Length2D() > 0)
				return 0.25f * (1.f - m_flAccuracy);
			else if (m_pPlayer->pev->flags & FL_DUCKING)
				return 0.125f * (1.f - m_flAccuracy);
			else
				return 0.15f * (1.f - m_flAccuracy);
		}
		else
		{
			if (!(m_pPlayer->pev->flags & FL_ONGROUND))
				return 1.2f * (1.f - m_flAccuracy);
			else if (m_pPlayer->pev->velocity.Length2D() > 0)
				return 0.225f * (1.f - m_flAccuracy);
			else if (m_pPlayer->pev->flags & FL_DUCKING)
				return 0.08f * (1.f - m_flAccuracy);
			else
				return 0.1f * (1.f - m_flAccuracy);
		}
	};
	inline auto EXPR_FIRING_SND() const noexcept -> span<string const> {
		static const auto rgszSounds{ CollectSounds("weapons/usp") };
		static const auto rgszSoundsUnsil{ CollectSounds("weapons/usp_unsil-") };

		if (m_iWeaponState & (WPNSTATE_USP_SILENCED | WPNSTATE_M4A1_SILENCED))
			return rgszSounds;
		else
			return rgszSoundsUnsil;
	}
	inline void EFFC_RECOIL() const noexcept {
		m_pPlayer->pev->punchangle.pitch -= 2;
	};
	inline void EFFC_SND_FIRING() const noexcept {
		if (m_iWeaponState & (WPNSTATE_USP_SILENCED | WPNSTATE_M4A1_SILENCED))
			g_engfuncs.pfnEmitSound(edict(), CHAN_WEAPON, UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(), VOL_NORM, ATTN_IDLE, SND_FL_NONE, 94 + UTIL_Random(0, 0xf));
		else
			g_engfuncs.pfnEmitSound(edict(), CHAN_WEAPON, UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(), VOL_NORM, ATTN_NORM, SND_FL_NONE, 87 + UTIL_Random(0, 0x12));
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
	//static inline constexpr auto FLAG_NO_ITEM_INFO = true;
	//static inline constexpr auto FLAG_DUAL_WIELDING = true;	// Emulate vanilla elites.
	static inline constexpr auto FLAG_SECATK_SILENCER = true;	// Emulate vanilla USP
	//bool m_bBurstFire{};										// A flag to emulate vanilla g18
};

template void LINK_ENTITY_TO_CLASS<CPistolUSP>(entvars_t* pev) noexcept;

struct CPistolP228 : CBasePistol<CPistolP228>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_P228;

	static inline constexpr char MODEL_V[] = "models/v_p228.mdl";
	static inline constexpr char MODEL_V_SHIELD[] = "models/shield/v_shield_p228.mdl";
	static inline constexpr char MODEL_W[] = "models/w_p228.mdl";
	static inline constexpr char MODEL_P[] = "models/p_p228.mdl";
	static inline constexpr char MODEL_P_SHIELD[] = "models/shield/p_shield_p228.mdl";
	static inline constexpr char MODEL_SHELL[] = "models/pshell.mdl";

	static inline constexpr char ANIM_3RD_PERSON[] = "onehanded";

	struct AnimDat_Idle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "shield"sv, };
	};
	struct AnimDat_ShieldedIdle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr std::array KEYWORD_SHIELD{ "shield"sv, };
	};
	struct AnimDat_Shoot final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_ShootLast final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto INCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_Draw final {
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, };
	};
	struct AnimDat_Reload final {
		static inline constexpr std::array KEYWORD{ "reload"sv, };
	};

	static inline constexpr std::array SOUND_ALL
	{
		"weapons/p228-1.wav",
		"weapons/p228_clipout.wav",
		"weapons/p228_clipin.wav",
		"weapons/p228_sliderelease.wav",
		"weapons/p228_slidepull.wav",
	};

	static inline constexpr char EV_FIRE[] = "events/p228.sc";

	static inline constexpr auto DAT_ACCY_INIT = 0.9f;
	static inline constexpr auto DAT_ACCY_RANGE = std::pair{ 0.6f, 0.9f };
	static inline constexpr auto DAT_MAX_CLIP = 13;
	static inline constexpr auto DAT_MAX_SPEED = 250.f;
	static inline constexpr auto DAT_SHIELDED_SPEED = 180.f;
	static inline constexpr char DAT_AMMO_NAME[] = "357SIG";
	static inline constexpr auto DAT_AMMO_MAX = 52;
	static inline constexpr char DAT_HUD_NAME[] = "weapon_p228";
	static inline constexpr auto DAT_SLOT = 1;
	static inline constexpr auto DAT_SLOT_POS = 3;
	static inline constexpr auto DAT_ITEM_FLAGS = 0;
	static inline constexpr auto DAT_ITEM_WEIGHT = 5;
	static inline constexpr auto DAT_FIRE_VOLUME = BIG_EXPLOSION_VOLUME;
	static inline constexpr auto DAT_FIRE_FLASH = DIM_GUN_FLASH;
	static inline constexpr auto DAT_EFF_SHOT_DIST = 4096.f;
	static inline constexpr auto DAT_PENETRATION = 1;
	static inline constexpr auto DAT_BULLET_TYPE = BULLET_PLAYER_357SIG;
	static inline constexpr auto DAT_RANGE_MODIFIER = 0.8f;
	static inline constexpr auto DAT_FIRE_INTERVAL = 0.2f - 0.05f;

	static inline constexpr float EXPR_DAMAGE() noexcept {
		return 32.f;
	}
	inline float EXPR_ACCY() const noexcept {
		return m_flAccuracy - (0.325f - (gpGlobals->time - m_flLastFire)) * 0.3f;
	}
	inline float EXPR_SPREAD() const noexcept
	{
		if (!(m_pPlayer->pev->flags & FL_ONGROUND))
			return 1.5f * (1.f - m_flAccuracy);
		else if (m_pPlayer->pev->velocity.LengthSquared2D() > 0)
			return 0.255f * (1.f - m_flAccuracy);
		else if (m_pPlayer->pev->flags & FL_DUCKING)
			return 0.075f * (1.f - m_flAccuracy);
		else
			return 0.15f * (1.f - m_flAccuracy);
	}
	static inline auto EXPR_FIRING_SND() noexcept -> span<string const> {
		static const auto rgszSounds{ CollectSounds("weapons/p228-") };
		return rgszSounds;
	}
	inline void EFFC_RECOIL() const noexcept {
		m_pPlayer->pev->punchangle.pitch -= 2;
	};
	inline void EFFC_SND_FIRING() const noexcept {
		g_engfuncs.pfnEmitSound(edict(), CHAN_WEAPON,
			UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(),
			VOL_NORM, ATTN_NORM, SND_FL_NONE, UTIL_Random(94, 94 + 0xf)
		);
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
};

template void LINK_ENTITY_TO_CLASS<CPistolP228>(entvars_t* pev) noexcept;

struct CPistolDeagle : CBasePistol<CPistolDeagle>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_DEAGLE;

	static inline constexpr char MODEL_V[] = "models/v_deagle.mdl";
	static inline constexpr char MODEL_V_SHIELD[] = "models/shield/v_shield_deagle.mdl";
	static inline constexpr char MODEL_W[] = "models/w_deagle.mdl";
	static inline constexpr char MODEL_P[] = "models/p_deagle.mdl";
	static inline constexpr char MODEL_P_SHIELD[] = "models/shield/p_shield_deagle.mdl";
	static inline constexpr char MODEL_SHELL[] = "models/pshell.mdl";

	static inline constexpr char ANIM_3RD_PERSON[] = "onehanded";

	struct AnimDat_Idle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "shield"sv, };
	};
	struct AnimDat_ShieldedIdle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr std::array KEYWORD_SHIELD{ "shield"sv, };
	};
	struct AnimDat_Shoot final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_ShootLast final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto INCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_Draw final {
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, };
	};
	struct AnimDat_Reload final {
		static inline constexpr std::array KEYWORD{ "reload"sv, };
	};

	static inline constexpr std::array SOUND_ALL
	{
		"weapons/deagle-1.wav",
		"weapons/deagle-2.wav",
		"weapons/de_clipout.wav",
		"weapons/de_clipin.wav",
		"weapons/de_deploy.wav",
	};

	static inline constexpr char EV_FIRE[] = "events/deagle.sc";

	static inline constexpr auto DAT_ACCY_INIT = 0.9f;
	static inline constexpr auto DAT_ACCY_RANGE = std::pair{ 0.55f, 0.9f };
	static inline constexpr auto DAT_MAX_CLIP = 7;
	static inline constexpr auto DAT_MAX_SPEED = 250.f;
	static inline constexpr auto DAT_SHIELDED_SPEED = 180.f;
	static inline constexpr char DAT_AMMO_NAME[] = "50AE";
	static inline constexpr auto DAT_AMMO_MAX = 35;
	static inline constexpr char DAT_HUD_NAME[] = "weapon_deagle";
	static inline constexpr auto DAT_SLOT = 1;
	static inline constexpr auto DAT_SLOT_POS = 1;
	static inline constexpr auto DAT_ITEM_FLAGS = 0;
	static inline constexpr auto DAT_ITEM_WEIGHT = 7;
	static inline constexpr auto DAT_FIRE_VOLUME = BIG_EXPLOSION_VOLUME;
	static inline constexpr auto DAT_FIRE_FLASH = NORMAL_GUN_FLASH;
	static inline constexpr auto DAT_EFF_SHOT_DIST = 4096.f;
	static inline constexpr auto DAT_PENETRATION = 2;
	static inline constexpr auto DAT_BULLET_TYPE = BULLET_PLAYER_50AE;
	static inline constexpr auto DAT_RANGE_MODIFIER = 0.81f;
	static inline constexpr auto DAT_FIRE_INTERVAL = 0.3f - 0.075f;

	static inline constexpr float EXPR_DAMAGE() noexcept {
		return 54.f;
	}
	inline float EXPR_ACCY() const noexcept {
		return m_flAccuracy - (0.4f - (gpGlobals->time - m_flLastFire)) * 0.35f;
	}
	inline float EXPR_SPREAD() const noexcept
	{
		if (!(m_pPlayer->pev->flags & FL_ONGROUND))
			return 1.5f * (1.f - m_flAccuracy);
		else if (m_pPlayer->pev->velocity.LengthSquared2D() > 0)
			return 0.25f * (1.f - m_flAccuracy);
		else if (m_pPlayer->pev->flags & FL_DUCKING)
			return 0.115f * (1.f - m_flAccuracy);
		else
			return 0.13f * (1.f - m_flAccuracy);
	}
	static inline auto EXPR_FIRING_SND() noexcept -> span<string const> {
		static const auto rgszSounds{ CollectSounds("weapons/deagle-") };
		return rgszSounds;
	}
	inline void EFFC_RECOIL() const noexcept {
		m_pPlayer->pev->punchangle.pitch -= 2;
	};
	inline void EFFC_SND_FIRING() const noexcept {
		g_engfuncs.pfnEmitSound(edict(), CHAN_WEAPON,
			UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(),
			VOL_NORM, 0.6f, SND_FL_NONE, UTIL_Random(94, 94 + 0xf)
		);
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
};

template void LINK_ENTITY_TO_CLASS<CPistolDeagle>(entvars_t* pev) noexcept;

struct CPistolFN57 : CBasePistol<CPistolFN57>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_FIVESEVEN;

	static inline constexpr char MODEL_V[] = "models/v_fiveseven.mdl";
	static inline constexpr char MODEL_V_SHIELD[] = "models/shield/v_shield_fiveseven.mdl";
	static inline constexpr char MODEL_W[] = "models/w_fiveseven.mdl";
	static inline constexpr char MODEL_P[] = "models/p_fiveseven.mdl";
	static inline constexpr char MODEL_P_SHIELD[] = "models/shield/p_shield_fiveseven.mdl";
	static inline constexpr char MODEL_SHELL[] = "models/pshell.mdl";

	static inline constexpr char ANIM_3RD_PERSON[] = "onehanded";

	struct AnimDat_Idle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "shield"sv, };
	};
	struct AnimDat_ShieldedIdle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr std::array KEYWORD_SHIELD{ "shield"sv, };
	};
	struct AnimDat_Shoot final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_ShootLast final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto INCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_Draw final {
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, };
	};
	struct AnimDat_Reload final {
		static inline constexpr std::array KEYWORD{ "reload"sv, };
	};

	static inline constexpr std::array SOUND_ALL
	{
		"weapons/fiveseven-1.wav",
		"weapons/fiveseven_clipout.wav",
		"weapons/fiveseven_clipin.wav",
		"weapons/fiveseven_sliderelease.wav",
		"weapons/fiveseven_slidepull.wav",
	};

	static inline constexpr char EV_FIRE[] = "events/fiveseven.sc";

	static inline constexpr auto DAT_ACCY_INIT = 0.92f;
	static inline constexpr auto DAT_ACCY_RANGE = std::pair{ 0.725f, 0.92f };
	static inline constexpr auto DAT_MAX_CLIP = 20;
	static inline constexpr auto DAT_MAX_SPEED = 250.f;
	static inline constexpr auto DAT_SHIELDED_SPEED = 180.f;
	static inline constexpr char DAT_AMMO_NAME[] = "57mm";
	static inline constexpr auto DAT_AMMO_MAX = 100;
	static inline constexpr char DAT_HUD_NAME[] = "weapon_fiveseven";
	static inline constexpr auto DAT_SLOT = 1;
	static inline constexpr auto DAT_SLOT_POS = 6;
	static inline constexpr auto DAT_ITEM_FLAGS = 0;
	static inline constexpr auto DAT_ITEM_WEIGHT = 5;
	static inline constexpr auto DAT_FIRE_VOLUME = BIG_EXPLOSION_VOLUME;
	static inline constexpr auto DAT_FIRE_FLASH = DIM_GUN_FLASH;
	static inline constexpr auto DAT_EFF_SHOT_DIST = 4096.f;
	static inline constexpr auto DAT_PENETRATION = 1;
	static inline constexpr auto DAT_BULLET_TYPE = BULLET_PLAYER_57MM;
	static inline constexpr auto DAT_RANGE_MODIFIER = 0.885f;
	static inline constexpr auto DAT_FIRE_INTERVAL = 0.2f - 0.05f;

	static inline constexpr float EXPR_DAMAGE() noexcept {
		return 20.f;
	}
	inline float EXPR_ACCY() const noexcept {
		return m_flAccuracy - (0.275f - (gpGlobals->time - m_flLastFire)) * 0.25f;
	}
	inline float EXPR_SPREAD() const noexcept
	{
		if (!(m_pPlayer->pev->flags & FL_ONGROUND))
			return 1.5f * (1.f - m_flAccuracy);
		else if (m_pPlayer->pev->velocity.LengthSquared2D() > 0)
			return 0.255f * (1.f - m_flAccuracy);
		else if (m_pPlayer->pev->flags & FL_DUCKING)
			return 0.075f * (1.f - m_flAccuracy);
		else
			return 0.15f * (1.f - m_flAccuracy);
	}
	static inline auto EXPR_FIRING_SND() noexcept -> span<string const> {
		static const auto rgszSounds{ CollectSounds("weapons/fiveseven-") };
		return rgszSounds;
	}
	inline void EFFC_RECOIL() const noexcept {
		m_pPlayer->pev->punchangle.pitch -= 2;
	};
	inline void EFFC_SND_FIRING() const noexcept {
		g_engfuncs.pfnEmitSound(edict(), CHAN_WEAPON,
			UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(),
			VOL_NORM, ATTN_NORM, SND_FL_NONE, UTIL_Random(94, 94 + 0xf)
		);
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
};

template void LINK_ENTITY_TO_CLASS<CPistolFN57>(entvars_t* pev) noexcept;

struct CPistolBeretta : CBasePistol<CPistolBeretta>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_ELITE;

	static inline constexpr char MODEL_V[] = "models/v_elite.mdl";
	static inline constexpr char MODEL_W[] = "models/w_elite.mdl";
	static inline constexpr char MODEL_P[] = "models/p_elite.mdl";
	static inline constexpr char MODEL_SHELL[] = "models/pshell.mdl";

	static inline constexpr char ANIM_3RD_PERSON[] = "dualpistols";

	struct AnimDat_Idle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "empty"sv, };
	};
	struct AnimDat_OneEmptyIdle final {
		static inline constexpr std::array KEYWORD{ "idle"sv, };
		static inline constexpr auto INCLUSION = std::array{ "empty"sv, };
	};
	struct AnimDat_Shoot final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto INCLUSION = std::array{ "right"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_ShootOther final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto INCLUSION = std::array{ "left"sv, };
		static inline constexpr auto EXCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_ShootOneEmpty final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto INCLUSION = std::array{ "leftlast"sv, "leftempty"sv, };
	};
	struct AnimDat_ShootLast final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto INCLUSION = std::array{ "rightlast"sv, "rightempty"sv, };
	};
	struct AnimDat_Draw final {
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, };
	};
	struct AnimDat_Reload final {
		static inline constexpr std::array KEYWORD{ "reload"sv, };
	};

	static inline constexpr std::array SOUND_ALL
	{
		"weapons/elite_fire.wav",
		"weapons/elite_reloadstart.wav",
		"weapons/elite_leftclipin.wav",
		"weapons/elite_clipout.wav",
		"weapons/elite_sliderelease.wav",
		"weapons/elite_rightclipin.wav",
		"weapons/elite_deploy.wav",
	};

	static inline constexpr char EV_FIRE_L[] = "events/elite_left.sc";
	static inline constexpr char EV_FIRE_R[] = "events/elite_right.sc";

	static inline constexpr auto DAT_ACCY_INIT = 0.88f;
	static inline constexpr auto DAT_ACCY_RANGE = std::pair{ 0.725f, 0.88f };
	static inline constexpr auto DAT_MAX_CLIP = 30;
	static inline constexpr auto DAT_MAX_SPEED = 250.f;
	static inline constexpr char DAT_AMMO_NAME[] = "9mm";
	static inline constexpr auto DAT_AMMO_MAX = 120;
	static inline constexpr char DAT_HUD_NAME[] = "weapon_elite";
	static inline constexpr auto DAT_SLOT = 1;
	static inline constexpr auto DAT_SLOT_POS = 5;
	static inline constexpr auto DAT_ITEM_FLAGS = 0;
	static inline constexpr auto DAT_ITEM_WEIGHT = 5;
	static inline constexpr auto DAT_FIRE_VOLUME = BIG_EXPLOSION_VOLUME;
	static inline constexpr auto DAT_FIRE_FLASH = DIM_GUN_FLASH;
	static inline constexpr auto DAT_EFF_SHOT_DIST = 8192.f;
	static inline constexpr auto DAT_PENETRATION = 1;
	static inline constexpr auto DAT_BULLET_TYPE = BULLET_PLAYER_9MM;
	static inline constexpr auto DAT_RANGE_MODIFIER = 0.885f;
	static inline constexpr auto DAT_FIRE_INTERVAL = 0.2f - 0.078f;

	static inline constexpr float EXPR_DAMAGE() noexcept {
		return 20.f;
	}
	inline float EXPR_ACCY() const noexcept {
		return m_flAccuracy - (0.325f - (gpGlobals->time - m_flLastFire)) * 0.275f;
	}
	inline float EXPR_SPREAD() const noexcept
	{
		if (!(m_pPlayer->pev->flags & FL_ONGROUND))
			return 1.3f * (1.f - m_flAccuracy);
		else if (m_pPlayer->pev->velocity.LengthSquared2D() > 0)
			return 0.175f * (1.f - m_flAccuracy);
		else if (m_pPlayer->pev->flags & FL_DUCKING)
			return 0.08f * (1.f - m_flAccuracy);
		else
			return 0.1f * (1.f - m_flAccuracy);
	}
	static inline auto EXPR_FIRING_SND() noexcept -> span<string const> {
		static const auto rgszSounds{ "weapons/elite_fire.wav"s };
		return span{ &rgszSounds, 1 };
	}
	inline void EFFC_RECOIL() const noexcept {
		m_pPlayer->pev->punchangle.pitch -= 2;
	};
	inline void EFFC_SND_FIRING() const noexcept {
		g_engfuncs.pfnEmitSound(edict(), CHAN_WEAPON,
			UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(),
			VOL_NORM, ATTN_NORM, SND_FL_NONE, UTIL_Random(94, 94 + 0xf)
		);
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_DUAL_WIELDING = true;
};

template void LINK_ENTITY_TO_CLASS<CPistolBeretta>(entvars_t* pev) noexcept;
