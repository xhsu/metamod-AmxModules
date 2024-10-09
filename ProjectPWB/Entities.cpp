import CBase;
import Configs;
import Entities;
import Plugin;
import VTFH;

import UtlHook;
import UtlRandom;

static fnEntityTouch_t gpfnOrgWpnBoxTouch = nullptr;
static fnEntityTraceAttack_t gpfnOrgWpnBoxTraceAttack = nullptr;
static fnEntityTakeDamage_t gpfnOrgWpnBoxTakeDamage = nullptr;

static void __fastcall HamF_Touch(CWeaponBox* pThis, std::uintptr_t edx, CBaseEntity* pOther) noexcept
{
	gpfnOrgWpnBoxTouch(pThis, pOther);

	auto const pev = pThis->pev;

	if (pOther->IsPlayer())
	{
		if (pOther->pev->velocity.LengthSquared() > (140.0 * 140.0))	// Walking won't kick anything.
			PlayerKick(pev, pOther->edict());

		return;
	}

	if (EHANDLE<CBasePlayer> pOwner{ pev->owner }; pOwner && pOwner != pOther)
	{
		pev->owner = nullptr;	// Feel free to touch anything.
	}

	if (pOther->pev->solid == SOLID_BSP)
	{
		// lie flat
		if (pev->flags & FL_ONGROUND)
		{
			TraceResult tr{};
			g_engfuncs.pfnTraceLine(
				pev->origin, pev->origin - Vector{ 0, 0, 10 },
				dont_ignore_monsters | dont_ignore_glass,
				pThis->edict(),
				&tr
			);

			if (tr.flFraction < 1.f)
			{
				pev->angles = tr.vecPlaneNormal.VectorAngles();
				pev->angles[0] = -pev->angles[0];
			}
		}

#define m_flTimeNextTouchSfx m_flStartThrow

		if (std::abs(pev->velocity.z) > 1.f && pThis->m_flTimeNextTouchSfx < gpGlobals->time)
		{
			g_engfuncs.pfnEmitSound(pThis->edict(), CHAN_WEAPON, WEAPONBOX_SFX_DROP, 0.25f, ATTN_STATIC, SND_FL_NONE, UTIL_Random(94, 110));
			pThis->m_flTimeNextTouchSfx = gpGlobals->time + 0.2f;
		}

#undef m_flTimeNextTouchSfx

		// Door, glass, etc.
		if (pev_valid(pOther->pev) == EValidity::Full
			&& pOther->pev->takedamage != DAMAGE_NO
			&& pev->velocity.LengthSquared() >= (350.0 * 350.0))
		{
			gpGamedllFuncs->dllapi_table->pfnUse(pOther->edict(), pThis->edict());
		}

		// Weaponbox drop down from high place.
		if (pev->velocity.LengthSquared() >= (1000.0 * 1000.0))
		{
			MetalHit(pev);
		}

		// disown, such that we can pickup our own gun.
		pev->owner = nullptr;
	}

	FreeRotationInTheAir(pev);

	if (pev->flags & FL_ONGROUND)
	{
		// Additional friction. Don't be hrash.
		pev->velocity *= (float)cvar_velocitydecay;
	}
}

static void __fastcall HamF_TraceAttack(CWeaponBox* pThis, std::uintptr_t edx,
	entvars_t* pevAttacker, float flDamage, Vector vecDir, TraceResult* ptr, int bitsDamageTypes) noexcept
{
	auto const pev = pThis->pev;

	if (bitsDamageTypes & (DMG_CLUB | DMG_BULLET))
	{
		pev->velocity += vecDir * (flDamage * (float)cvar_gunshotenergyconv);	// Original speed must be included.

		MakeMetalSFX(pThis->edict());
		UTIL_Spark(ptr->vecEndPos);

		FreeRotationInTheAir(pev);
	}

	// SUPERCEDED
}

static qboolean __fastcall HamF_TakeDamage(CWeaponBox* pThis, std::uintptr_t edx,
	entvars_t* pevInflictor, entvars_t* pevAttacker, float flDamage, int bitsDamageTypes) noexcept
{
	auto const pev = pThis->pev;

	if (bitsDamageTypes & DMG_EXPLOSION)	// grenade, actually.
	{
		pev->velocity +=
			(pev->origin - pevInflictor->origin).Normalize() * (flDamage * (float)cvar_gunshotenergyconv);

		FreeRotationInTheAir(pev);
		MetalHit(pev);
	}

	// SUPERCEDED
	return false;
}

void DeployWeaponBoxHook() noexcept
{
	static bool bHooked = false;

	[[likely]]
	if (bHooked)
		return;

	auto const pEdict = g_engfuncs.pfnCreateNamedEntity(MAKE_STRING("weaponbox"));
	auto const vft = UTIL_RetrieveVirtualFunctionTable(pEdict->pvPrivateData);

	UTIL_VirtualTableInjection(vft, VFTIDX_CBASE_TOUCH, &HamF_Touch, (void**)&gpfnOrgWpnBoxTouch);
	UTIL_VirtualTableInjection(vft, VFTIDX_CBASE_TRACEATTACK, &HamF_TraceAttack, (void**)&gpfnOrgWpnBoxTraceAttack);
	UTIL_VirtualTableInjection(vft, VFTIDX_CBASE_TAKEDAMAGE, &HamF_TakeDamage, (void**)&gpfnOrgWpnBoxTakeDamage);

	g_engfuncs.pfnRemoveEntity(pEdict);

	bHooked = true;
}
