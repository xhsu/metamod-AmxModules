import std;
import hlsdk;

import UtlRandom;

import CBase;
import ConditionZero;
import Decal;
import Message;
import Resources;
import Uranus;
import ZBot;


inline Resource::Add GIBS_CONCRETE{ "models/gibs_wallbrown.mdl" };
//inline Resource::Add GIBS_METAL{ "models/gibs_stairsmetal.mdl" };
//inline Resource::Add GIBS_RUBBLE{ "models/gibs_rubble.mdl" };
inline Resource::Add GIBS_WOOD{ "models/gibs_woodplank.mdl" };

inline Resource::Add SFX_HIT_WOOD[] =
{
	"player/pl_wood1.wav",
	"player/pl_wood2.wav",
	"player/pl_wood3.wav",
	"player/pl_wood4.wav",
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
};

extern edict_t* CreateWallPuff(TraceResult const& tr) noexcept;

static inline void VFX_BulletImpact(TraceResult const& tr, char cTextureType) noexcept
{
	UTIL_Decal(tr.pHit, tr.vecEndPos, UTIL_GetRandomOne(Decal::GUNSHOT));
	CreateWallPuff(tr);

	switch (cTextureType)
	{
	case CHAR_TEX_METAL:
	case CHAR_TEX_VENT:
		// Spark MDL
		break;

	case CHAR_TEX_GRATE:
	case CHAR_TEX_COMPUTER:
		MsgPVS(SVC_TEMPENTITY, tr.vecEndPos + tr.vecPlaneNormal);
		WriteData(TE_SPARKS);
		WriteData(tr.vecEndPos + tr.vecPlaneNormal);
		MsgEnd();
		break;

	case CHAR_TEX_WOOD:
		UTIL_BreakModel(
			tr.vecEndPos, Vector(1, 1, 1) /* Invalid Arg? */, tr.vecPlaneNormal * UTIL_Random(75, 100),
			UTIL_Random(0.8f, 1.2f),
			GIBS_WOOD,
			UTIL_Random(2, 4),
			UTIL_Random(6.f, 9.f),
			1 << 3	/* wood sfx */
		);

		g_engfuncs.pfnEmitAmbientSound(
			ent_cast<edict_t*>(0), tr.vecEndPos,
			UTIL_GetRandomOne(SFX_HIT_WOOD),
			0.9f, ATTN_NORM, SND_FL_NONE, 96 + UTIL_Random(0, 0xF)
		);

		break;

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
}

// Go to the trouble of combining multiple pellets into a single damage call.
// This version is used by Players, uses the random seed generator to sync client and server side shots.
Vector2D CS_FireBullets3(
	CBasePlayer* pAttacker, CBasePlayerItem* pInflictor,
	float flSpread, float flDistance,
	int iPenetration, int iBulletType, float flDamage, float flRangeModifier) noexcept
{
	[[maybe_unused]] auto const iOriginalPenetration{ iPenetration };
	float flPenetrationPower{};
	float flPenetrationDistance{};
	auto flCurDmg{ flDamage };
	float flCurrentDistance{};
	TraceResult tr{};

	auto const [vecForward, vecRight, vecUp]
		= (pAttacker->pev->v_angle + pAttacker->pev->punchangle).AngleVectors();

	switch (iBulletType)
	{
	case BULLET_PLAYER_9MM:
		flPenetrationPower = 21;
		flPenetrationDistance = 800;
		break;
	case BULLET_PLAYER_45ACP:
		flPenetrationPower = 15;
		flPenetrationDistance = 500;
		break;
	case BULLET_PLAYER_50AE:
		flPenetrationPower = 30;
		flPenetrationDistance = 1000;
		break;
	case BULLET_PLAYER_762MM:
		flPenetrationPower = 39;
		flPenetrationDistance = 5000;
		break;
	case BULLET_PLAYER_556MM:
		flPenetrationPower = 35;
		flPenetrationDistance = 4000;
		break;
	case BULLET_PLAYER_338MAG:
		flPenetrationPower = 45;
		flPenetrationDistance = 8000;
		break;
	case BULLET_PLAYER_57MM:
		flPenetrationPower = 30;
		flPenetrationDistance = 2000;
		break;
	case BULLET_PLAYER_357SIG:
		flPenetrationPower = 25;
		flPenetrationDistance = 800;
		break;
	default:
		flPenetrationPower = 0;
		flPenetrationDistance = 0;
		break;
	}

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

			VFX_BulletImpact(tr, cTextureType);

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
					VFX_BulletImpact(tr, cTextureType);
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
