import std;
import hlsdk;

import UtlRandom;
import UtlString;

import CBase;
import Message;
import Prefab;
import Resources;
import Server;
import Sprite;
import Studio;
import Task;
import WinAPI;


enum ETaskFlags : std::uint64_t
{
	TASK_ANIMATION			= (1ull << 0),
	TASK_FADE_OUT			= (1ull << 1),
	TASK_FADE_IN			= (1ull << 2),
	TASK_SCALING			= (1ull << 3),
	TASK_COLOR_DRIFT		= (1ull << 4),
	TASK_REFLECTING_FLAME	= (1ull << 5),
	TASK_FOLLOWING			= (1ull << 6),
	TASK_TIME_OUT			= (1ull << 7),
	TASK_ANIMATING_SKIN		= (1ull << 8),
	TASK_ANIMATING_BODY		= (1ull << 9),
};


static Task Task_SpritePlayOnce(entvars_t* const pev, short const FRAME_COUNT, double const FPS) noexcept
{
	short iFrame = (short)std::clamp(pev->frame, 0.f, float(FRAME_COUNT - 1));

	for (; iFrame < FRAME_COUNT;)
	{
		co_await float(1.f / FPS);

		pev->framerate = float(1.f / FPS);
		pev->frame = ++iFrame;
		pev->animtime = gpGlobals->time;
	}

	pev->flags |= FL_KILLME;
}

static Task Task_FadeOut(entvars_t* const pev, float const AWAIT, float const DECAY, float const ROLL, float const SCALE_INC) noexcept
{
	if (AWAIT > 0)
		co_await AWAIT;

	// Save the scale here to compatible with other behaviour.
	auto const flOriginalScale = pev->scale;

	for (auto flPercentage = (255.f - pev->renderamt) / 255.f;
		pev->renderamt > 0;
		flPercentage = (255.f - pev->renderamt) / 255.f)
	{
		co_await TaskScheduler::NextFrame::Rank.back();

		pev->renderamt -= DECAY;
		pev->angles.roll += ROLL;
		pev->scale = flOriginalScale * (1.f + flPercentage * SCALE_INC);	// fade out by zooming the SPR.
	}

	pev->flags |= FL_KILLME;
}

static Task Task_Remove(entvars_t* const pev, float const TIME) noexcept
{
	co_await TIME;

	pev->flags |= FL_KILLME;
}

static Task Task_TellMeWhere(CBasePlayer *pPlayer, Vector vecOfs) noexcept
{
	for (;; co_await 0.1f)
	{
		auto&& [fwd, right, up]
			= (pPlayer->pev->v_angle + pPlayer->pev->punchangle).AngleVectors();

		auto const vec =
			pPlayer->pev->origin + pPlayer->pev->view_ofs
			+ up * vecOfs.z + fwd * vecOfs.x + right * vecOfs.y;

		MsgBroadcast(SVC_TEMPENTITY);
		WriteData(TE_SPARKS);
		WriteData(vec);
		MsgEnd();
	}
}


struct CWallPuff : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "env_wall_puff";
	static inline constexpr double FPS = 30.0;
	static inline Resource::Add SPRITES[] =
	{
		"sprites/wall_puff1.spr",
		"sprites/wall_puff2.spr",
		"sprites/wall_puff3.spr",
		"sprites/wall_puff4.spr",
	};

	CWallPuff(TraceResult const& tr) noexcept : m_tr{ tr } {}

	void Spawn() noexcept override
	{
		auto const flLightness = UTIL_Random(1.0, 48.0);

		pev->rendermode = kRenderTransAdd;
		pev->renderamt = UTIL_Random(72.f, 96.f);	// Alpha?
		pev->rendercolor = Vector(0xD1, 0xC5, 0x9F);	// Color. Cannot be 0x000000
		pev->frame = 0;

		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NOCLIP;
		pev->gravity = 0;
		pev->scale = UTIL_Random(0.6f, 0.75f);

		auto const WallPuffSpr = UTIL_GetRandomOne(SPRITES);
		g_engfuncs.pfnSetModel(edict(), WallPuffSpr);

		Vector const vecDir = m_tr.vecPlaneNormal + CrossProduct(m_tr.vecPlaneNormal,
			(m_tr.vecPlaneNormal - Vector::Up()).LengthSquared() < std::numeric_limits<float>::epsilon() ? Vector::Front() : Vector::Up()	// #INVESTIGATE why will consteval fail here?
		);

		pev->velocity = vecDir.Normalize() * UTIL_Random(24.0, 48.0);

		g_engfuncs.pfnSetOrigin(edict(), m_tr.vecEndPos + m_tr.vecPlaneNormal * 24.0 * pev->scale);	// The actual SPR size will be 36 on radius. Clip the outter plain black part and it will be 24.

		m_Scheduler.Enroll(Task_SpritePlayOnce(pev, GoldSrc::SpriteInfo[WallPuffSpr.m_pszName]->m_iNumOfFrames, FPS), TASK_ANIMATION);
		m_Scheduler.Enroll(Task_FadeOut(pev, 0.f, 1.f, 0.07f, UTIL_Random(0.65f, 0.85f)), TASK_FADE_OUT);
	}

	static CWallPuff* Create(const TraceResult& tr) noexcept
	{
		auto const [pEdict, pPrefab]
			= UTIL_CreateNamedPrefab<CWallPuff>(tr);

		pEdict->v.origin = tr.vecEndPos;

		pPrefab->Spawn();
		pPrefab->pev->nextthink = 0.1f;

		return pPrefab;
	}

	TraceResult m_tr{};
};

