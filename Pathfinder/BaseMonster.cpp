/*
* Day-Night Survival Dev Team
* File Creation: 10 Oct 2024
* 
* Programmer: Luna the Reborn
* Consultant: Crsky
*/

#include <assert.h>
#include <stdio.h>

#ifdef __INTELLISENSE__
#include <ranges>
#endif

import std;
import hlsdk;

import BaseMonster;
import CBase;
import Math;
import Task;
import FileSystem;

import LocalNav;

import UtlRandom;



inline void UTIL_DrawBeamPoints(Vector const& vecStart, Vector const& vecEnd,
	int iLifetime, std::uint8_t bRed, std::uint8_t bGreen, std::uint8_t bBlue) noexcept
{
	g_engfuncs.pfnMessageBegin(MSG_PVS, SVC_TEMPENTITY, vecStart, nullptr);
	g_engfuncs.pfnWriteByte(TE_BEAMPOINTS);
	g_engfuncs.pfnWriteCoord(vecStart.x);
	g_engfuncs.pfnWriteCoord(vecStart.y);
	g_engfuncs.pfnWriteCoord(vecStart.z);
	g_engfuncs.pfnWriteCoord(vecEnd.x);
	g_engfuncs.pfnWriteCoord(vecEnd.y);
	g_engfuncs.pfnWriteCoord(vecEnd.z);
	g_engfuncs.pfnWriteShort(g_engfuncs.pfnModelIndex("sprites/smoke.spr"));
	g_engfuncs.pfnWriteByte(0);	// starting frame
	g_engfuncs.pfnWriteByte(0);	// frame rate
	g_engfuncs.pfnWriteByte(iLifetime);
	g_engfuncs.pfnWriteByte(10);	// width
	g_engfuncs.pfnWriteByte(0);	// noise
	g_engfuncs.pfnWriteByte(bRed);
	g_engfuncs.pfnWriteByte(bGreen);
	g_engfuncs.pfnWriteByte(bBlue);
	g_engfuncs.pfnWriteByte(255);	// brightness
	g_engfuncs.pfnWriteByte(0);	// scroll speed
	g_engfuncs.pfnMessageEnd();
}

// Single action

