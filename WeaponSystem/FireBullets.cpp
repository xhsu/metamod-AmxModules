#include <assert.h>

#ifdef __INTELLISENSE__
import std;
#else
import std.compat;	// #MSVC_BUG_STDCOMPAT
#endif
import hlsdk;

import UtlRandom;

import CBase;
import ConditionZero;
import Decal;
import Message;
import Resources;
import Task;
import Uranus;
import ZBot;

import Ammo;


#pragma region EFFECT_RES

//inline Resource::Add GIBS_BRICK{ "models/gibs_brick.mdl" };	// LUNA: DS using this for ... Slosh??
inline Resource::Add GIBS_CONCRETE{ "models/gibs_concrete.mdl" };
inline Resource::Add GIBS_FLORA{ "models/gibs_flora.mdl" };
inline Resource::Add GIBS_GLASS{ "models/gibs_glass.mdl" };
inline Resource::Add GIBS_WOOD{ "models/gibs_wood.mdl" };

inline Resource::Add SPR_Snowflake{ "sprites/effects/snowflake.spr" };

inline Resource::Add SFX_HIT_METAL[] =
{
	"player/pl_metal1.wav",
	"player/pl_metal2.wav",
	"player/pl_metal3.wav",
	"player/pl_metal4.wav",
};

inline Resource::Add SFX_HIT_DIRT[] =
{
	"player/pl_dirt1.wav",
	"player/pl_dirt2.wav",
	"player/pl_dirt3.wav",
	"player/pl_dirt4.wav",
};

inline Resource::Add SFX_HIT_VENT[] =
{
	"player/pl_duct1.wav",
	"player/pl_duct2.wav",
	"player/pl_duct3.wav",
	"player/pl_duct4.wav",
};

inline Resource::Add SFX_HIT_GRATE[] =
{
	"player/pl_grate1.wav",
	"player/pl_grate2.wav",
	"player/pl_grate3.wav",
	"player/pl_grate4.wav",
};

inline Resource::Add SFX_HIT_TILE[] =
{
	"player/pl_tile1.wav",
	"player/pl_tile2.wav",
	"player/pl_tile3.wav",
	"player/pl_tile4.wav",
	"player/pl_tile5.wav",
};

inline Resource::Add SFX_HIT_SLOSH[] =
{
	"player/pl_slosh1.wav",
	"player/pl_slosh2.wav",
	"player/pl_slosh3.wav",
	"player/pl_slosh4.wav",
};

inline Resource::Add SFX_HIT_SNOW[] =
{
	"player/pl_snow1.wav",
	"player/pl_snow2.wav",
	"player/pl_snow3.wav",
	"player/pl_snow4.wav",
	"player/pl_snow5.wav",
	"player/pl_snow6.wav",
};

inline Resource::Add SFX_HIT_WOOD[] =
{
	"player/pl_wood1.wav",
	"player/pl_wood2.wav",
	"player/pl_wood3.wav",
	"player/pl_wood4.wav",
};

inline Resource::Add SFX_HIT_GLASS[] =
{
	"debris/glass1.wav",
	"debris/glass2.wav",
	"debris/glass3.wav",
	"debris/glass4.wav",
};

inline Resource::Add SFX_HIT_CARPET[] =
{
	"player/pl_carpet1.wav",
	"player/pl_carpet2.wav",
	"player/pl_carpet3.wav",
	"player/pl_carpet4.wav",
};

inline Resource::Add SFX_HIT_GRASS[] =
{
	"player/pl_grass1.wav",
	"player/pl_grass2.wav",
	"player/pl_grass3.wav",
	"player/pl_grass4.wav",
};

inline Resource::Add SFX_HIT_GRAVEL[] =
{
	"player/pl_gravel1.wav",
	"player/pl_gravel2.wav",
	"player/pl_gravel3.wav",
	"player/pl_gravel4.wav",
};

inline Resource::Add SFX_RICO_GENERIC[] =
{
	"weapons/ric1.wav",
	"weapons/ric2.wav",
	"weapons/ric3.wav",
	"weapons/ric4.wav",
	"weapons/ric5.wav",
	"weapons/ric_conc-1.wav",
	"weapons/ric_conc-2.wav",
};

inline Resource::Add SFX_RICO_METAL[] =
{
	"weapons/ric_metal-1.wav",
	"weapons/ric_metal-2.wav",
	"debris/r_metal3.wav",
	"debris/r_metal4.wav",
};

#pragma endregion EFFECT_RES