edict_t* CreateWallPuff(TraceResult const& tr) noexcept
{
	return CWallPuff::Create(tr)->edict();
}

struct CGunSmoke : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "env_gun_smoke";
	static inline constexpr double FPS = 30.0;

	static inline Resource::Add RifleSmokes[] =
	{
		"sprites/rifle_smoke1.spr",
		"sprites/rifle_smoke2.spr",
		"sprites/rifle_smoke3.spr",
	};

	static inline Resource::Add PistolSmokes[] =
	{
		"sprites/pistol_smoke1.spr",
		"sprites/pistol_smoke2.spr",
	};

	CGunSmoke(CBasePlayer* pPlayer, Vector const& vecMuzzleOfs, bool bIsPistol, bool bShootingLeft) noexcept
		: m_pPlayer{ pPlayer }, m_bIsPistol{ bIsPistol },
		m_vecViewModelMuzzle{ vecMuzzleOfs }
	{
		g_engfuncs.pfnGetAttachment(pPlayer->edict(), bShootingLeft ? 1 : 0, m_vecWorldGunshotSpot, nullptr);
	}

	void Spawn() noexcept override
	{
		auto const flLightness = UTIL_Random(1.0, 48.0);

		pev->rendermode = kRenderTransAdd;
		pev->renderamt = UTIL_Random(48.f, 72.f);	// Alpha?
		pev->rendercolor = Vector(0xD1, 0xC5, 0x9F);	// Color. Cannot be 0x000000
		pev->frame = 0;

		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NOCLIP;
		pev->gravity = 0;
		pev->scale = UTIL_Random(0.6f, 0.75f);
		pev->owner = m_pPlayer->edict();

		auto const& GunSmokeSpr = m_bIsPistol ? UTIL_GetRandomOne(PistolSmokes) : UTIL_GetRandomOne(RifleSmokes);
		g_engfuncs.pfnSetModel(edict(), GunSmokeSpr);

		g_engfuncs.pfnSetOrigin(edict(), pev->origin);

		m_Scheduler.Enroll(Task_SpritePlayOnce(pev, GoldSrc::SpriteInfo[GunSmokeSpr.m_pszName]->m_iNumOfFrames, FPS), TASK_ANIMATION);
		m_Scheduler.Enroll(Task_FadeOut(pev, 0.f, 1.f, 0.07f, UTIL_Random(0.65f, 0.85f)), TASK_FADE_OUT);
		//m_Scheduler.Enroll(Task_TellMeWhere(m_pPlayer, m_vecViewModelMuzzle));
	}

	CBasePlayer* m_pPlayer{};
	bool m_bIsPistol{};
	Vector m_vecWorldGunshotSpot{};	// Attachment #0 - on player model.
	Vector m_vecViewModelMuzzle{};	// Attachment #0 - on view model.
};

edict_t* CreateGunSmoke(CBasePlayer* pPlayer, Vector const& vecMuzzleOfs, bool bIsPistol, bool bShootingLeft) noexcept
{
	auto const [pEdict, pPrefab]
		= UTIL_CreateNamedPrefab<CGunSmoke>(pPlayer, vecMuzzleOfs, bIsPistol, bShootingLeft);

	auto&& [fwd, right, up]
		= (pPlayer->pev->v_angle + pPlayer->pev->punchangle).AngleVectors();

	if (!bShootingLeft)
		pEdict->v.origin = pPlayer->pev->origin + pPlayer->pev->view_ofs + up * pPrefab->m_vecViewModelMuzzle.z + fwd * pPrefab->m_vecViewModelMuzzle.x + right * pPrefab->m_vecViewModelMuzzle.y;
	else
		pEdict->v.origin = pPlayer->pev->origin + pPlayer->pev->view_ofs + up * pPrefab->m_vecViewModelMuzzle.z + fwd * pPrefab->m_vecViewModelMuzzle.x - right * pPrefab->m_vecViewModelMuzzle.y;

	pPrefab->Spawn();
	pPrefab->pev->nextthink = 0.1f;

	return pEdict;
}

