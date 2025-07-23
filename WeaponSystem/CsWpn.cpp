
#include <assert.h>	// #UPDATE_AT_CPP26 contract assert

#ifdef __INTELLISENSE__
import std;
#else
import std.compat;	// #MSVC_BUG_STDCOMPAT
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
import Plugin;
import Prefab;
import Query;
import Resources;
import Studio;
import Task;
import Uranus;
import WinAPI;
import ZBot;

import Ammo;
import BPW;
import Buy;
import WpnIdAllocator;

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
	TASK_DEBUG_MONITOR = (1ull << 63),

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

	TASK_EQCEV_EF_MUZZLE_SMOKE = (1ull << 32),
	TASK_EQCEV_EF_EJECT_SHELL = (1ull << 33),
	TASK_EQCEV_EF_MUZZLE_LIGHT = (1ull << 34),

	TASK_EQCEV_DT_CLIP = (1ull << 40),
};

enum EExtendedQcEvents : int32_t
{
	EQCEV_EF_MUZZLE_SMOKE = 6001,
	EQCEV_EF_EJECT_SHELL,
	EQCEV_EF_MUZZLE_LIGHT,

	EQCEV_DT_CLIP = 6101,
};

extern void UpdateGunSmokeList(std::string_view szSmokeNameRoot) noexcept;
extern edict_t* CreateGunSmoke(CBasePlayer* pPlayer, Vector const& vecMuzzleOfs, std::string_view szSmokeNameRoot, int iPlayerAttIdx) noexcept;

extern Vector2D CS_FireBullets3(
	CBasePlayer* pAttacker, CBasePlayerItem* pInflictor,
	Vector const& vecSrcOfs, float flSpread, float flDistance,
	int iPenetration, CAmmoInfo const* pAmmoInfo, float flDamage, float flRangeModifier) noexcept;