// Effect.cpp
extern edict_t* CreateWallPuff(TraceResult const& tr) noexcept;
extern edict_t* CreateSnowSteam(TraceResult const& tr);
extern edict_t* CreateSpark3D(TraceResult const& tr) noexcept;
extern edict_t* CreateSnowSplash(TraceResult const& tr) noexcept;
extern edict_t* CreateWaterSplash3D(Vector const& vecOrigin) noexcept;
//

static Task VFX_WaterSplash(Vector vecSrc, Vector vecEnd) noexcept
{
	// Assume that player doesn't shooting from water,
	// not shooting through water
	// and not that close to the water.

	assert(g_engfuncs.pfnPointContents(vecEnd) == CONTENTS_WATER);

	auto vecDir = vecEnd - vecSrc;
	auto iCounter = 0z;

	for (;
		g_engfuncs.pfnPointContents(vecSrc) != g_engfuncs.pfnPointContents(vecEnd);
		co_await TaskScheduler::NextFrame::Rank[0])
	{
		vecSrc += vecDir * 0.5;
		vecDir *= 0.5;

		if (g_engfuncs.pfnPointContents(vecSrc) == CONTENTS_WATER)
			break;

		++iCounter;
	}

	auto iCounter2 = 0z;

	for (vecEnd = vecSrc - vecDir;	// In the first iteration we are certain that we should go back, and vecSrc is under water.
		vecDir.LengthSquared() > 4.0 * 4.0;
		co_await TaskScheduler::NextFrame::Rank[0], ++iCounter2)
	{
		vecDir = (vecEnd - vecSrc) * 0.5;
		auto const vecMid = vecSrc + vecDir;

		auto const C_SRC = g_engfuncs.pfnPointContents(vecSrc);
		auto const C_MID = g_engfuncs.pfnPointContents(vecMid);
		auto const C_END = g_engfuncs.pfnPointContents(vecEnd);

		if (C_SRC == C_MID && C_MID == C_END)
			co_return;	// BAD

		if (C_SRC == C_MID && C_MID != C_END)
		{
			vecSrc = vecMid;
		}
		else if (C_SRC != C_MID && C_MID == C_END)
		{
			vecEnd = vecMid;
		}
		else
			co_return;	// BAD
	}

	CreateWaterSplash3D(
		g_engfuncs.pfnPointContents(vecSrc) == CONTENTS_WATER ?
		vecSrc : vecEnd
	);

	co_return;
}