struct CSnowSteam : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "env_wall_puff";
	static inline constexpr double FPS = 30.0;
	static inline Resource::Add STEAM_SPRITE{ "sprites/WSIV/bettyspr5.spr" };

	CSnowSteam(TraceResult const& tr) noexcept : m_tr{ tr } {}

	void Spawn() noexcept override
	{
		auto const flLightness = UTIL_Random(1.0, 48.0);

		pev->rendermode = kRenderTransAdd;
		pev->renderamt = UTIL_Random(192.f, 255.f);	// Alpha?
		pev->rendercolor = Vector(0xFF, 0xFF, 0xFF);	// Color. Cannot be 0x000000
		pev->frame = 0;

		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NOCLIP;
		pev->gravity = 0;
		pev->scale = UTIL_Random(0.6f, 0.75f);

		g_engfuncs.pfnSetModel(edict(), STEAM_SPRITE);

		Vector const vecDir = m_tr.vecPlaneNormal + CrossProduct(m_tr.vecPlaneNormal,
			(m_tr.vecPlaneNormal - Vector::Up()).LengthSquared() < std::numeric_limits<float>::epsilon() ? Vector::Front() : Vector::Up()
		);

		pev->velocity = vecDir.Normalize() * UTIL_Random(24.0, 48.0);

		g_engfuncs.pfnSetOrigin(edict(), m_tr.vecEndPos + m_tr.vecPlaneNormal * 24.0 * pev->scale);

		m_Scheduler.Enroll(Task_SpritePlayOnce(pev, GoldSrc::SpriteInfo[STEAM_SPRITE]->m_iNumOfFrames, FPS), TASK_ANIMATION);
		m_Scheduler.Enroll(Task_FadeOut(pev, 0.f, 1.f, 0.07f, 0), TASK_FADE_OUT);
	}

	static CSnowSteam* Create(const TraceResult& tr) noexcept
	{
		auto const [pEdict, pPrefab]
			= UTIL_CreateNamedPrefab<CSnowSteam>(tr);

		pEdict->v.origin = tr.vecEndPos;

		pPrefab->Spawn();
		pPrefab->pev->nextthink = 0.1f;

		return pPrefab;
	}

	TraceResult m_tr{};
};

edict_t* CreateSnowSteam(TraceResult const& tr) noexcept
{
	return CSnowSteam::Create(tr)->edict();
}

struct CSpark3D : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "env_spark_3d";
	static inline Resource::Add SPARK_MODEL{ "models/WSIV/m_flash1.mdl" };
	static inline constexpr auto HOLD_TIME = 0.07f;

	void Spawn() noexcept override
	{
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NONE;
		pev->gravity = 0;
		pev->rendermode = kRenderTransAdd;
		pev->renderfx = kRenderFxNone;
		pev->renderamt = UTIL_Random(192.f, 255.f);

		auto const iValue = UTIL_Random(0, 4);
		switch (iValue)
		{
		case 0:
		case 1:
		case 2:
		case 3:
			pev->body = 0;
			pev->skin = iValue;
			break;

		case 4:
			pev->body = 1;
			break;

		default:
			std::unreachable();
		}

		g_engfuncs.pfnSetModel(edict(), SPARK_MODEL);
		g_engfuncs.pfnSetSize(edict(), Vector::Zero(), Vector::Zero());
		g_engfuncs.pfnSetOrigin(edict(), pev->origin);

		m_Scheduler.Enroll(Task_Remove(pev, HOLD_TIME), TASK_TIME_OUT);
	}
};

edict_t* CreateSpark3D(TraceResult const& tr) noexcept
{
	return Prefab_t::Create<CSpark3D>(tr.vecEndPos, tr.vecPlaneNormal.VectorAngles())->edict();
}

static Resource::Add g_SnowSplashModel{ "models/WSIV/m_spark1.mdl" };

