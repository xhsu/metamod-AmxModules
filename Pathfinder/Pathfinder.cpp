/*
* Day-Night Survival Dev Team
* File Creation: 10 Oct 2024
* 
* Programmer: Luna the Reborn
* Consultant: Crsky
*/

#include <assert.h>

import std;
import hlsdk;

import BaseMonster;
import CBase;
import Math;
import Task;

import LocalNav;

import UtlRandom;

Task CBaseAI::Task_Turn() noexcept
{
	auto const START = pev->angles.yaw;
	auto const END = pev->ideal_yaw;
	auto const AMOUNT = double((((int(END - START) % 360) + 540) % 360) - 180);
	auto const START_TIME = gpGlobals->time;

//	g_engfuncs.pfnServerPrint(std::format("yaw_diff: {:.1f}\n", AMOUNT).c_str());

	auto flTurningTime = std::fmax((float)AMOUNT / 100.f, 0.3f);
	if (AMOUNT >= 135.0)
	{
		m_Scheduler.Enroll(Task_Anim_TurnThenBackToIdle("180L"), TASK_ANIM_TURNING, true);
		flTurningTime = m_ModelInfo->at("180L").m_total_length;
	}
	else if (AMOUNT <= -135.0)
	{
		m_Scheduler.Enroll(Task_Anim_TurnThenBackToIdle("180R"), TASK_ANIM_TURNING, true);
		flTurningTime = m_ModelInfo->at("180R").m_total_length;
	}

	for (m_bYawReady = false;;)
	{
		auto const flElap = gpGlobals->time - START_TIME;
		auto const t = std::clamp<double>(flElap / flTurningTime, 0, 1);
		auto const lerp = arithmetic_lerp(AMOUNT, 0.0, t, &Interpolation::decelerated<>);

		pev->angles.yaw = START + (float)lerp;
		UTIL_SetController(edict(), 0, AMOUNT - lerp);

		if (t >= 1.0)
			break;

		co_await 0.01f;
	}

	m_bYawReady = true;
	pev->angles.yaw = pev->ideal_yaw;

	// Rationalize.
	while (pev->angles.yaw >= 360.f)
		pev->angles.yaw -= 360.f;
	while (pev->angles.yaw < 0)
		pev->angles.yaw += 360.f;

	pev->ideal_yaw = pev->angles.yaw;
}

void PreThink(entvars_t* pev) noexcept
{
	Vector vecSrc{};
	Vector vecDest{};
	TraceResult tr{};
	float flOrigDist{};
	float flRaisedDist{};

	if (!(pev->flags & FL_ONGROUND))
		return;

	if (pev->velocity.Length2D() < 1)
		return;

	vecSrc = pev->origin;
	vecDest = vecSrc + pev->velocity * gpGlobals->frametime;
	vecDest.z = vecSrc.z;

	g_engfuncs.pfnTraceMonsterHull(pev->pContainingEntity, vecSrc, vecDest, dont_ignore_monsters | dont_ignore_glass, pev->pContainingEntity, &tr);

	if (tr.fStartSolid)
		return;

	if (tr.flFraction == 1)
		return;

	if (tr.vecPlaneNormal.z > 0.7)
		return;

	flOrigDist = (float)(tr.vecEndPos - pev->origin).Length2D();
	vecSrc.z += (float)cvar_stepsize;
	vecDest = vecSrc + (pev->velocity.Normalize() * 0.1);
	vecDest.z = vecSrc.z;

	g_engfuncs.pfnTraceMonsterHull(pev->pContainingEntity, vecSrc, vecDest, dont_ignore_monsters | dont_ignore_glass, pev->pContainingEntity, &tr);

	if (tr.fStartSolid)
		return;

	vecSrc = tr.vecEndPos;
	vecDest = tr.vecEndPos;
	vecDest.z -= (float)cvar_stepsize;

	g_engfuncs.pfnTraceMonsterHull(pev->pContainingEntity, vecSrc, vecDest, dont_ignore_monsters | dont_ignore_glass, pev->pContainingEntity, &tr);

	if (tr.vecPlaneNormal.z < 0.7)
		return;

	flRaisedDist = (float)(tr.vecEndPos - pev->origin).Length2D();

	if (flRaisedDist > flOrigDist)
	{
		Vector vecOrigin = pev->origin;
		vecOrigin.z = tr.vecEndPos.z;
		g_engfuncs.pfnSetOrigin(pev->pContainingEntity, vecOrigin);
		pev->velocity.z += pev->gravity * 1.f * gpGlobals->frametime;
	}
}