static Task VFX_BulletImpact(Vector vecSrc, TraceResult tr, char cTextureType, float flDamage) noexcept
{
	[[maybe_unused]] auto const vecDir = (tr.vecEndPos - vecSrc).Normalize();

	// tr.fInWater doesn't work.
	if (g_engfuncs.pfnPointContents(tr.vecEndPos) == CONTENTS_WATER)
		TaskScheduler::Enroll(VFX_WaterSplash(vecSrc, tr.vecEndPos));

	co_await TaskScheduler::NextFrame::Rank[0];

	UTIL_Decal(tr.pHit, tr.vecEndPos, UTIL_GetRandomOne(Decal::GUNSHOT));

	co_await TaskScheduler::NextFrame::Rank[0];

	// Wall impact particles.
	switch (cTextureType)
	{
	case CHAR_TEX_FLESH:
	case CHAR_TEX_SLOSH:
		// Absolutely no effect on these two.
		break;

	case CHAR_TEX_METAL:
	case CHAR_TEX_VENT:
	case CHAR_TEX_GRATE:
	case CHAR_TEX_COMPUTER:
		CreateSpark3D(tr);
		UTIL_DLight(tr.vecEndPos, 1.f, { 255, 120, 100, }, 0.1f, 0);

		MsgBroadcast(SVC_TEMPENTITY);
		WriteData(TE_STREAK_SPLASH);
		WriteData(tr.vecEndPos);
		WriteData(tr.vecPlaneNormal);	// dir
		WriteData((uint8_t)10);	// color
		WriteData(UTIL_Random<uint16_t>(72, 96));	// count
		WriteData((uint16_t)std::clamp(flDamage * 2.f, 120.f, 240.f));	// base speed
		WriteData(uint16_t(std::clamp(flDamage * 2.f, 120.f, 240.f) * 0.75f));	// random velocity
		MsgEnd();

		break;

	case CHAR_TEX_WOOD:
		UTIL_BreakModel(
			tr.vecEndPos, Vector(1, 1, 1) /* Invalid Arg? */, tr.vecPlaneNormal * flDamage * 2.f,
			UTIL_Random(0.8f, 1.2f),
			GIBS_WOOD,
			(uint8_t)std::clamp(std::lroundf(flDamage / 4.f), 2l, 16l),
			UTIL_Random(6.f, 9.f),
			BREAK_WOOD
		);

		CreateWallPuff(tr);
		break;

	case CHAR_TEX_CONCRETE:
	case CHAR_TEX_DIRT:
	case CHAR_TEX_TILE:
	case CHAR_TEX_GRAVEL:
		UTIL_BreakModel(
			tr.vecEndPos, Vector(0.1, 0.1, 0.1) /* Invalid Arg? */, tr.vecPlaneNormal * flDamage * 2.f,
			UTIL_Random(0.8f, 1.2f),
			GIBS_CONCRETE,
			(uint8_t)std::clamp(std::lroundf(flDamage / 4.f), 2l, 16l),
			UTIL_Random(6.f, 9.f),
			BREAK_NONE
		);
		// 1 << 0 - glass
		// 1 << 1 - metal
		// 3 - none?
		// 1 << 2 - none?
		// 5 - none
		// 6 - none
		// 7 - none
		// 1 << 3 - wood
		// 9 - none

		CreateWallPuff(tr);
		break;

	case CHAR_TEX_GLASS:
		UTIL_BreakModel(
			tr.vecEndPos, Vector(1, 1, 1) /* Invalid Arg? */, tr.vecPlaneNormal * flDamage * 2.f,
			UTIL_Random(0.8f, 1.2f),
			GIBS_GLASS,
			(uint8_t)std::clamp(std::lroundf(flDamage / 4.f), 2l, 16l),
			UTIL_Random(6.f, 9.f),
			BREAK_GLASS | BREAK_TRANS
		);

		CreateWallPuff(tr);
		break;

	case CHAR_TEX_GRASS_CS:
	case CHAR_TEX_GRASS_CZ:
		UTIL_BreakModel(
			tr.vecEndPos, Vector(1, 1, 1) /* Invalid Arg? */, tr.vecPlaneNormal * flDamage * 2.f,
			UTIL_Random(0.8f, 1.2f),
			GIBS_FLORA,
			(uint8_t)std::clamp(std::lroundf(flDamage / 4.f), 2l, 16l),
			UTIL_Random(6.f, 9.f),
			BREAK_NONE
		);
		break;

	case CHAR_TEX_SNOW:
	{
		Vector const vecFlakeDir {
			(tr.vecEndPos + tr.vecPlaneNormal).Make2D() + vecDir.Make2D(),
			tr.vecEndPos.z + tr.vecPlaneNormal.z
		};

		MsgPVS(SVC_TEMPENTITY, tr.vecEndPos);
		WriteData(TE_SPRITETRAIL);
		WriteData(tr.vecEndPos);

		if (std::fabs(tr.vecPlaneNormal.z) > 1e-5)
			WriteData(vecFlakeDir);
		else [[unlikely]]
			WriteData(tr.vecEndPos + tr.vecPlaneNormal);

		WriteData((uint16_t)SPR_Snowflake);
		WriteData((uint8_t)std::clamp(std::lroundf(flDamage / 4.f), 2l, 16l));
		WriteData((uint8_t)6);	// Life, in 0.1', doesn't work??
		WriteData((uint8_t)1);	// Size
		WriteData((uint8_t)std::clamp(std::lroundf(flDamage / 2.f), 0l, 255l));
		WriteData((uint8_t)18);
		MsgEnd();

		CreateSnowSplash(tr);
		CreateSnowSteam(tr);

		//UTIL_DLight(tr.vecEndPos, 1.f, { 255, 255, 255, }, 0.1f, 0);

		break;
	}

	default:
		CreateWallPuff(tr);
		break;
	}

	co_await TaskScheduler::NextFrame::Rank[0];

	// Bullet impact SFX
	switch (cTextureType)
	{
	case CHAR_TEX_METAL:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_METAL),
			0.9f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_DIRT:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_DIRT),
			0.9f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_VENT:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_VENT),
			0.5f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_GRATE:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_GRATE),
			0.9f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_TILE:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_TILE),
			0.8f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_SLOSH:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_SLOSH),
			0.9f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_SNOW:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_SNOW),
			0.7f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_WOOD:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_WOOD),
			0.9f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_COMPUTER:
	case CHAR_TEX_GLASS:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_GLASS),
			0.8f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

		// Extentions by CZDS
	case CHAR_TEX_CARPET:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_CARPET),
			1.f, ATTN_STATIC, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_GRASS_CS:
	case CHAR_TEX_GRASS_CZ:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_GRASS),
			1.f, ATTN_STATIC, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_GRAVEL:
		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_GRAVEL),
			0.9f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);
		break;

	case CHAR_TEX_FLESH:
		// Should we add helmet and vest and flesh hurt sound here?

	default:
		break;
	}

	// ricochet sfx
	if (UTIL_Random())
	{
		switch (cTextureType)
		{
		case CHAR_TEX_METAL:
		case CHAR_TEX_GRATE:
		case CHAR_TEX_VENT:
		case CHAR_TEX_COMPUTER:
			g_engfuncs.pfnEmitAmbientSound(
				ent_cast<edict_t*>(0), tr.vecEndPos,
				UTIL_GetRandomOne(SFX_RICO_METAL),
				VOL_NORM, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
			);
			break;

			// Soft surfaces.
		case CHAR_TEX_FLESH:
		case CHAR_TEX_SLOSH:
		case CHAR_TEX_SNOW:
			break;

		default:
			g_engfuncs.pfnEmitAmbientSound(
				ent_cast<edict_t*>(0), tr.vecEndPos,
				UTIL_GetRandomOne(SFX_RICO_GENERIC),
				VOL_NORM, ATTN_STATIC, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
			);
			break;
		}
	}

	co_await TaskScheduler::NextFrame::Rank[0];

	// Tracer effect.
	MsgBroadcast(SVC_TEMPENTITY);
	WriteData(TE_TRACER);
	WriteData(vecSrc);
	WriteData(tr.vecEndPos);
	MsgEnd();
}