edict_t* CreateSnowSplash(TraceResult const& tr) noexcept
{
	auto const pSplashEnt = Prefab_t::Create<CSpark3D>(tr.vecEndPos, tr.vecPlaneNormal.VectorAngles());

	g_engfuncs.pfnSetModel(pSplashEnt->edict(), g_SnowSplashModel);
	pSplashEnt->pev->body = UTIL_Random(0, 3);
	pSplashEnt->pev->skin = UTIL_Random(0, 3);
	pSplashEnt->pev->renderamt = UTIL_Random(32.f, 64.f);

	pSplashEnt->m_Scheduler.Enroll(Task_Remove(pSplashEnt->pev, 0.05f), TASK_TIME_OUT, true);

	return pSplashEnt->edict();
}

struct CWaterSplash : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "env_water_splash_3d";
	static inline Resource::Add SPLASH_MODEL{ "models/WSIV/m_spark2.mdl" };

	struct animating_bodypart_t
	{
		short m_iGroupIndex{};
		short m_iSubModelsCount{};
		float m_flInterval{};
	};

	static inline int m_iSkinAnimFrames{};
	static inline std::vector<animating_bodypart_t> m_rgAnimatingBodyGroups{};

	void Spawn() noexcept override
	{
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NONE;
		pev->gravity = 0;
		pev->rendermode = kRenderTransAdd;
		pev->renderfx = kRenderFxNone;
		pev->renderamt = UTIL_Random(192.f, 255.f);

		g_engfuncs.pfnSetModel(edict(), SPLASH_MODEL);
		g_engfuncs.pfnSetOrigin(edict(), pev->origin);
		g_engfuncs.pfnSetSize(edict(), Vector::Zero(), Vector::Zero());

		Precache();

		m_Scheduler.Enroll(Task_SkinAnimation(60.f), TASK_TIME_OUT | TASK_ANIMATION | TASK_ANIMATING_SKIN);

		for (auto&& info : m_rgAnimatingBodyGroups)
			m_Scheduler.Enroll(Task_BodyPartAnimation(info), TASK_ANIMATION | TASK_ANIMATING_BODY);
	}

	void Precache() noexcept override
	{
		static bool bPrecached = false;
		if (bPrecached) [[likely]]
			return;

		auto const pStudioInfo = Resource::GetStudioTranscription(SPLASH_MODEL);
		m_iSkinAnimFrames = std::ssize(pStudioInfo->m_Skins);

		for (int i = 0; i < std::ssize(pStudioInfo->m_Parts); ++i)
		{
			std::string_view const szName{ pStudioInfo->m_Parts[i].m_szName};
			if (!szName.starts_with("animated_"))
				continue;

			auto const pos = szName.find_first_of('_');
			if (pos == szName.npos)
				continue;

			auto const szNum = szName.substr(pos + 1);

			m_rgAnimatingBodyGroups.emplace_back(
				(short)i,
				(short)pStudioInfo->m_Parts[i].m_SubModels.size(),
				1.f / UTIL_StrToNum<float>(szNum)
			);
		}
	}

	Task Task_SkinAnimation(float FPS) noexcept
	{
		for (int i = 0; i < m_iSkinAnimFrames; co_await (1.f / FPS), ++i)
		{
			pev->skin = i;
		}

		while (m_Scheduler.Exist(TASK_ANIMATING_BODY))
			co_await 0.1f;

		pev->flags |= FL_KILLME;
	}

	Task Task_BodyPartAnimation(animating_bodypart_t info) noexcept
	{
		for (int i = 0; i < info.m_iSubModelsCount; co_await info.m_flInterval, ++i)
		{
			SetBodygroup(pev, info.m_iGroupIndex, i);
		}
	}
};

edict_t* CreateWaterSplash3D(Vector const& vecOrigin) noexcept
{
	return Prefab_t::Create<CWaterSplash>(vecOrigin, Angles::Upwards())->edict();
}

// Forwards

void Effect_AddToFullPack_Post(entity_state_t* pState, edict_t* pEdict, edict_t* pClientSendTo, bool bIsPlayer) noexcept
{
	if (bIsPlayer || !UTIL_IsLocalRtti(pEdict->pvPrivateData)) [[likely]]
		return;

	auto const pEntity = ent_cast<CBaseEntity*>(pEdict);
	auto const pGunSmoke = dynamic_cast<CGunSmoke*>(pEntity);

	if (pGunSmoke == nullptr)
		return;

	auto const pOwnerEdict = pGunSmoke->m_pPlayer->edict();
	if (pClientSendTo != pOwnerEdict
		|| (pClientSendTo == pOwnerEdict && !UTIL_IsFirstPersonal(pOwnerEdict)))
	{
		pState->origin = pGunSmoke->m_vecWorldGunshotSpot;
	}
}