Task CBaseAI::Task_Walk(Vector const vecTarget, bool const bShouldPlayIdleAtEnd) noexcept
{
	Vector vecDiff{};
	Vector2D vecDirFlr{};
	float YAW{};

	for (;;)
	{
		co_await TaskScheduler::NextFrame::Rank[0];

		// We reach the dest, watch out for div by zero!
		if (vecTarget.Approx(pev->origin, 0.5f))
			break;

		vecDiff = (vecTarget - pev->origin);
		vecDirFlr = vecDiff.Make2D().Normalize();
		YAW = (float)vecDirFlr.Yaw();

		if (std::fabs(YAW - pev->ideal_yaw) > 1 && std::fabs(YAW - pev->angles.yaw) > 1)
		{
			pev->ideal_yaw = YAW;
			m_Scheduler.Enroll(Task_Turn(), TASK_MOVE_TURNING, true);
		}

		// If the turning causes a turning anim, stop walking and wait.
		while (m_Scheduler.Exist(TASK_ANIM_TURNING))
		{
			pev->velocity = g_vecZero;
			co_await TaskScheduler::NextFrame::Rank[1];
		}

		if (!IsAnimPlaying("Walk1"))
		{
			PlayAnim("Walk1");
			co_await 0.1f;
		}

		if (vecDiff.LengthSquared2D() > pev->maxspeed * gpGlobals->frametime * 2.0)
		{
			// LUNA:
			// The problem with pfnWalkMove() is that it stops anim interpolation.
			// Which is horriable, as we don't have many anim for old HL stuff.

//			g_engfuncs.pfnWalkMove(edict(), YAW, pev->maxspeed * gpGlobals->frametime, WALKMOVE_NORMAL);
//			pev->velocity = { vecDirFlr * pev->maxspeed, 0 };

			auto vecbigDest = pev->origin + Vector{ (vecDirFlr * (float)cvar_stepsize), 0 };
			auto const iTravelStatus = PathTraversable(pev->origin, &vecbigDest, dont_ignore_glass | dont_ignore_monsters);

			if (iTravelStatus != PTRAVELS_NO)
			{
				pev->velocity.x = vecDirFlr.x * pev->maxspeed;
				pev->velocity.y = vecDirFlr.y * pev->maxspeed;

				switch (iTravelStatus)
				{
				case PTRAVELS_STEP:
					if (pev->flags & FL_ONGROUND)
						// Theoratically it should be just enough. v = sqrt(2g*dx)
						pev->velocity.z = (float)std::sqrt(2.0 * 386.08858267717 * (vecbigDest.z - pev->origin.z)) * 1.6f;
					break;

				case PTRAVELS_STEPJUMPABLE:
					if (pev->flags & FL_ONGROUND)
						pev->velocity.z = 270;
					break;

				default:
					break;
				}
			}
		}
		else
		{
			// LUNA: sometimes the target origin is under the ground due to how NAV works.
			// In these cases, setting origin will cause the AI stuck forever.
//			g_engfuncs.pfnSetOrigin(edict(), vecTarget);
			break;
		}
	}

	// Clear vel. As the vel might be NaNF when close the end.
	// LUNA: this was consider as a physical change as well?? It will cause anim interpolation dead!
//	pev->velocity = g_vecZero;

	if (bShouldPlayIdleAtEnd)
	{
		co_await 0.02f;	// longer for interpo?
		PlayAnim("Idle2");
	}
}
