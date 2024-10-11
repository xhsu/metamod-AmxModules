export module Entities;

import hlsdk;

import CBase;
import Configs;
import Prefab;

import UtlRandom;

extern "C++" void DeployWeaponBoxHook() noexcept;

export inline constexpr char WEAPONBOX_SFX_HIT[] = "debris/metal6.wav";
export inline constexpr char WEAPONBOX_SFX_DROP[] = "items/weapondrop1.wav";

export inline std::set<CWeaponBox*, std::less<>> gWpnBoxCheck{};

export void Materialization(entvars_t* pev) noexcept
{
	pev->friction = (float)cvar_friction;
	pev->gravity = (float)cvar_gravity;
	pev->solid = SOLID_BBOX;
	pev->movetype = MOVETYPE_BOUNCE;
	pev->takedamage = DAMAGE_YES;
	g_engfuncs.pfnSetSize(pev->pContainingEntity, Vector{ -16, -16, -0.5f }, Vector{ 16, 16, 0.5f });

	// Co-op with DLL
	pev->fuser4 = 9527.f;
}

export void FreeRotationInTheAir(entvars_t* pev) noexcept
{
	auto const flSpeed = pev->velocity.Length();

	pev->avelocity[0] = (float)flSpeed;
	pev->avelocity[1] = (float)UTIL_Random(-flSpeed, flSpeed);
}

export void PlayerKick(entvars_t* pev, edict_t* pPlayer) noexcept
{
	auto const vecVel =
		(pev->origin - pPlayer->v.origin).Normalize() * (pPlayer->v.velocity.Length() * 1.65f);

	// Add player velocity as if it were taken by the player.
	pev->velocity =
		vecVel + pPlayer->v.velocity;
}

export inline void UTIL_Spark(Vector const& vecOrigin) noexcept
{
	g_engfuncs.pfnMessageBegin(MSG_BROADCAST, SVC_TEMPENTITY, nullptr, nullptr);
	g_engfuncs.pfnWriteByte(TE_SPARKS);
	g_engfuncs.pfnWriteCoord(vecOrigin.x);
	g_engfuncs.pfnWriteCoord(vecOrigin.y);
	g_engfuncs.pfnWriteCoord(vecOrigin.z);
	g_engfuncs.pfnMessageEnd();
}

export inline void MakeMetalSFX(edict_t* ent) noexcept
{
	g_engfuncs.pfnEmitSound(ent, CHAN_ITEM, WEAPONBOX_SFX_HIT, 0.5f, ATTN_STATIC, SND_FL_NONE, UTIL_Random(94, 110));
}

export inline void MetalHit(entvars_t* pev) noexcept
{
	auto const vecOrigin =
		pev->origin + Vector{ UTIL_Random(-10.0, 10.0), UTIL_Random(-10.0, 10.0), 0 };

	UTIL_Spark(vecOrigin);
	MakeMetalSFX(pev->pContainingEntity);
}

export inline Vector UTIL_GetPlayerFront(entvars_t* pPlayer, float flMaxDist) noexcept
{
	auto const VAngles = Angles{ std::clamp(pPlayer->v_angle[0], -70.f, 65.f), pPlayer->v_angle[1], pPlayer->v_angle[2] };
	auto const vecSrc = pPlayer->origin + pPlayer->view_ofs;
	auto const vecEnd = vecSrc + VAngles.Front() * flMaxDist;

	TraceResult tr{};
	g_engfuncs.pfnTraceLine(
		vecSrc, vecEnd,
		dont_ignore_glass | dont_ignore_monsters,
		pPlayer->pContainingEntity,
		&tr
	);

	return tr.vecEndPos;
}