[[nodiscard]] static auto GetAnimsFromKeywords(
	string_view szModel, span<string_view const> rgszKeywords,
	span<span<string_view const> const> rgrgszMustInc = {}, span<string_view const> rgszMustExc = {}) noexcept -> vector<TranscriptedSequence const*>
{
	// #UPDATE_AT_CPP26 transparant at
	auto const ModelInfo = Resource::GetStudioTranscription(szModel);
	static auto const fnCaselessCmp = [](char lhs, char rhs) static noexcept
	{
		if (lhs >= 'A' && lhs <= 'Z')
			lhs ^= 0b0010'0000;
		if (rhs >= 'A' && rhs <= 'Z')
			rhs ^= 0b0010'0000;

		return lhs == rhs;
	};

	auto const fnFilter =
		[&](TranscriptedSequence const& Seq) noexcept
		{
			auto const szLabel = string_view{ Seq.m_szLabel };

			// At least one of the keywords must presented.
			bool bContains = false;
			for (auto&& szKeyword : rgszKeywords)
			{
				if (std::ranges::contains_subrange(szLabel, szKeyword, fnCaselessCmp))
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
				if (std::ranges::contains_subrange(szLabel, szBad, fnCaselessCmp))
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
					if (std::ranges::contains_subrange(szLabel, szReq, fnCaselessCmp))
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
		ModelInfo->m_Sequences
		| std::views::filter(fnFilter)
		| std::views::transform([](auto&& a) static noexcept { return std::addressof(a); })
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

extern auto RunDynExpr(string_view szExpr) noexcept -> std::expected<std::any, string>;
extern void DynExprBindVector(string_view name, Vector const& vec) noexcept;
extern void DynExprBindNum(string_view name, double num) noexcept;
extern void DynExprUnbind(string_view name) noexcept;

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
	static inline vector<TranscriptedSequence const*> const* m_pRegulars{};
	static inline vector<TranscriptedSequence const*> const* m_pUnsil{};
	static inline vector<TranscriptedSequence const*> const* m_pShield{};
#endif

	static auto operator()(CBasePlayerWeapon* pWeapon) noexcept -> TranscriptedSequence const*
	{
		static auto const REGULAR{
			GetAnimsFromKeywords(CWeapon::MODEL_V, AnimDat::KEYWORD, std::array{ GetGenericInclusions(), }, GetGenericExclusions())
		};
#ifdef _DEBUG
		assert(!REGULAR.empty()); m_pRegulars = &REGULAR;
#endif
		auto pResult{ UTIL_GetRandomOne(REGULAR) };

		// Unsil
		if constexpr (requires { CWeapon::FLAG_SECATK_SILENCER; })
		{
			static const auto UNSIL_INC = vector{ GetGenericInclusions(), GetUnsilKeywords(), };
			static const auto UNSIL_EXC = UTIL_ArraySetDiff(GetGenericExclusions(), GetUnsilKeywords());
			static auto const ANIM_UNSIL{
				GetAnimsFromKeywords(CWeapon::MODEL_V, AnimDat::KEYWORD, UNSIL_INC, UNSIL_EXC)
			};
#ifdef _DEBUG
			assert(!ANIM_UNSIL.empty()); m_pUnsil = &ANIM_UNSIL;
#endif
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
#ifdef _DEBUG
			assert(!ANIM_SHIELD.empty()); m_pShield = &ANIM_SHIELD;
#endif
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
	static inline std::set<string, sv_iless_t> m_rgszReferencedShellModels{};

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
		CRTP()->SetWorldModel();

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
			m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

		m_iClip = T::DAT_MAX_CLIP;	// Changed from m_iDefaultAmmo, as I changed the meaning of that member.
		m_fMaxSpeed = T::DAT_MAX_SPEED;	// From deagle
		m_flAccuracy = T::DAT_ACCY_INIT;

		assert(CRTP()->DAT_AMMUNITION != nullptr);

		if constexpr (requires { T::m_bBurstFire; })
		{
			CRTP()->m_bBurstFire = false;
			m_iGlock18ShotsFired = 0;
			m_flGlock18Shoot = 0;
		}

		// Get ready to fall down
		CRTP()->FallInit();

		// Gun hit effect.
		CRTP()->pev->takedamage = DAMAGE_NO;

		// extend
		__super::Spawn();

		m_Scheduler.Policy() = ESchedulerPolicy::UNORDERED;
	}

	void Precache() noexcept override
	{
		Resource::Precache(T::MODEL_V);

		// Precache classical WP only if it's specified.
		if constexpr (requires { T::FLAG_USING_OLD_PW; })
		{
			Resource::Precache(T::MODEL_W);
			Resource::Precache(T::MODEL_P);
		}

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			Resource::Precache(T::MODEL_V_SHIELD);

			if constexpr (requires { T::FLAG_USING_OLD_PW; })
				Resource::Precache(T::MODEL_P_SHIELD);
		}

		if constexpr (requires { T::SOUND_ALL; })
		{
			for (auto&& file : T::SOUND_ALL)
				Resource::Precache(file);
		}

		for (auto&& file : CRTP()->EXPR_FIRING_SND())
			Resource::Precache(file);

		// Extra care for USP.
		if constexpr (requires { T::FLAG_SECATK_SILENCER; })
		{
			auto const bitsSavedWpnStates = CRTP()->m_iWeaponState;
			CRTP()->m_iWeaponState = WPNSTATE_USP_SILENCED | WPNSTATE_M4A1_SILENCED;

			for (auto&& file : CRTP()->EXPR_FIRING_SND())
				Resource::Precache(file);

			CRTP()->m_iWeaponState = bitsSavedWpnStates;
		}

		if constexpr (requires { T::MODEL_SHELL; })
			m_iShellId = /*m_iShell =*/ Resource::Precache(T::MODEL_SHELL);

		if constexpr (requires { T::FLAG_DUAL_WIELDING; T::EV_FIRE_R; T::EV_FIRE_L; })
		{
			m_usFireEv = Resource::Precache(T::EV_FIRE_R);	// This goes first.
			m_usFireEv2 = Resource::Precache(T::EV_FIRE_L);
		}
		else if constexpr (requires { T::EV_FIRE; })
			m_usFireEv = Resource::Precache(T::EV_FIRE);

		CRTP()->DAT_AMMUNITION = Ammo_Register(*CRTP()->DAT_AMMUNITION);

		auto pTranscriptedStudio = Resource::GetStudioTranscription(T::MODEL_V);
		for (auto&& Sequence : pTranscriptedStudio->m_Sequences)
		{
			for (auto&& Event : Sequence.m_Events)
			{
				switch (Event.event)
				{
				case EQCEV_EF_MUZZLE_SMOKE:
				{
					auto const QcScript = CRTP()->ParseQcScript(&Event, &Sequence);
					if (QcScript && QcScript->size() >= 3)
						::UpdateGunSmokeList(QcScript->at(2));
					break;
				}
				case EQCEV_EF_EJECT_SHELL:
				{
					auto const QcScript = CRTP()->ParseQcScript(&Event, &Sequence);
					if (QcScript && QcScript->size() >= 3)
					{
						auto&& [it, bNew] = m_rgszReferencedShellModels.emplace(std::format("models/{}", QcScript->at(2)));
						Resource::Precache(*it);
					}
					break;
				}
				case STUDIOEV_PLAYSOUND:
				{
					Resource::Precache(Event.options);
					break;
				}
				default:
					break;
				}
			}
		}
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
			p->pszAmmo1 = CRTP()->DAT_AMMUNITION->m_szName.data();
			p->iMaxAmmo1 = CRTP()->DAT_AMMUNITION->m_iMax;
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

	Task Task_Deploy(TranscriptedSequence const* pAnim) noexcept
	{
		if (!CanDeploy())
			co_return;

		m_pPlayer->TabulateAmmo();

		m_pPlayer->pev->viewmodel = MAKE_STRING(T::MODEL_V);
		m_Scheduler.Enroll(Task_WeaponAnim(pAnim), TASK_ANIMATION, true);	// Deploy anim.

		CRTP()->UpdateThirdPersonModel(TASK_BEHAVIOR_DRAW);

		if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
		{
			if (m_pPlayer->HasShield())
				m_pPlayer->pev->viewmodel = MAKE_STRING(T::MODEL_V_SHIELD);

			// P model for shield was put in UpdateThirdPersonModel()
		}

		// Don't know why Valve created this field..
		CRTP()->model_name = m_pPlayer->pev->viewmodel;

		m_pPlayer->m_flNextAttack = 0;//pAnim->GetTotalLength();
		m_flTimeWeaponIdle = pAnim->GetTotalLength();
		m_flLastFireTime = 0.0f;
		m_flDecreaseShotsFired = gpGlobals->time;

		m_pPlayer->m_iFOV = DEFAULT_FOV;
		m_pPlayer->pev->fov = DEFAULT_FOV;
		m_pPlayer->m_iLastZoom = DEFAULT_FOV;
		m_pPlayer->m_bResumeZoom = false;

		co_await pAnim->GetTotalLength();

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
		static constexpr bool bHasSecondaryAttack =
			requires { T::FLAG_CAN_HAVE_SHIELD; }
			|| requires { T::m_bBurstFire; }
			|| requires { T::FLAG_SECATK_SILENCER; };

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

			if (bHasSecondaryAttack && (m_pPlayer->pev->button & IN_ATTACK2)
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
				if ((m_iClip == 0 && CRTP()->DAT_AMMUNITION != nullptr)
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
	inline auto SendWeaponAnim(string_view anim, int iBody = 0, bool bSkipLocal = false) const noexcept -> TranscriptedSequence const*
	{
		try
		{
			auto const pSeq =
				Resource::GetStudioTranscription(STRING(m_pPlayer->pev->viewmodel))->AtSequence(anim);

			m_pPlayer->pev->weaponanim = pSeq->m_index;

			if (bSkipLocal && g_engfuncs.pfnCanSkipPlayer(m_pPlayer->edict()))
				return pSeq;

			gmsgWeaponAnim::Send(m_pPlayer->edict(), pSeq->m_index, iBody);
			return pSeq;
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
			co_await pAnim->GetTotalLength();
		}

		co_return;
	}

	Task Task_WeaponAnim(TranscriptedSequence const* pAnim, int iBody = 0) const noexcept
	{
		if (pAnim)
		{
			m_pPlayer->pev->weaponanim = pAnim->m_index;
			gmsgWeaponAnim::Send(m_pPlayer->edict(), pAnim->m_index, iBody);

			co_await pAnim->GetTotalLength();
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

					co_await pSilencerAnim->GetTotalLength();

					// Invert the flag.
					self->m_iWeaponState ^= WPNSTATE_USP_SILENCED;

				}(this),
				TASK_BEHAVIOR_SPECIAL | TASK_ANIMATION,
				true
			);
		}
	}

	void EjectBrass(Vector const &vecEjectionPortOfs, Vector const& vecVel, string_view szModel, TE_BOUNCE iSoundType) const noexcept
	{
		auto&& [fwd, right, up] = m_pPlayer->pev->v_angle.AngleVectors();
		auto const vecOrigin = m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs
			+ up * vecEjectionPortOfs.z + fwd * vecEjectionPortOfs.x + right * vecEjectionPortOfs.y;

		MsgPVS(SVC_TEMPENTITY, vecOrigin);
		WriteData(TE_MODEL);
		WriteData(vecOrigin);
		WriteData(
			m_pPlayer->pev->velocity
				+ vecVel.y * right
				+ vecVel.z * up
				+ vecVel.x * fwd
		);
		WriteData(msg_angle_t{ pev->angles.yaw });
		WriteData((uint16_t)Resource::Precache(szModel));	// A quicker way to get modelindex for local res.
		WriteData(iSoundType);
		WriteData(static_cast<uint8_t>(50));
		MsgEnd();
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
				CRTP()->DAT_AMMUNITION, CRTP()->EXPR_DAMAGE(), CRTP()->DAT_RANGE_MODIFIER
			);
		}
		else
		{
			CS_FireBullets3(
				m_pPlayer, this,
				g_vecZero, CRTP()->EXPR_SPREAD(), CRTP()->DAT_EFF_SHOT_DIST, CRTP()->DAT_PENETRATION,
				CRTP()->DAT_AMMUNITION, CRTP()->EXPR_DAMAGE(), CRTP()->DAT_RANGE_MODIFIER
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

		m_flTimeWeaponIdle = pShootingAnim->GetTotalLength();	// Change it to anim time of shooting anim.
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
		CRTP()->EFFC_SND_FIRING();

		// Extended model event dispatching.
		CRTP()->DispatchQcEvents(pShootingAnim);
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

	Task Task_Reload(TranscriptedSequence const* pReloadAnim) noexcept
	{
		if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
			co_return;

		auto const j = std::min(T::DAT_MAX_CLIP - m_iClip, m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]);
		if (!j)
			co_return;

		// Reload QC scripts
		CRTP()->DispatchQcEvents(pReloadAnim);
		bool const bAnyScriptedReloadEv = CRTP()->m_Scheduler.Exist(TASK_EQCEV_DT_CLIP);

		auto const flAnimTotalLen = pReloadAnim->GetTotalLength();
		auto const flAnimFreezeEnds = pReloadAnim->GetTimeAfterLastWav() == -1 ? flAnimTotalLen : pReloadAnim->GetTimeAfterLastWav();

		m_pPlayer->m_flNextAttack = 0;//flAnimFreezeEnds;

		ReloadSound();
		SendWeaponAnim(pReloadAnim->m_index);

		m_fInReload = true;
		m_flTimeWeaponIdle = flAnimTotalLen;

		// complete the reload.
		co_await flAnimFreezeEnds;

		// Add them to the clip
		if (!bAnyScriptedReloadEv)
		{
			m_iClip += j;
			m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= j;
		}

		m_pPlayer->TabulateAmmo();
		m_fInReload = false;

		// Block the idle anim.
		co_await std::max(0.f, flAnimTotalLen - flAnimFreezeEnds);
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

		m_flTimeWeaponIdle = std::max(5.f, pAnimInfo->GetTotalLength());
		m_Scheduler.Enroll(
			[](TranscriptedSequence const* pSeq, CBasePlayer* pPlayer, int iBody = 0) static noexcept -> Task
			{
				pPlayer->pev->weaponanim = pSeq->m_index;
				gmsgWeaponAnim::Send(pPlayer->edict(), pSeq->m_index, iBody);

				co_await std::max(5.f, pSeq->GetTotalLength());
			}(pAnimInfo, m_pPlayer),
			TASK_ANIMATION,
			true
		);
	}

	void Holster(int skiplocal = 0) noexcept override
	{
		// cancel any reload in progress.
		m_fInReload = false;
		m_fInSpecialReload = false;
		m_pPlayer->pev->viewmodel = 0;

		CRTP()->UpdateThirdPersonModel(TASK_BEHAVIOR_HOLSTER);

		m_Scheduler.Clear();
		pev->effects &= ~EF_NODRAW;
	}

public: // Materializing weapon, like CWeaponBox

	void Drop() noexcept override
	{
		// From CBasePlayer::DropPlayerItem

		if (m_pPlayer->m_bIsVIP)
		{
			gmsgTextMsg::Send(m_pPlayer->edict(), HUD_PRINTCENTER, "#Weapon_Cannot_Be_Dropped");
			return;
		}
		// Shield Dropping should not be here. Moved to OrpheuF_DropPlayerItem.

		if (!CRTP()->CanDrop())
		{
			gmsgTextMsg::Send(m_pPlayer->edict(), HUD_PRINTCENTER, "#Weapon_Cannot_Be_Dropped");
			return;
		}

		// take item off hud
		m_pPlayer->pev->weapons &= ~(1 << m_iId);

		// No more weapon
		if ((m_pPlayer->pev->weapons & ~(1 << WEAPON_SUIT)) == 0)
			m_pPlayer->m_iHideHUD |= HIDEHUD_WEAPONS;

#if 1
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

		CRTP()->SetWorldModel();
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
			auto const iAmmoIndex = m_pPlayer->GetAmmoIndex(CRTP()->DAT_AMMUNITION->m_szName.data());
			assert(iAmmoIndex == m_iPrimaryAmmoType);

			if (iAmmoIndex != -1)
			{
				// From pWeaponBox->PackAmmo

				m_iDefaultAmmo += m_pPlayer->m_rgAmmo[iAmmoIndex];

				m_pPlayer->m_rgAmmo[iAmmoIndex] = 0;
			}
		}

		m_pPlayer = nullptr;

		this->SetTouch(&T::Touch_Materialized);
		m_Scheduler.Enroll(Task_UnsetOwner(), TASK_MATERIALIZED_UNSET_OWNER, true);
#ifdef _DEBUG
		m_Scheduler.Enroll(Task_Materialized_PrintInfo(), TASK_DEBUG_MONITOR, true);
#endif
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

	void Kill(void) noexcept override
	{
		if (m_pPlayer && m_pPlayer->m_pActiveItem == this)
			CRTP()->Holster();

		pev->flags |= FL_KILLME;
	}

	void Touch_Nonplayer(CBaseEntity* pOther) noexcept
	{
		if (pev->flags & FL_ONGROUND)
		{
			pev->velocity *= 0.95f;
		}
	}

#ifdef _DEBUG
	Task Task_Materialized_PrintInfo() const noexcept
	{
		for (;; co_await 0.1f)
		{
			//g_engfuncs.pfnServerPrint(
			//	std::format("SOLID: {}\n", (int)pev->solid).c_str()
			//);
		}
	}

	Task Task_Posessed_PrintInfo() const noexcept
	{
		for (;; co_await 0.1f)
		{
			//auto const szWpnInfo = std::format(
			//	"pev->model: {}\n"
			//	"pev->effect: {:b}\n"
			//	"pev->body: {}\n"
			//	"\n",
			//	STRING(pev->model),
			//	pev->effects,
			//	pev->body
			//);
			//
			//g_engfuncs.pfnServerPrint(szWpnInfo.c_str());
		}

		co_return;
	}
#endif

	// Keep this one down. Or it will delete the dropped weapon like in HL1.
	qboolean AddDuplicate(CBasePlayerItem* pItem) noexcept final { return false; }
	qboolean ExtractAmmo(CBasePlayerWeapon*) noexcept final { return true; }
	qboolean ExtractClipAmmo(CBasePlayerWeapon* pWeapon) noexcept final { return true; }

	// Add the carrying ammo to player. Assume m_pPlayer is ready.
	qboolean AddWeapon(void) noexcept final
	{
		if (CRTP()->m_iDefaultAmmo > 0)
		{
			if (CRTP()->m_pPlayer->GiveAmmo(
				CRTP()->m_iDefaultAmmo,
				(char*)CRTP()->DAT_AMMUNITION->m_szName.data(),
				CRTP()->DAT_AMMUNITION->m_iMax) > 0
				)
			{
				CRTP()->m_iDefaultAmmo = 0;
				// Keep quiet now, or it will muff the sound for gun pickup.
				//g_engfuncs.pfnEmitSound(CRTP()->edict(), CHAN_ITEM, "items/9mmclip1.wav", VOL_NORM, ATTN_NORM, SND_FL_NONE, PITCH_NORM);
			}
		}

		return true;
	}

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

		if constexpr (requires { T::FLAG_DUAL_WIELDING; } || T::DAT_SLOT == 0)
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

#ifdef _DEBUG
			m_Scheduler.Enroll(Task_Posessed_PrintInfo(), TASK_DEBUG_MONITOR, true);
#endif
		}
	}

	qboolean AddToPlayer(CBasePlayer* pPlayer) noexcept override
	{
		m_pPlayer = pPlayer;

		if (!m_iPrimaryAmmoType)
		{
			m_iPrimaryAmmoType = CRTP()->DAT_AMMUNITION->m_iAmmoId;
			m_iSecondaryAmmoType = 0;
		}

		m_iId = PistolSlotMgr(pPlayer)->AllocSlot(
			CRTP()->DAT_HUD_NAME,
			CRTP()->m_iPrimaryAmmoType,
			CRTP()->DAT_AMMUNITION->m_iMax,
			CRTP()->DAT_ITEM_FLAGS
		);

		if (m_iId <= 0)	[[unlikely]]
		{
			assert(false);
			m_iId = CRTP()->PROTOTYPE_ID;
			return false;
		}

		// Deal out stored ammo.
		if (!CRTP()->AddWeapon())
		{
			return false;
		}

		gmsgWeapPickup::Send(pPlayer->edict(), m_iId);
		pPlayer->pev->weapons |= (1 << m_iId);

		return true;
	}

	void AttachToPlayer(CBasePlayer* pPlayer) noexcept override
	{
		pev->movetype = MOVETYPE_FOLLOW;
		pev->solid = SOLID_NOT;
		pev->aiment = pPlayer->edict();

		if (auto const pInfo = CRTP()->GetCombinedModelInfo())
		{
			if (pPlayer->m_pActiveItem != this)
			{
				g_engfuncs.pfnSetModel(CRTP()->edict(), pInfo->m_szModel.data());
				pev->body = pInfo->m_iBModelBodyVal;
				pev->sequence = pInfo->m_iSequence;
				pev->effects &= ~EF_NODRAW;
			}

			// otherwise it's already setup in Deploy().
		}
		else
		{
			// server won't send down to clients if modelindex == 0
			pev->modelindex = 0;
			pev->model = 0;
			pev->effects |= EF_NODRAW;
		}

		pev->owner = pPlayer->edict();

		// Remove think - prevents futher attempts to materialize
		// LUNA: disabled due to how task coroutine works in Prefab system.
		//pev->nextthink = 0;
		//SetThink(nullptr);

		SetTouch(nullptr);
		this->m_Scheduler.Delist(TASK_ALL_MATERIALIZED);
	}

	void UpdateThirdPersonModel(EWeaponTaskFlags2 iType) const noexcept
	{
		// Assume m_pPlayer is properly set.

		if constexpr (requires { T::FLAG_USING_OLD_PW; })
		{
			switch (iType)
			{
			case TASK_BEHAVIOR_DRAW:
			{
				m_pPlayer->pev->weaponmodel = MAKE_STRING(T::MODEL_P);
				this->pev->effects |= EF_NODRAW;	// Hide this entity and using standard P model.

				strcpy(m_pPlayer->m_szAnimExtention, T::ANIM_3RD_PERSON);
				*std::ranges::rbegin(m_pPlayer->m_szAnimExtention) = '\0';

				if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
				{
					if (m_pPlayer->HasShield())
					{
						m_pPlayer->pev->weaponmodel = MAKE_STRING(T::MODEL_P_SHIELD);
						strcpy(m_pPlayer->m_szAnimExtention, "shieldgun");
					}
				}

				break;
			}
			case TASK_BEHAVIOR_HOLSTER:
			{
				m_pPlayer->pev->weaponmodel = 0;
				this->pev->effects |= EF_NODRAW;

				break;
			}
			default: [[unlikely]]
				assert(false);
				break;
			}
		}
		else
		{
			auto const pInfo = CRTP()->GetCombinedModelInfo();
			assert(pInfo != nullptr);

			switch (iType)
			{
			case TASK_BEHAVIOR_DRAW:
			{
				m_pPlayer->pev->weaponmodel = 0;
				this->pev->effects &= ~EF_NODRAW;	// Hide standard P model and using MOVETYPE_FOLLOW and pev->aiment.

				g_engfuncs.pfnSetModel(CRTP()->edict(), pInfo->m_szModel.data());
				pev->body = pInfo->m_iPModelBodyVal;
				pev->sequence = pInfo->m_iSequence;

				strncpy(m_pPlayer->m_szAnimExtention, pInfo->m_szPlayerAnim.c_str(), sizeof(m_pPlayer->m_szAnimExtention) - 1);
				*std::ranges::rbegin(m_pPlayer->m_szAnimExtention) = '\0';

				if constexpr (requires { T::FLAG_CAN_HAVE_SHIELD; })
				{
					if (m_pPlayer->HasShield())
					{
						if (auto const pShieldInfo = ShieldGetWeaponInfo(CRTP()->NameOfThisWeapon());
							pShieldInfo && pShieldInfo->IsValid())
						{
							g_engfuncs.pfnSetModel(CRTP()->edict(), pShieldInfo->m_szModel.data());
							strncpy(m_pPlayer->m_szAnimExtention, pShieldInfo->m_szPlayerAnim.data(), pShieldInfo->m_szPlayerAnim.size());
							m_pPlayer->m_szAnimExtention[pShieldInfo->m_szPlayerAnim.size()] = '\0';

							pev->body = pShieldInfo->m_iBodyVal;
							pev->sequence = pShieldInfo->m_iSequence;
						}
					}
				}

				break;
			}
			case TASK_BEHAVIOR_HOLSTER:
			{
				m_pPlayer->pev->weaponmodel = 0;
				this->pev->effects &= ~EF_NODRAW;

				g_engfuncs.pfnSetModel(CRTP()->edict(), pInfo->m_szModel.data());
				pev->body = pInfo->m_iBModelBodyVal;
				pev->sequence = pInfo->m_iSequence;

				break;
			}
			default: [[unlikely]]
				assert(false);
				break;
			}
		}
	}

	void SetWorldModel() noexcept
	{
		if constexpr (requires { T::FLAG_USING_OLD_PW; })
			g_engfuncs.pfnSetModel(CRTP()->edict(), CRTP()->MODEL_W);
		else
		{
			auto const pInfo = CRTP()->GetCombinedModelInfo();
			g_engfuncs.pfnSetModel(CRTP()->edict(), pInfo->m_szModel.data());
			CRTP()->pev->angles = Angles::Upwards();
			CRTP()->pev->sequence = pInfo->m_iSequence;
			CRTP()->pev->body = pInfo->m_iWModelBodyVal;
		}
	}

public:

	[[nodiscard]] bool ShouldQcEventResume(int iAnimIndex) const noexcept
	{
		if (!m_pPlayer || !m_pPlayer->IsAlive())
			return false;

		if (m_pPlayer->m_pActiveItem != this)
			return false;

		if (m_pPlayer->pev->weaponanim != iAnimIndex)
			return false;

		return true;
	}

	auto ParseQcScript(mstudioevent_t const* pEvent, TranscriptedSequence const* pAnim) const noexcept -> std::expected<std::vector<std::string_view>, std::string_view>
	{
		auto EvOption = UTIL_SplitByBrackets(pEvent->options)
			.or_else([&](string_view s) noexcept -> std::expected<std::vector<std::string_view>, std::string_view> {
				g_engfuncs.pfnServerPrint(
					std::format("[WSIV] QC Script Err - {}: {}: {}\n",
						STRING(m_pPlayer->pev->viewmodel), pAnim->m_szLabel , s
					).c_str()
				);
				return std::unexpected(std::move(s));
			});

		return std::move(EvOption);
	}

	template <typename R>
	auto ExecuteQcScript(std::string_view szScript, TranscriptedSequence const* pAnim) const noexcept -> std::expected<R, std::string>
	{
		return RunDynExpr(szScript)
			.and_then([&](std::any a) noexcept -> std::expected<R, std::string> {
				try {
					return std::any_cast<R>(std::move(a));
				}
				catch (const std::exception& e) {
					return std::unexpected(e.what());
				}
			})
			.or_else([&](string s) noexcept -> std::expected<R, std::string> {
				g_engfuncs.pfnServerPrint(
					std::format("[WSIV] QC Script Err - {}: {}: {}\n",
						STRING(m_pPlayer->pev->viewmodel), pAnim->m_szLabel , s
					).c_str()
				);
				return std::unexpected(std::move(s));
			});
	}

	void DispatchQcEvents(TranscriptedSequence const* pAnim) noexcept
	{
		for (auto&& Event : pAnim->m_Events)
		{
			switch (Event.event)
			{
			case EQCEV_EF_MUZZLE_SMOKE:
			{
				m_Scheduler.Enroll(
					[](CBasePistol* pWeapon, CBasePlayer* pPlayer,
						mstudioevent_t const* pEvent, TranscriptedSequence const* pAnim
						) static noexcept -> Task
					{
						co_await (pEvent->frame / pAnim->m_fps);

						if (!pWeapon->ShouldQcEventResume(pAnim->m_index))
							co_return;

						// Expecting format: [VIEW_MODEL_ATTACHMENT] [PLAYER_MODEL_ATTACHMENT] [SPRITE_CANDIDATES]
						auto const EvOption = pWeapon->ParseQcScript(pEvent, pAnim);

						if (!EvOption || EvOption->size() < 3)
							co_return;

						auto const iViewModelAtt = UTIL_StrToNum<unsigned>(EvOption->at(0));
						auto const iPlayerAtt = UTIL_StrToNum<int>(EvOption->at(1));
						auto const& szGunSmoke = EvOption->at(2);
						auto const vecMuzOfs = UTIL_GetAttachmentOffset(
							STRING(pPlayer->pev->viewmodel),
							iViewModelAtt,
							pAnim->m_index,
							(float)pEvent->frame
						);

						CreateGunSmoke(pPlayer, vecMuzOfs, szGunSmoke, iPlayerAtt);

					}(this, m_pPlayer, &Event, pAnim),
					TASK_EQCEV_EF_MUZZLE_SMOKE
				);
				break;
			}
			case EQCEV_EF_EJECT_SHELL:
			{
				m_Scheduler.Enroll(
					[](CBasePistol* pWeapon, CBasePlayer* pPlayer,
						mstudioevent_t const* pEvent, TranscriptedSequence const* pAnim
						) static noexcept -> Task
					{
						co_await (pEvent->frame / pAnim->m_fps);

						if (!pWeapon->ShouldQcEventResume(pAnim->m_index))
							co_return;

						// Expecting format: [ATTACHMENT_NUM] [vec3(FWD, RIGHT, UP)] [MODEL] [SOUND]
						auto EvOption = pWeapon->ParseQcScript(pEvent, pAnim);

						if (!EvOption || EvOption->size() < 4)
							co_return;

						// Handle default val
						if (sv_icmp_t{}(EvOption->at(1), "default"))
							EvOption->at(1) = "vec3(25, -rand(85, 110), rand(85, 110))";

						auto const QcVeclocity = pWeapon->ExecuteQcScript<Vector>(EvOption->at(1), pAnim);
						if (!QcVeclocity)
							co_return;

						auto const iAttachment = UTIL_StrToNum<unsigned>(EvOption->at(0));	// arg0: attachment idx.
						auto const vecEjtPortOfs = UTIL_GetAttachmentOffset(
							STRING(pPlayer->pev->viewmodel),
							iAttachment,
							pAnim->m_index,
							(float)pEvent->frame
						);

						char szModel[64]{};
						auto const FmtRes = std::format_to_n(szModel, sizeof(szModel) - 1, "models/{}", EvOption->at(2));
						szModel[FmtRes.size] = '\0';

						auto const iSoundType = UTIL_StrToNum<TE_BOUNCE>(EvOption->at(3));

						pWeapon->EjectBrass(vecEjtPortOfs, *QcVeclocity, { szModel, (size_t)FmtRes.size }, iSoundType);

					}(this, m_pPlayer, &Event, pAnim),
					TASK_EQCEV_EF_EJECT_SHELL
				);
				break;
			}
			case EQCEV_EF_MUZZLE_LIGHT:
			{
				m_Scheduler.Enroll(
					[](CBasePistol* pWeapon, CBasePlayer* pPlayer,
						mstudioevent_t const* pEvent, TranscriptedSequence const* pAnim
						) static noexcept -> Task
					{
						// Expecting format: [PL_ATTACHMENT] [RADIUS] [vec3(R, G, B)] [LIFE] [DECAY]
						auto EvOption = pWeapon->ParseQcScript(pEvent, pAnim);

						// Handle default val.
						if (EvOption && EvOption->size() == 2 && sv_icmp_t{}(EvOption->at(1), "default"))
						{
							// UTIL_DLight(vecGunOrigin, 4.5f, { 255, 150, 15 }, 0.8f, 15.f);
							EvOption->at(1) = "4.5";
							EvOption->push_back("vec3(255, 150, 15)");
							EvOption->push_back("0.8");
							EvOption->push_back("15");
						}
						else if (!EvOption || EvOption->size() < 5)
							co_return;

						co_await (pEvent->frame / pAnim->m_fps);

						if (!pWeapon->ShouldQcEventResume(pAnim->m_index))
							co_return;

						auto const iPlayerAttIdx = UTIL_StrToNum<int>(EvOption->at(0));
						auto const flRadius = UTIL_StrToNum<float>(EvOption->at(1));
						auto const QcColor = pWeapon->ExecuteQcScript<Vector>(EvOption->at(2), pAnim);
						auto const flLife = UTIL_StrToNum<float>(EvOption->at(3));
						auto const flDecay = UTIL_StrToNum<float>(EvOption->at(4));

						if (!QcColor)
							co_return;

						Vector vecGunOrigin{};
						g_engfuncs.pfnGetAttachment(pPlayer->edict(), iPlayerAttIdx, vecGunOrigin, nullptr);

						UTIL_DLight(
							vecGunOrigin, flRadius,
							{ (uint8_t)std::lroundf(QcColor->x), (uint8_t)std::lroundf(QcColor->y), (uint8_t)std::lroundf(QcColor->z) },
							flLife, flDecay
						);

					}(this, m_pPlayer, &Event, pAnim),
					TASK_EQCEV_EF_MUZZLE_LIGHT
				);
				break;
			}

			case EQCEV_DT_CLIP:
			{
				m_Scheduler.Enroll(
					[](CBasePistol* pWeapon, CBasePlayer* pPlayer,
						mstudioevent_t const* pEvent, TranscriptedSequence const* pAnim
						) static noexcept -> Task
					{
						// Expecting format: [NEW_CLIP_NUM] [NEW_AMMO_NUM]
						auto EvOption = pWeapon->ParseQcScript(pEvent, pAnim);

						if (!EvOption || EvOption->empty())
							co_return;

						if (EvOption->size() == 1 && sv_icmp_t{}(EvOption->at(0), "default"))
							EvOption->at(0) = "-min(MAXCLIP - CLIP, AMMO)";

						co_await (pEvent->frame / pAnim->m_fps);

						if (!pWeapon->ShouldQcEventResume(pAnim->m_index) || pPlayer != pWeapon->m_pPlayer)
							co_return;

						DynExprBindNum("CLIP", pWeapon->m_iClip);
						DynExprBindNum("AMMO", pPlayer->m_rgAmmo[pWeapon->m_iPrimaryAmmoType]);
						DynExprBindNum("MAXCLIP", T::DAT_MAX_CLIP);

						if (EvOption->size() >= 2)
						{
							auto const QcNewClip = pWeapon->ExecuteQcScript<double>(EvOption->at(0), pAnim);
							auto const QcNewAmmo = pWeapon->ExecuteQcScript<double>(EvOption->at(1), pAnim);

							if (QcNewClip)
								pWeapon->m_iClip = std::lround(*QcNewClip);
							if (QcNewAmmo)
								pPlayer->m_rgAmmo[pWeapon->m_iPrimaryAmmoType] = std::lround(*QcNewAmmo);
						}
						else if (EvOption->size() >= 1)
						{
							auto const QcMagDiff = pWeapon->ExecuteQcScript<double>(EvOption->at(0), pAnim);

							if (QcMagDiff)
							{
								auto const iMagDiff = std::lround(*QcMagDiff);
								pWeapon->m_iClip -= iMagDiff;
								pPlayer->m_rgAmmo[pWeapon->m_iPrimaryAmmoType] += iMagDiff;
							}
						}

						DynExprUnbind("CLIP");
						DynExprUnbind("AMMO");
						DynExprUnbind("MAXCLIP");

					}(this, m_pPlayer, &Event, pAnim),
					TASK_EQCEV_DT_CLIP
				);
				break;

				break;
			}

			default:
				break;
			}
		}
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

	static constexpr auto NameOfThisWeapon() noexcept -> std::string_view
	{
		std::string_view const szClassname = T::CLASSNAME;
		if (auto const pos = szClassname.find_last_of('_'); pos != szClassname.npos)
			return szClassname.substr(pos + 1);

		return "";
	}

	static auto GetCombinedModelInfo() noexcept -> CCombinedModelInfo const*
	{
		static constexpr std::string_view szWeaponName = T::NameOfThisWeapon();
		if (auto const it = gCombinedModelInfo.find(szWeaponName); it != gCombinedModelInfo.cend())
			return std::addressof(it->second);

		return nullptr;
	}

public:
	static CPrefabWeapon* BuyWeapon(CBasePlayer* pPlayer) noexcept
	{
		if (!pPlayer->CanPlayerBuy(true))
			return nullptr;

		static constexpr bool bWpnCanPairWithShield = requires { T::FLAG_CAN_HAVE_SHIELD; };
		static constexpr bool bWpnHasTeam = requires { T::DAT_TEAM; };
		static constexpr bool bBanInAssassinationGame = requires { T::FLAG_BAN_IN_AS; };
		static constexpr bool bWpnHasCost = requires { T::DAT_COST; };

		if constexpr (!bWpnCanPairWithShield)
		{
			if (pPlayer->HasShield())
				return nullptr;
		}

		for (auto pWpn = pPlayer->m_rgpPlayerItems[T::DAT_SLOT + 1]; pWpn; pWpn = pWpn->m_pNext)
		{
			if (pWpn->pev->classname == MAKE_STRING(T::CLASSNAME))
			{
				gmsgTextMsg::Send(pPlayer->edict(), HUD_PRINTCENTER, "#Cstrike_Already_Own_Weapon");
				return nullptr;
			}
		}

		if constexpr (bBanInAssassinationGame)
		{
			if (g_pGameRules->m_iMapHasVIPSafetyZone != 0)
			{
				gmsgTextMsg::Send(pPlayer->edict(), HUD_PRINTCENTER, "#Cstrike_Not_Available");
				return nullptr;
			}
		}

		if constexpr (bWpnHasTeam)
		{
			if (pPlayer->m_iTeam != T::DAT_TEAM)
			{
				gmsgTextMsg::Unmanaged<MSG_ONE>(g_vecZero, pPlayer->edict(), HUD_PRINTCENTER, "#Alias_Not_Avail", T::NameOfThisWeapon().data());
				return nullptr;
			}
		}

		if constexpr (bWpnHasCost)
		{
			if (pPlayer->m_iAccount < T::DAT_COST)
			{
				gmsgTextMsg::Send(pPlayer->edict(), HUD_PRINTCENTER, "#Not_Enough_Money");
				gmsgBlinkAcct::Send(pPlayer->edict(), 2);
				return nullptr;
			}
		}

		// Drop other weapon

		if constexpr (!bWpnCanPairWithShield)
			pPlayer->DropShield();

		for (auto pWpn = static_cast<CBasePlayerWeapon*>(pPlayer->m_rgpPlayerItems[T::DAT_SLOT + 1]);
			pWpn;
			pWpn = static_cast<CBasePlayerWeapon*>(pWpn->m_pNext))
		{
			if constexpr (bWpnCanPairWithShield)
			{
				if (pPlayer->HasShield() && pPlayer->m_bShieldDrawn && pWpn == pPlayer->m_pActiveItem)
					pWpn->SecondaryAttack();
			}

			pPlayer->DropPlayerItem(STRING(pWpn->pev->classname));
		}

		// Give weapon

		auto const pEdict = g_engfuncs.pfnCreateNamedEntity(MAKE_STRING(T::CLASSNAME));

		if (pev_valid(pEdict) != EValidity::Full)
		{
			g_engfuncs.pfnAlertMessage(at_console, "NULL Ent in GiveNamedItemEx classname `%s`!\n", T::CLASSNAME);
			return nullptr;
		}

		pEdict->v.origin = pPlayer->pev->origin;
		pEdict->v.spawnflags |= SF_NORESPAWN;

		gpGamedllFuncs->dllapi_table->pfnSpawn(pEdict);
		gpGamedllFuncs->dllapi_table->pfnTouch(pEdict, pPlayer->edict());

		// not allow the item to fall to the ground.
		if (pev_valid(pEdict->v.owner) != EValidity::Full || pEdict->v.owner != pPlayer->edict())
		{
			pEdict->v.flags |= FL_KILLME;
			return nullptr;
		}

		if constexpr (bWpnHasCost)
			pPlayer->AddAccount(-T::DAT_COST);

		/* #TODO_PIW_TUTOR
		if (TheTutor)
		{
			TheTutor->OnEvent(EVENT_PLAYER_BOUGHT_SOMETHING, pPlayer);
		}
		*/

		return ent_cast<CPrefabWeapon*>(pEdict);
	}

private:
	__forceinline [[nodiscard]] T* CRTP() noexcept { static_assert(std::is_base_of_v<CBasePistol, T>); return static_cast<T*>(this); }
	__forceinline [[nodiscard]] T const* CRTP() const noexcept { static_assert(std::is_base_of_v<CBasePistol, T>); return static_cast<T const*>(this); }
};

inline constexpr CAmmoInfo Ammo_45ACP	{ .m_szName{"45ACP"},	.m_iAmmoId{6},	.m_iMax{100},	.m_iPenetrationPower{15},	.m_iPenetrationDistance{500},	.m_flCost{25.f / 12.f}, };
inline constexpr CAmmoInfo Ammo_57MM	{ .m_szName{"57MM"},	.m_iAmmoId{7},	.m_iMax{100},	.m_iPenetrationPower{30},	.m_iPenetrationDistance{2000},	.m_flCost{50.f / 50.f}, };
inline constexpr CAmmoInfo Ammo_50AE	{ .m_szName{"50AE"},	.m_iAmmoId{8},	.m_iMax{35},	.m_iPenetrationPower{30},	.m_iPenetrationDistance{1000},	.m_flCost{40.f / 7.f}, };
inline constexpr CAmmoInfo Ammo_357SIG	{ .m_szName{"357SIG"},	.m_iAmmoId{9},	.m_iMax{52},	.m_iPenetrationPower{25},	.m_iPenetrationDistance{800},	.m_flCost{50.f / 13.f}, };
inline constexpr CAmmoInfo Ammo_9MM		{ .m_szName{"9mm"},		.m_iAmmoId{10},	.m_iMax{120},	.m_iPenetrationPower{21},	.m_iPenetrationDistance{800},	.m_flCost{20.f / 30.f}, };

struct CPistolGlock : CBasePistol<CPistolGlock>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_GLOCK18;
	static inline constexpr char CLASSNAME[] = "weapon_glock18";

	static inline constexpr char MODEL_V[] = "models/v_glock18.mdl";
	static inline constexpr char MODEL_V_SHIELD[] = "models/shield/v_shield_glock18.mdl";

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
		static inline constexpr auto EXCLUSION = std::array{ "last"sv, "empty"sv, "burst"sv, };
	};
	struct AnimDat_ShootLast final {
		static inline constexpr std::array KEYWORD{ "shoot"sv, "fire"sv, };
		static inline constexpr auto INCLUSION = std::array{ "last"sv, "empty"sv, };
	};
	struct AnimDat_Draw final {
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, "select"sv, };
	};
	struct AnimDat_Reload final {
		static inline constexpr std::array KEYWORD{ "reload"sv, };
	};

	static inline constexpr auto DAT_ACCY_INIT = 0.9f;
	static inline constexpr auto DAT_ACCY_RANGE = std::pair{ 0.6f, 0.9f };
	static inline constexpr auto DAT_MAX_CLIP = 20;
	static inline constexpr auto DAT_MAX_SPEED = 250.f;
	static inline constexpr auto DAT_SHIELDED_SPEED = 180.f;
	static inline			auto DAT_AMMUNITION = &Ammo_9MM;
	static inline constexpr auto& DAT_HUD_NAME = CLASSNAME;
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

	static inline constexpr auto DAT_COST = 400;

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
		g_engfuncs.pfnEmitSound(m_pPlayer->edict(), CHAN_WEAPON,
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
	//static inline constexpr auto FLAG_USING_OLD_PW = true;	// Using w_glock.mdl, p_glock.mdl and requires "onehanded" anim.
	//static inline constexpr auto FLAG_BAN_IN_AS = true;	// Cannot buy in as_ map.
};

template void LINK_ENTITY_TO_CLASS<CPistolGlock>(entvars_t* pev) noexcept;
template CPrefabWeapon* BuyWeaponByWeaponClass<CPistolGlock>(CBasePlayer* pPlayer) noexcept;

struct CPistolUSP : CBasePistol<CPistolUSP>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_USP;
	static inline constexpr char CLASSNAME[] = "weapon_usp";

	static inline constexpr char MODEL_V[] = "models/v_usp.mdl";
	static inline constexpr char MODEL_V_SHIELD[] = "models/shield/v_shield_usp.mdl";

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
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, "select"sv, };
		static inline constexpr std::array KEYWORD_UNSIL{ "unsil"sv, };

		static inline constexpr auto EXCLUSION = UTIL_MergeArray(/*KEYWORD_SHIELD, */KEYWORD_UNSIL, std::array{ "shield"sv, });
	};
	struct AnimDat_Reload final {
		static inline constexpr std::array KEYWORD{ "reload"sv, };
		static inline constexpr std::array KEYWORD_UNSIL{ "unsil"sv, };

		static inline constexpr auto EXCLUSION = UTIL_MergeArray(/*KEYWORD_SHIELD, */KEYWORD_UNSIL, std::array{ "shield"sv, });
	};

	static inline constexpr auto DAT_ACCY_INIT = 0.92f;
	static inline constexpr auto DAT_ACCY_RANGE = std::pair{ 0.6f, 0.92f };
	static inline constexpr auto DAT_MAX_CLIP = 12;
	static inline constexpr auto DAT_MAX_SPEED = 250.f;
	static inline constexpr auto DAT_SHIELDED_SPEED = 180.f;
	static inline			auto DAT_AMMUNITION = &Ammo_45ACP;
	static inline constexpr auto& DAT_HUD_NAME = CLASSNAME;
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
	static inline constexpr auto DAT_COST = 500;

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
			g_engfuncs.pfnEmitSound(m_pPlayer->edict(), CHAN_WEAPON, UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(), VOL_NORM, ATTN_IDLE, SND_FL_NONE, 94 + UTIL_Random(0, 0xf));
		else
			g_engfuncs.pfnEmitSound(m_pPlayer->edict(), CHAN_WEAPON, UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(), VOL_NORM, ATTN_NORM, SND_FL_NONE, 87 + UTIL_Random(0, 0x12));
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
	//static inline constexpr auto FLAG_NO_ITEM_INFO = true;
	//static inline constexpr auto FLAG_DUAL_WIELDING = true;	// Emulate vanilla elites.
	static inline constexpr auto FLAG_SECATK_SILENCER = true;	// Emulate vanilla USP
	//bool m_bBurstFire{};										// A flag to emulate vanilla g18
};