Task CBaseAI::Task_Move_Turn(bool const bSkipAnim) noexcept
{
	auto const START = pev->angles.yaw;
	auto const END = pev->ideal_yaw;
	auto const AMOUNT = double((((int(END - START) % 360) + 540) % 360) - 180);
	auto const START_TIME = gpGlobals->time;

//	g_engfuncs.pfnServerPrint(std::format("yaw_diff: {:.1f}\n", AMOUNT).c_str());

	auto flTurningTime = std::fmax((float)AMOUNT / 100.f, 0.3f);
	if (AMOUNT >= 135.0 && !bSkipAnim)
	{
		m_Scheduler.Enroll(Task_Anim_TurnThenBackToIdle("180L"), TASK_ANIM_TURNING | TASK_ANIM_INTERCEPTING, true);
		flTurningTime = m_ModelInfo->at("180L").m_total_length;
	}
	else if (AMOUNT <= -135.0 && !bSkipAnim)
	{
		m_Scheduler.Enroll(Task_Anim_TurnThenBackToIdle("180R"), TASK_ANIM_TURNING | TASK_ANIM_INTERCEPTING, true);
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

Task CBaseAI::Task_Move_Walk(Vector const vecTarget, Activity iMoveType, double const flApprox) noexcept
{
	Vector vecDiff{};
	Vector2D vecDirFlr{};
	float YAW{};

	m_vecGoal = vecTarget;

	for (;;)
	{
		co_await TaskScheduler::NextFrame::Rank[0];

		vecDiff = (vecTarget - pev->origin);
		vecDirFlr = vecDiff.Make2D().Normalize();
		YAW = (float)vecDirFlr.Yaw();

		Turn(YAW, false);

		// If the turning causes a turning anim, stop walking and wait.
		while (m_Scheduler.Exist(TASK_ANIM_INTERCEPTING))
		{
			pev->velocity = g_vecZero;
			co_await TaskScheduler::NextFrame::Rank[1];
		}

		if (!IsAnimPlaying(iMoveType))
		{
			PlayAnim(iMoveType);
			co_await 0.1f;
		}

		// watch out for div by zero!
		if (vecDiff.LengthSquared2D() > (flApprox * flApprox))
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
						pev->velocity.z = (float)std::sqrt(2.0 * 386.08858267717 * std::abs(vecbigDest.z - pev->origin.z)) * 1.6f;
					break;

				case PTRAVELS_STEPJUMPABLE:
					if (pev->flags & FL_ONGROUND)
						pev->velocity.z = 270;
					break;

				default:
					break;
				}
			}
			// Somehow this is unreachable.
			else
				co_return;
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
}

Task CBaseAI::Task_Move_Detour(Vector vecTarget, double flApprox) noexcept
{
	static constexpr auto RUN_DIST_SQ = 270.0 * 270.0;

	std::vector<Vector> Nodes{};

	goto LAB_DETOUR_START;

	for (;;)
	{
	LAB_DETOUR_RETRY:;
		flApprox += 1;	// relaxing condition.

	LAB_DETOUR_START:;
		co_await 0.12f;

		m_vecGoal = vecTarget;

		// Close enough
		if ((vecTarget - pev->origin).LengthSquared2D() < flApprox * flApprox)
			break;

		if (PathTraversable(pev->origin, &vecTarget, dont_ignore_monsters | dont_ignore_glass) != PTRAVELS_NO)
		{
			m_Scheduler.Enroll(
				Task_Move_Walk(
					vecTarget,
					(vecTarget - pev->origin).LengthSquared2D() > RUN_DIST_SQ ? ACT_RUN : ACT_WALK,
					flApprox
				), TASK_MOVE_WALKING, true);
		}
		else
		{
			auto const nindexPath =
				m_localnav.FindPath(pev->origin, m_vecGoal, 80, dont_ignore_monsters | dont_ignore_glass);

			if (nindexPath == NODE_INVALID_EMPTY)
				continue;	// retry everything.

			m_localnav.SetupPathNodes(nindexPath, &Nodes);
			auto const m_nTargetNode = m_localnav.GetFurthestTraversableNode(pev->origin, &Nodes, dont_ignore_monsters | dont_ignore_glass);
			std::span const rgvecLocalNav{ Nodes.begin(), Nodes.begin() + m_nTargetNode + 1 };

			// Walking along the local NAV.
			for (Vector const& vec : rgvecLocalNav | std::views::reverse)
			{
				m_Scheduler.Enroll(
					Task_Move_Walk(
						vec,
						(vec - pev->origin).LengthSquared2D() > RUN_DIST_SQ ? ACT_RUN : ACT_WALK,

						// when we fall back into local NAV, it normally means something is in the way. Hence we need better pathing precision.
						std::clamp<double>(flApprox / 2.5, 8.0, 16)
					), TASK_MOVE_WALKING, true);

				while (m_Scheduler.Exist(TASK_MOVE_WALKING | TASK_MOVE_TURNING))
				{
					co_await TaskScheduler::NextFrame::Rank[3];

					// Wait and check if we are stucked.
					if (m_StuckMonitor.GetDuration() > 1)
					{
						m_StuckMonitor.Reset();
						goto LAB_DETOUR_RETRY;
					}
				}
			}
		}

		while (m_Scheduler.Exist(TASK_MOVE_WALKING | TASK_MOVE_TURNING))
			co_await TaskScheduler::NextFrame::Rank[3];

		co_await TaskScheduler::NextFrame::Rank[3];
	}

	pev->velocity.x = vecTarget.x - pev->origin.x;
	pev->velocity.y = vecTarget.y - pev->origin.y;

	co_return;
}

Task CBaseAI::Task_Move_Ladder(CNavLadder const* ladder, NavTraverseType how, CNavArea const* pNextArea) noexcept
{
	// Why are you calling me then!
	if (how != GO_LADDER_DOWN && how != GO_LADDER_UP)
		co_return;

	std::vector<Vector> Nodes{};
	TraceResult tr{};

	m_pTargetEnt = ladder->m_entity;
	m_fTargetEntHit = false;

	if (how == GO_LADDER_DOWN)
	{
		m_vecGoal = ladder->m_top + Vector{ ladder->m_dirVector, 0 } * 16;

		// When moving down, the starting post is extra-important.
		do
		{
			g_engfuncs.pfnTraceMonsterHull(
				edict(),
				m_vecGoal,
				Vector{ m_vecGoal.x, m_vecGoal.y, m_vecGoal.z - 80.f },
				dont_ignore_monsters | dont_ignore_glass,
				edict(),
				&tr
			);

			// One unit.
			m_vecGoal.x += ladder->m_dirVector.x;
			m_vecGoal.y += ladder->m_dirVector.y;

		} while (tr.fAllSolid || tr.fStartSolid || tr.flFraction < 1.f);

		auto const vecLadderTop{ m_vecGoal };
//		Detour(m_vecGoal, 1);	// We have NO torlerance when ladders involved.
//		while (m_Scheduler.Exist(TASK_MOVE_DETOUR))
//			co_await TaskScheduler::NextFrame::Rank[4];

		m_Scheduler.Enroll(
			Task_Move_Walk(
				m_vecGoal,
				(m_vecGoal - pev->origin).LengthSquared2D() > (270.0 * 270.0) ? ACT_RUN : ACT_WALK,
				1.0	// We have NO torlerance when ladders involved.
			), TASK_MOVE_WALKING, true);

		while (m_Scheduler.Exist(TASK_MOVE_WALKING | TASK_MOVE_TURNING))
		{
			if ((pev->origin - vecLadderTop).LengthSquared2D() < 36.0 * 36.0)
				pev->movetype = MOVETYPE_FLY;

			co_await TaskScheduler::NextFrame::Rank[0];
		}

		pev->velocity = g_vecZero;	// Stop immediately, preventing the sliding effect in flying mode.
		Turn((-ladder->m_dirVector).Yaw(), true);

		// Start climbing
		InsertingAnim(ACT_HOVER);
		while (m_Scheduler.Exist(TASK_ANIM_INTERCEPTING))
			co_await TaskScheduler::NextFrame::Rank[0];

		pev->origin = vecLadderTop;
		pev->movetype = MOVETYPE_STEP;

		// Persisting climbing anim
		InsertingAnim(ACT_GLIDE);

		while (ladder->m_bottom.z + HalfHumanHeight < GetFeet().z)
		{
			// We should not push AI in XY direction here, or it will leave the ladder area.
			pev->velocity.z = -100.f;

			co_await TaskScheduler::NextFrame::Rank[0];
		}
	}
	else if (how == GO_LADDER_UP)
	{
		m_vecGoal = ladder->m_bottom + Vector{ ladder->m_dirVector, 0 } * 17;

		// Go and find the bottom of the ladder
		Detour(m_vecGoal, 4);
		while (m_Scheduler.Exist(TASK_MOVE_DETOUR))
			co_await TaskScheduler::NextFrame::Rank[4];

		Turn((-ladder->m_dirVector).Yaw(), true);

		// Start climbing
		InsertingAnim(ACT_HOVER);
		while (m_Scheduler.Exist(TASK_ANIM_INTERCEPTING))
			co_await TaskScheduler::NextFrame::Rank[0];

		// Persisting climbing anim
		InsertingAnim(ACT_GLIDE);

		while (GetFeet().z < ladder->m_top.z)
		{
			pev->velocity.x = ladder->m_dirVector.x * -80.f;	// We are pushing AI towards the dir of the ladder.
			pev->velocity.y = ladder->m_dirVector.y * -80.f;
			pev->velocity.z = 150.f;

			co_await TaskScheduler::NextFrame::Rank[0];
		}
	}

	InsertingAnim(ACT_LAND);

	if (pNextArea)
	{
		// A little push, handling places like cs_assault CT spawn building.
		while (how == GO_LADDER_UP && !(pev->flags & FL_ONGROUND))
		{
			auto const vecDir = (pNextArea->GetCenter() - pev->origin).Make2D().Normalize();

			pev->velocity.x = vecDir.x * 160.f;
			pev->velocity.y = vecDir.y * 160.f;

			Turn(vecDir.Yaw(), true);

			co_await TaskScheduler::NextFrame::Rank[0];
		}
	}

	while (m_Scheduler.Exist(TASK_ANIM_INTERCEPTING))
		co_await TaskScheduler::NextFrame::Rank[0];

	co_return;
}

// Plots

Task CBaseAI::Task_Plot_WalkOnPath(Vector const vecTarget, double flApprox) noexcept
{
	std::vector<Vector> Nodes{};

	for (;;)
	{
		// We arrived.
		if ((pev->origin - vecTarget).LengthSquared2D() < flApprox * flApprox)
			break;

		co_await 0.01f;

		[[unlikely]]
		if (!m_path.Compute(pev->origin, vecTarget, HostagePathCost{}))
		{
			g_engfuncs.pfnServerPrint("No path found on AI!\n");
			continue;
		}

		auto Path =
			SimplifiedPath(dont_ignore_glass | dont_ignore_monsters);

		if (Path.empty())
			continue;

		co_await 0.01f;

		m_vecGoal = Path.front().pos;
		m_Scheduler.Enroll(Task_Debug_ShowPath(Path, pev->origin), TASK_DEBUG, true);

		co_await 0.01f;

		// Attempt to walk with the current path.
		for (auto it = Path.begin(); it != Path.end(); /* do nothing */)
		{
			switch (it->how)
			{
			case GO_LADDER_UP:
			case GO_LADDER_DOWN:
			{
				auto const nextIt = it + 1;

				m_Scheduler.Enroll(
					Task_Move_Ladder(it->ladder, it->how, nextIt == Path.end() ? nullptr : nextIt->area),
					TASK_MOVE_LADDER,
					true
				);
				break;	// switch
			}

			default:
				Detour(it->pos, flApprox);	// the accuracy doesn't matter anymore, we have local NAV.
				break;	// switch
			}

			auto bNewPath = false;
			auto const pSave = std::addressof(*it);

			while (m_Scheduler.Exist(TASK_MOVEMENTS))
			{
				co_await 0.11f;

				// Now we are free, let's simplify paths.
				Path = SimplifiedPath(dont_ignore_monsters | dont_ignore_glass, Path.subspan(it - Path.begin()));
				it = Path.begin();
				bNewPath = true;

				m_Scheduler.Enroll(Task_Debug_ShowPath(Path, pev->origin), TASK_DEBUG, true);

				co_await TaskScheduler::NextFrame::Rank[3];

				// Wait and check if we are stucked.
				if (m_StuckMonitor.GetDuration() > 1 && !m_Scheduler.Exist(TASK_MOVE_LADDER))
				{
					m_StuckMonitor.Reset();
					goto LAB_RECOMPUTE_PATH;
				}
			}

			if (Path.empty())
				goto LAB_RECOMPUTE_PATH;

			// Only increase iterator if we doesn't successfully simplify the route.
			if (std::addressof(*it) == pSave)
				++it;

			co_await TaskScheduler::NextFrame::Rank[3];
		}

	LAB_RECOMPUTE_PATH:;
	}

	PlayAnim(ACT_IDLE);
	co_return;
}

// Debug

Task CBaseAI::Task_Debug_ShowPath(std::span<PathSegment const> segments, Vector const vecSrc) noexcept
{
	static constexpr Vector VEC_OFS{ 0, 0, VEC_DUCK_HULL_MAX.z / 2.f };
	static constexpr std::array<std::string_view, NUM_TRAVERSE_TYPES + 1> TRAV_MEANS =
	{
		"GO_NORTH",
		"GO_EAST",
		"GO_SOUTH",
		"GO_WEST",
		"GO_LADDER_UP",
		"GO_LADDER_DOWN",
		"GO_JUMP",
		"TERMINUS",
	};

	g_engfuncs.pfnServerPrint(
		std::format("{} Segments in total.\n", segments.size()).c_str()
	);

	for (auto&& Seg : segments)
	{
		g_engfuncs.pfnServerPrint(
			std::format("    {}@ {: <6.1f}{: <6.1f}{: <6.1f}\n", TRAV_MEANS[Seg.how], Seg.pos.x, Seg.pos.y, Seg.pos.z).c_str()
		);
	}

	for (;;)
	{
		co_await 0.01f;	// avoid inf loop.

		if (segments.size() >= 1)
		{
			UTIL_DrawBeamPoints(
				vecSrc + VEC_OFS,
				segments.front().pos + VEC_OFS,
				5, 255, 255, 255
			);
		}

		for (auto&& [src, dest] : segments | std::views::adjacent<2>)
		{
			// Connect regular path with ladders.
			switch (src.how)
			{
			case GO_LADDER_DOWN:
				UTIL_DrawBeamPoints(
					src.ladder->m_top,
					src.ladder->m_bottom,
					5, 128, 0, 0
				);
				UTIL_DrawBeamPoints(
					src.ladder->m_bottom,
					dest.pos + VEC_OFS,
					5, 255, 255, 255
				);
				break;

			case GO_LADDER_UP:
				UTIL_DrawBeamPoints(
					src.ladder->m_bottom,
					src.ladder->m_top,
					5, 0, 128, 0
				);
				UTIL_DrawBeamPoints(
					src.ladder->m_top,
					dest.pos + VEC_OFS,
					5, 255, 255, 255
				);
				break;

			case GO_JUMP:
				UTIL_DrawBeamPoints(
					src.pos + VEC_OFS,
					dest.pos + VEC_OFS,
					5, 0, 0, 128
				);
				break;

			default:
				UTIL_DrawBeamPoints(
					src.pos + VEC_OFS,
					dest.pos + VEC_OFS,
					5, 255, 255, 255
				);
				break;
			}

			co_await 0.01f;
		}

		UTIL_DrawBeamPoints(
			m_vecGoal,
			{ m_vecGoal.x, m_vecGoal.y, m_vecGoal.z + 36.f },
			5, 0x09, 0x10, 0x57
		);

		UTIL_DrawBeamPoints(
			pev->origin,
			pev->origin + pev->angles.Front() * (float)cvar_stepsize,
			5, 0, 0xFF, 0x9C
		);
	}

	co_return;
}

// Static Precache

void CBaseAI::PrepareActivityInfo(std::string_view szModel) noexcept
{
	auto const f = FileSystem::StandardOpen(szModel.data(), "rb");
	if (!f)
		return;

	std::fseek(f, 0, SEEK_END);
	auto const iSize = (std::size_t)ftell(f);
	auto const pbuf = new char[iSize + 1] {};
	std::fseek(f, 0, SEEK_SET);
	std::fread(pbuf, sizeof(char), iSize, f);

	auto const phdr = (studiohdr_t*)pbuf;
	auto const pseqdesc = (mstudioseqdesc_t*)((std::byte*)pbuf + phdr->seqindex);

	for (auto&& Sequence : std::span{ pseqdesc, phdr->numseq })
	{
		auto& vLibrary = m_ActSequences[Sequence.activity];
		vLibrary.reserve(vLibrary.size() + Sequence.actweight);

		// The weight translate into the frequency of the anim.
		vLibrary.insert_range(
			vLibrary.end(),
			std::views::repeat(&m_ModelInfo->at(Sequence.label), Sequence.actweight)
		);
	}

	delete[] pbuf;
	std::fclose(f);
}
