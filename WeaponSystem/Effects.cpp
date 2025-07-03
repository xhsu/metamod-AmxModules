import std;
import hlsdk;

import UtlRandom;

import Prefab;
import Resources;
import Task;
import Sprite;


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
};

inline Resource::Add g_WallPuffs[] =
{
	"sprites/wall_puff1.spr",
	"sprites/wall_puff2.spr",
	"sprites/wall_puff3.spr",
	"sprites/wall_puff4.spr",
};

inline Resource::Add g_RifleSmokes[] =
{
	"sprites/rifle_smoke1.spr",
	"sprites/rifle_smoke2.spr",
	"sprites/rifle_smoke3.spr",
};

inline Resource::Add g_PistolSmokes[] =
{
	"sprites/pistol_smoke1.spr",
	"sprites/pistol_smoke2.spr",
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


struct CWallPuff : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "env_wall_puff";
	static inline constexpr double FPS = 30.0;

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

		auto const WallPuffSpr = UTIL_GetRandomOne(g_WallPuffs);
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


	CGunSmoke(CBasePlayer* pPlayer, bool bIsPistol) noexcept : m_pPlayer{ pPlayer }, m_bIsPistol{ bIsPistol } {}

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

		auto const& GunSmokeSpr = m_bIsPistol ? UTIL_GetRandomOne(g_PistolSmokes) : UTIL_GetRandomOne(g_RifleSmokes);
		g_engfuncs.pfnSetModel(edict(), GunSmokeSpr);

		g_engfuncs.pfnSetOrigin(edict(), pev->origin);

		m_Scheduler.Enroll(Task_SpritePlayOnce(pev, GoldSrc::SpriteInfo[GunSmokeSpr.m_pszName]->m_iNumOfFrames, FPS), TASK_ANIMATION);
		m_Scheduler.Enroll(Task_FadeOut(pev, 0.f, 1.f, 0.07f, UTIL_Random(0.65f, 0.85f)), TASK_FADE_OUT);
	}

	static CGunSmoke* Create(CBasePlayer* pPlayer, bool bIsPistol, bool bShootingLeft) noexcept
	{
		auto const [pEdict, pPrefab]
			= UTIL_CreateNamedPrefab<CGunSmoke>(pPlayer, bIsPistol);

		auto&& [fwd, right, up]
			= (pPlayer->pev->v_angle + pPlayer->pev->punchangle).AngleVectors();

		if (!bShootingLeft)
			pEdict->v.origin = pPlayer->pev->origin + pPlayer->pev->view_ofs + up * -9 + fwd * 32 + right * 8;
		else
			pEdict->v.origin = pPlayer->pev->origin + pPlayer->pev->view_ofs + up * -9 + fwd * 32 - right * 8;

		pPrefab->Spawn();
		pPrefab->pev->nextthink = 0.1f;

		return pPrefab;
	}

	CBasePlayer* m_pPlayer{};
	bool m_bIsPistol{};
};

edict_t* CreateGunSmoke(CBasePlayer* pPlayer, bool bIsPistol, bool bShootingLeft) noexcept
{
	return CGunSmoke::Create(pPlayer, bIsPistol, bShootingLeft)->edict();
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
		g_engfuncs.pfnSetOrigin(edict(), pev->origin);
		g_engfuncs.pfnSetSize(edict(), Vector::Zero(), Vector::Zero());

		m_Scheduler.Enroll(Task_Remove(pev, HOLD_TIME), TASK_TIME_OUT);
	}
};

edict_t* CreateSpark3D(TraceResult const& tr) noexcept
{
	return Prefab_t::Create<CSpark3D>(tr.vecEndPos, tr.vecPlaneNormal.VectorAngles())->edict();
}

struct CWaterSplash : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "env_water_splash_3d";
	static inline Resource::Add SPLASH_MODEL{ "models/WSIV/m_spark2.mdl" };
	static inline constexpr float FPS = 60;

	int m_iSkinAnimFrames{};
	int m_iBodyPartAnimFrames{ 1 };

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

		auto const pStudioInfo = Resource::GetStudioTranscription(SPLASH_MODEL);
		m_iSkinAnimFrames = std::ssize(pStudioInfo->m_Skins);
		for (auto&& BodyPart : pStudioInfo->m_Parts)
			m_iBodyPartAnimFrames *= BodyPart.m_SubModels.size();

		m_Scheduler.Enroll(Task_SkinAnimation(), TASK_TIME_OUT | TASK_ANIMATION);
		m_Scheduler.Enroll(Task_BodyPartAnimation(), TASK_TIME_OUT | TASK_ANIMATION);
	}

	Task Task_SkinAnimation() noexcept
	{
		for (int i = 0; i < m_iSkinAnimFrames; co_await (1.f / FPS), ++i)
		{
			pev->skin = i;
		}

		while (pev->body < m_iBodyPartAnimFrames)
			co_await 0.1f;

		pev->flags |= FL_KILLME;
	}

	Task Task_BodyPartAnimation() noexcept
	{
		for (int i = 0; i < m_iBodyPartAnimFrames; co_await (1.f / FPS), ++i)
		{
			pev->body = i;
		}

		while (pev->skin < m_iSkinAnimFrames)
			co_await 0.1f;

		pev->flags |= FL_KILLME;
	}
};

edict_t* CreateWaterSplash3D(Vector const& vecOrigin) noexcept
{
	return Prefab_t::Create<CWaterSplash>(vecOrigin, Angles::Upwards())->edict();
}