// Go to the trouble of combining multiple pellets into a single damage call.
// This version is used by Players, uses the random seed generator to sync client and server side shots.
Vector2D CS_FireBullets3(
	CBasePlayer* pAttacker, CBasePlayerItem* pInflictor,
	Vector const& vecSrcOfs, float flSpread, float flDistance,
	int iPenetration, CAmmoInfo const* pAmmoInfo, float flDamage, float flRangeModifier) noexcept
{
	[[maybe_unused]] auto const iOriginalPenetration{ iPenetration };
	float flPenetrationPower{ (float)pAmmoInfo->m_iPenetrationPower };
	float const flPenetrationDistance{ (float)pAmmoInfo->m_iPenetrationDistance };
	auto flCurDmg{ flDamage };
	float flCurrentDistance{};
	TraceResult tr{};

	auto const [vecForward, vecRight, vecUp]
		= (pAttacker->pev->v_angle + pAttacker->pev->punchangle).AngleVectors();

	// Penetration data was moved into struct CAmmoInfo

	gpMultiDamage->type = (DMG_BULLET | DMG_NEVERGIB);

	float x{}, y{}, z{};

	if (pAttacker->IsPlayer())	[[likely]]
	{
		// Use player's random seed.
		// get circular gaussian spread
		x = Uranus::UTIL_SharedRandomFloat{}(pAttacker->random_seed, -0.5, 0.5) + Uranus::UTIL_SharedRandomFloat{}(pAttacker->random_seed + 1, -0.5, 0.5);
		y = Uranus::UTIL_SharedRandomFloat{}(pAttacker->random_seed + 2, -0.5, 0.5) + Uranus::UTIL_SharedRandomFloat{}(pAttacker->random_seed + 3, -0.5, 0.5);
	}
	else
	{
		do
		{
			x = UTIL_Random(-0.5f, 0.5f) + UTIL_Random(-0.5f, 0.5f);
			y = UTIL_Random(-0.5f, 0.5f) + UTIL_Random(-0.5f, 0.5f);
			z = x * x + y * y;
		} while (z > 1);
	}

	auto vecSrc = pAttacker->GetGunPosition();
	auto const vecDir = vecForward + x * flSpread * vecRight + y * flSpread * vecUp;
	auto vecEnd = vecSrc + vecDir * flDistance;

	float flDamageModifier = 0.5f;

	while (iPenetration != 0)
	{
		Uranus::ClearMultiDamage{}();
		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, dont_ignore_glass | dont_ignore_monsters, pAttacker->edict(), &tr);

		if (ZBot::Manager() && tr.flFraction != 1.0f)
			ZBot::Manager()->OnEvent(EVENT_BULLET_IMPACT, pAttacker, (CBaseEntity*)&tr.vecEndPos);

		char cTextureType = Uranus::UTIL_TextureHit{}(&tr, vecSrc, vecEnd);
		[[maybe_unused]] bool bSparks{}, bHitMetal{};

		switch (cTextureType)
		{
		case CHAR_TEX_METAL:
			bHitMetal = true;
			bSparks = true;

			flPenetrationPower *= 0.15f;
			flDamageModifier = 0.2f;
			break;

		case CHAR_TEX_CONCRETE:
			flPenetrationPower *= 0.25f;
			break;

		case CHAR_TEX_GRATE:
			bHitMetal = true;
			bSparks = true;

			flPenetrationPower *= 0.5f;
			flDamageModifier = 0.4f;
			break;

		case CHAR_TEX_VENT:
			bHitMetal = true;
			bSparks = true;

			flPenetrationPower *= 0.5f;
			flDamageModifier = 0.45f;
			break;

		case CHAR_TEX_TILE:
			flPenetrationPower *= 0.65f;
			flDamageModifier = 0.3f;
			break;

		case CHAR_TEX_COMPUTER:
			bHitMetal = true;
			bSparks = true;

			flPenetrationPower *= 0.4f;
			flDamageModifier = 0.45f;
			break;

		case CHAR_TEX_WOOD:
			flDamageModifier = 0.6f;
			break;

		default:
			break;
		}

		if (tr.flFraction != 1.0f)
		{
			CBaseEntity* pEntity = ent_cast<CBaseEntity*>(tr.pHit);
			--iPenetration;

			flCurrentDistance = tr.flFraction * flDistance;
			flCurDmg *= std::powf(flRangeModifier, flCurrentDistance / 500.f);

			if (flCurrentDistance > flPenetrationDistance)
			{
				iPenetration = 0;
			}

			if (tr.iHitgroup == HITGROUP_SHIELD)
			{
				g_engfuncs.pfnEmitSound(
					pEntity->edict(),
					CHAN_VOICE, UTIL_Random() ? "weapons/ric_metal-1.wav" : "weapons/ric_metal-2.wav",
					VOL_NORM, ATTN_NORM, SND_FL_NONE, PITCH_NORM
				);
				
				MsgPVS(SVC_TEMPENTITY, tr.vecEndPos);
				WriteData(TE_SPARKS);
				WriteData(tr.vecEndPos);
				MsgEnd();

				pEntity->pev->punchangle.pitch = flCurDmg * UTIL_Random(-0.15f, 0.15f);
				pEntity->pev->punchangle.roll = std::clamp(flCurDmg * UTIL_Random(-0.15f, 0.15f), -5.f, 5.f);

				if (pEntity->pev->punchangle.pitch < 4)
					pEntity->pev->punchangle.pitch = -4;	// Weird, but that's it according to IDA.

				break;
			}

			float flDistanceModifier{};
			if (tr.pHit->v.solid != SOLID_BSP || !iPenetration)
			{
				flPenetrationPower = 42;
				flDamageModifier = 0.75f;
				flDistanceModifier = 0.75f;
			}
			else
				flDistanceModifier = 0.5f;

			TaskScheduler::Enroll(VFX_BulletImpact(vecSrc, tr, cTextureType, flCurDmg));

			[[maybe_unused]] auto const vecPrevSrc{ vecSrc }, vecPrevEndpos{ tr.vecEndPos - vecDir * flPenetrationPower };
			vecSrc = tr.vecEndPos + (vecDir * flPenetrationPower);
			flDistance = (flDistance - flCurrentDistance) * flDistanceModifier;
			vecEnd = vecSrc + (vecDir * flDistance);

			pEntity->TraceAttack(pAttacker->pev, flCurDmg, vecDir, &tr, (DMG_BULLET | DMG_NEVERGIB));
			flCurDmg *= flDamageModifier;

			// Trace back, such that we can place a bullet hole on the opposite side of the wall as well.
			if (iPenetration != 0)
			{
				g_engfuncs.pfnTraceLine(vecPrevEndpos, vecPrevSrc, dont_ignore_glass | dont_ignore_monsters, pAttacker->edict(), &tr);
				if (!tr.fAllSolid && tr.fInOpen && tr.flFraction != 1)
					TaskScheduler::Enroll(VFX_BulletImpact(vecPrevEndpos, tr, cTextureType, flCurDmg));
			}
		}
		else
			// We exceed the bullet traveling limitation.
			iPenetration = 0;

		Uranus::ApplyMultiDamage{}(pInflictor->pev, pAttacker->pev);
	}

	return Vector2D{
		x * flSpread,
		y * flSpread,
	};
}