template void LINK_ENTITY_TO_CLASS<CPistolUSP>(entvars_t* pev) noexcept;
template CPrefabWeapon* BuyWeaponByWeaponClass<CPistolUSP>(CBasePlayer* pPlayer) noexcept;

struct CPistolP228 : CBasePistol<CPistolP228>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_P228;
	static inline constexpr char CLASSNAME[] = "weapon_p228";

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
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, "select"sv, };
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
	static inline			auto DAT_AMMUNITION = &Ammo_357SIG;
	static inline constexpr auto& DAT_HUD_NAME = CLASSNAME;
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
	static inline constexpr auto DAT_COST = 600;

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
		g_engfuncs.pfnEmitSound(m_pPlayer->edict(), CHAN_WEAPON,
			UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(),
			VOL_NORM, ATTN_NORM, SND_FL_NONE, UTIL_Random(94, 94 + 0xf)
		);
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
};

template void LINK_ENTITY_TO_CLASS<CPistolP228>(entvars_t* pev) noexcept;
template CPrefabWeapon* BuyWeaponByWeaponClass<CPistolP228>(CBasePlayer* pPlayer) noexcept;

struct CPistolDeagle : CBasePistol<CPistolDeagle>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_DEAGLE;
	static inline constexpr char CLASSNAME[] = "weapon_deagle";

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
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, "select"sv, };
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
	static inline			auto DAT_AMMUNITION = &Ammo_50AE;
	static inline constexpr auto& DAT_HUD_NAME = CLASSNAME;
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
	static inline constexpr auto DAT_COST = 650;

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
		g_engfuncs.pfnEmitSound(m_pPlayer->edict(), CHAN_WEAPON,
			UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(),
			VOL_NORM, 0.6f, SND_FL_NONE, UTIL_Random(94, 94 + 0xf)
		);
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
};

template void LINK_ENTITY_TO_CLASS<CPistolDeagle>(entvars_t* pev) noexcept;
template CPrefabWeapon* BuyWeaponByWeaponClass<CPistolDeagle>(CBasePlayer* pPlayer) noexcept;

struct CPistolFN57 : CBasePistol<CPistolFN57>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_FIVESEVEN;
	static inline constexpr char CLASSNAME[] = "weapon_fiveseven";

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
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, "select"sv, };
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
	static inline			auto DAT_AMMUNITION = &Ammo_57MM;
	static inline constexpr auto& DAT_HUD_NAME = CLASSNAME;
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
	static inline constexpr auto DAT_COST = 750;

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
		g_engfuncs.pfnEmitSound(m_pPlayer->edict(), CHAN_WEAPON,
			UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(),
			VOL_NORM, ATTN_NORM, SND_FL_NONE, UTIL_Random(94, 94 + 0xf)
		);
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_CAN_HAVE_SHIELD = true;
};

template void LINK_ENTITY_TO_CLASS<CPistolFN57>(entvars_t* pev) noexcept;
template CPrefabWeapon* BuyWeaponByWeaponClass<CPistolFN57>(CBasePlayer* pPlayer) noexcept;

struct CPistolBeretta : CBasePistol<CPistolBeretta>
{
	static inline constexpr WeaponIdType PROTOTYPE_ID = WEAPON_ELITE;
	static inline constexpr char CLASSNAME[] = "weapon_elite";

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
		static inline constexpr std::array KEYWORD{ "draw"sv, "deploy"sv, "select"sv, };
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
	static inline			auto DAT_AMMUNITION = &Ammo_9MM;
	static inline constexpr auto& DAT_HUD_NAME = CLASSNAME;
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
	static inline constexpr auto DAT_COST = 800;

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
		g_engfuncs.pfnEmitSound(m_pPlayer->edict(), CHAN_WEAPON,
			UTIL_GetRandomOne(EXPR_FIRING_SND()).c_str(),
			VOL_NORM, ATTN_NORM, SND_FL_NONE, UTIL_Random(94, 94 + 0xf)
		);
	}

	static inline constexpr auto FLAG_IS_PISTOL = true;
	static inline constexpr auto FLAG_DUAL_WIELDING = true;
	static inline constexpr auto FLAG_USING_OLD_PW = true;
};

template void LINK_ENTITY_TO_CLASS<CPistolBeretta>(entvars_t* pev) noexcept;
template CPrefabWeapon* BuyWeaponByWeaponClass<CPistolBeretta>(CBasePlayer* pPlayer) noexcept;

void ClearNewWeapons() noexcept
{
	for (auto&& pEnt :
		Query::all_nonplayer_entities()
		| Query::as<CBasePlayerItem>()
		| std::views::filter([](auto&& p) static noexcept { return UTIL_IsLocalRtti(p) && p->m_pPlayer == nullptr; })
		)
	{
		pEnt->Kill();
	}
}
