module;

#ifdef __INTELLISENSE__
#include <algorithm>
#include <ranges>
#endif

#include <assert.h>

export module BaseMonster;

import std;
import hlsdk;

import Prefab;
import Task;
import Models;

import Improvisational;
import LocalNav;

import UtlRandom;
import UtlString;



export enum ETaskIdMonster : std::uint64_t
{
	TASK_INIT = 1ull << 1,
	TASK_REMOVE = 1ull << 2,
	TASK_DEBUG = 1ull << 3,
	TASK_STUCKMONITOR = 1ull << 4,

	TASK_MOVE_TURNING = 1ull << 8,
	TASK_MOVE_WALKING = 1ull << 9,
	TASK_MOVE_LADDER = 1ull << 10,
	TASK_MOVEMENTS = TASK_MOVE_TURNING | TASK_MOVE_WALKING | TASK_MOVE_LADDER,

	TASK_ANIM_TURNING = 1ull << 16,
	TASK_ANIM_INTERCEPTING = 1ull << 17,

	TASK_PLOT_WALK_TO = 1ull << 24,
	TASK_PLOT_PATROL = 1ull << 25,
};



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

export struct CBaseAI : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "mob_hgrunt";

	static inline decltype(GoldSrc::m_StudioInfo)::value_type::second_type* m_ModelInfo{};
	using seq_info_t = std::remove_pointer_t<decltype(m_ModelInfo)>::value_type::second_type;
	static inline std::array<std::vector<seq_info_t const*>, 0xFF> m_ActSequences{};

	CNavPath m_path{};
	CLocalNav m_localnav{};
	CStuckMonitor m_StuckMonitor{};

	Vector m_vecGoal{};
	EHANDLE<CBaseEntity> m_pTargetEnt{};
	CNavArea* m_CurArea{};
	bool m_bYawReady : 1 { false };
	mutable bool m_fTargetEntHit : 1 { false };

	// Entity properties

	void Spawn() noexcept override
	{
		static bool bPrecached = false;
		if (!bPrecached)
		{
			GoldSrc::CacheStudioModelInfo("models/hgrunt.mdl");
			m_ModelInfo = &GoldSrc::m_StudioInfo.at("models/hgrunt.mdl");

			// Must place after m_ModelInfo prep.
			PrepareActivityInfo("models/hgrunt.mdl");

			bPrecached = true;
		}

		// Must leave it out. Say, what if the map changed?
		if (LoadNavigationMap() != NAV_OK)
			g_engfuncs.pfnServerPrint("NAV map no found when trying to create AI!\n");

		pev->solid = SOLID_SLIDEBOX;
		pev->movetype = MOVETYPE_STEP;
		pev->effects = 0;
		pev->health = 100.f;
		pev->flags |= FL_MONSTER;
		pev->gravity = 1.f;

		g_engfuncs.pfnSetModel(edict(), "models/hgrunt.mdl");	// LUNA: Fuck GoldSrc. This one must place before SetSize()
		g_engfuncs.pfnSetSize(edict(), VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX);

		pev->takedamage = DAMAGE_YES;
		pev->ideal_yaw = pev->angles.yaw;
		pev->max_health = pev->health;
		pev->deadflag = DEAD_NO;

		pev->view_ofs = g_engfuncs.pfnGetModelPtr(edict())->eyeposition;

		pev->sequence = m_ModelInfo->at("idle2").m_iSeqIdx;
		pev->animtime = gpGlobals->time;
		pev->framerate = 1.f;
		pev->frame = 0;

		UTIL_SetController(edict(), 0, 0);

		m_Scheduler.Enroll(Task_Init(), TASK_INIT, true);
//		m_Scheduler.Enroll(Task_Kill(), TASK_REMOVE, true);
		m_Scheduler.Enroll(Task_StuckMonitor(), TASK_STUCKMONITOR, true);

		m_localnav.m_pOwner = this;
	}

	constexpr int Classify() noexcept override { return CLASS_HUMAN_MILITARY; }

	// Improv properties

/*
	bool IsAlive() const noexcept override { return pev->deadflag == DEAD_NO; }

	void MoveTo(const Vector& goal) noexcept override { Plot_PathToLocation(goal); }
	void LookAt(const Vector& target) noexcept override
	{
		auto const YAW = (float)(goal - pev->origin).Yaw();

		UTIL_SetController(edict(), 0, YAW);
	}
	void ClearLookAt() noexcept override {}	// It's a immed set.
*/

	virtual void FaceTo(const Vector& where) noexcept
	{
		Turn(
			(where - pev->origin).Yaw()
		);
	}
	virtual void ClearFaceTo() noexcept { m_Scheduler.Delist(TASK_ANIM_TURNING | TASK_MOVE_TURNING); }

	virtual const Vector& GetFeet() const noexcept { return pev->origin; };		// return position of "feet" - point below centroid of improv at feet level
	virtual Vector GetCentroid() const noexcept { return pev->origin + (VEC_HUMAN_HULL_MIN + VEC_HUMAN_HULL_MAX) / 2.f; };
	virtual Vector GetEyes() const noexcept { return pev->origin + pev->view_ofs; };

	// Plots

	// Util

	__forceinline void InsertingAnim(Activity activity) noexcept
	{
		m_Scheduler.Enroll(Task_Anim_Intercepting(activity), TASK_ANIM_INTERCEPTING, true);
	}

	inline auto PlayAnim(std::string_view what) noexcept -> decltype(&m_ModelInfo->find(what)->second)
	{
		if (auto const it = m_ModelInfo->find(what); it != m_ModelInfo->end())
		{
			pev->sequence = it->second.m_iSeqIdx;
			pev->animtime = gpGlobals->time;
			pev->framerate = 1.f;
			pev->frame = 0;

			return &it->second;
		}

		return nullptr;
	}

	virtual auto PlayAnim(Activity activity) noexcept -> decltype(&m_ModelInfo->at(""))
	{
		seq_info_t const* seq{};

		auto const DrawRandom =
			[&seq](Activity activity) noexcept
			{
				if (!m_ActSequences[activity].empty())
					seq = UTIL_GetRandomOne(m_ActSequences[activity]);
			};

		switch (activity)
		{
		case ACT_IDLE:
			DrawRandom(activity);
			break;

		case ACT_WALK:
			if (pev->health < pev->max_health * 0.65f)
				DrawRandom(ACT_WALK_HURT);
			if (!seq)
				DrawRandom(activity);
			break;

		case ACT_RUN:
			if (pev->health < pev->max_health * 0.65f)
				DrawRandom(ACT_RUN_HURT);
			if (!seq)
				DrawRandom(activity);
			break;

		case ACT_INVALID:
		default:
			break;
		}

		if (seq)
		{
			pev->sequence = seq->m_iSeqIdx;
			pev->animtime = gpGlobals->time;
			pev->framerate = 1.f;
			pev->frame = 0;

			pev->maxspeed = seq->m_flGroundSpeed;
		}

		return nullptr;
	}

	inline bool IsAnimPlaying(std::string_view what) const noexcept
	{
		if (auto const it = m_ModelInfo->find(what);
			it != m_ModelInfo->end())
		{
			return pev->sequence == it->second.m_iSeqIdx;
		}

		// No info.
		return false;
	}

	virtual bool IsAnimPlaying(Activity activity) const noexcept
	{
		auto const pmodel = g_engfuncs.pfnGetModelPtr(edict());

		if (pev->sequence >= (signed)pmodel->numseq || pev->sequence < 0)
			return false;

		auto const pseqdesc =
			(mstudioseqdesc_t*)((std::byte*)pmodel + pmodel->seqindex) + pev->sequence;

		switch (activity)
		{
		case ACT_IDLE:
			return
				pseqdesc->activity == ACT_COMBAT_IDLE
				|| pseqdesc->activity == ACT_CROUCH_IDLE
				|| pseqdesc->activity == ACT_CROUCH_IDLE_FIDGET
				|| pseqdesc->activity == ACT_CROUCH_IDLE_SCARED
				|| pseqdesc->activity == ACT_CROUCH_IDLE_SCARED_FIDGET
				|| pseqdesc->activity == ACT_CROUCHIDLE
				|| pseqdesc->activity == ACT_FOLLOW_IDLE
				|| pseqdesc->activity == ACT_FOLLOW_IDLE_FIDGET
				|| pseqdesc->activity == ACT_FOLLOW_IDLE_SCARED
				|| pseqdesc->activity == ACT_FOLLOW_IDLE_SCARED_FIDGET
				|| pseqdesc->activity == ACT_IDLE
				|| pseqdesc->activity == ACT_IDLE_ANGRY
				|| pseqdesc->activity == ACT_IDLE_FIDGET
				|| pseqdesc->activity == ACT_IDLE_SCARED
				|| pseqdesc->activity == ACT_IDLE_SCARED_FIDGET
				|| pseqdesc->activity == ACT_IDLE_SNEAKY
				|| pseqdesc->activity == ACT_IDLE_SNEAKY_FIDGET;

		case ACT_RUN:
			return
				pseqdesc->activity == ACT_RUN
				|| pseqdesc->activity == ACT_RUN_HURT
				|| pseqdesc->activity == ACT_RUN_SCARED;

		case ACT_WALK:
			return
				pseqdesc->activity == ACT_CROUCH_WALK
				|| pseqdesc->activity == ACT_CROUCH_WALK_SCARED
				|| pseqdesc->activity == ACT_WALK
				|| pseqdesc->activity == ACT_WALK_BACK
				|| pseqdesc->activity == ACT_WALK_HURT
				|| pseqdesc->activity == ACT_WALK_SCARED
				|| pseqdesc->activity == ACT_WALK_SNEAKY;

		default:
			return pseqdesc->activity == activity;
		}

		std::unreachable();
		return false;
	}

	inline void Turn(std::floating_point auto YAW, bool bSkipAnim = false) noexcept
	{
		if (std::abs(YAW - pev->ideal_yaw) > 1 && std::abs(YAW - pev->angles.yaw) > 1)
		{
			pev->ideal_yaw = (float)YAW;
			m_Scheduler.Enroll(Task_Move_Turn(bSkipAnim), TASK_MOVE_TURNING, true);
		}
	}

	// Path Verification

	auto SimplifiedPath(TRACE_FL fNoMonsters, std::span<PathSegment> Path = {}) noexcept -> decltype(this->m_path.Inspect())
	{
		if (Path.empty())
			Path = m_path.Inspect();

		decltype(std::addressof(Path[0])) pStart{};
		Vector vecDummy{};
		size_t nCount{};

		// You cannot simplify across jumping and ladder points.

		for (nCount = 0; auto&& Seg : Path)
		{
			if (Seg.how >= NUM_DIRECTIONS && Seg.how != NUM_TRAVERSE_TYPES)
				break;

			++nCount;
		}

		std::span const SimplifiableSegments{ Path.subspan(0, nCount) };

		if (!nCount)
			return Path;

		// Counting down from the farthest.
		for (nCount = SimplifiableSegments.size() - 1; auto&& Seg : SimplifiableSegments | std::views::reverse)
		{
			PathTraversAble res{ PTRAVELS_NO };
			switch (Seg.how)
			{
			case GO_LADDER_UP:
				vecDummy = Seg.ladder->m_bottom + Vector{ Seg.ladder->m_dirVector, 0 } * 17.0;
				res = PathTraversable(pev->origin, &vecDummy, fNoMonsters);
				break;

			case GO_LADDER_DOWN:
				vecDummy = Seg.ladder->m_top + Vector{ Seg.ladder->m_dirVector, 0 } * 17.0;
				res = PathTraversable(pev->origin, &vecDummy, fNoMonsters);
				break;

			default:
				res = PathTraversable(pev->origin, &Seg.pos, fNoMonsters);
				break;
			}

			assert(Path.size() >= nCount);

			// The node gets modified in the process.
			if (res != PTRAVELS_NO)
				return std::span{ Path.data() + nCount, Path.size() - nCount };

			--nCount;
		}

		// None of them can be simplified, so just return the whole path.
		return Path;
	}

	PathTraversAble PathTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters) const noexcept
	{
		TraceResult tr{};
		auto retval = PTRAVELS_NO;

		auto vecSrcTmp{ vecSource };
		auto vecDestTmp = *pvecDest - vecSource;

		auto vecDir = vecDestTmp.Normalize();
		vecDir.z = 0;

		auto flTotal = vecDestTmp.Length2D();

		while (flTotal > 1.0f)
		{
			if (flTotal >= (float)cvar_stepsize)
				vecDestTmp = vecSrcTmp + (vecDir * (float)cvar_stepsize);
			else
				vecDestTmp = *pvecDest;

			m_fTargetEntHit = false;

			if (PathClear(vecSrcTmp, vecDestTmp, fNoMonsters, &tr))
			{
				vecDestTmp = tr.vecEndPos;

				if (retval == PTRAVELS_NO)
				{
					retval = PTRAVELS_SLOPE;
				}
			}
			else
			{
				if (tr.fStartSolid)
				{
					return PTRAVELS_NO;
				}

				if (tr.pHit && !(std::to_underlying(fNoMonsters) & ignore_monsters) && tr.pHit->v.classname)
				{
					// #PF_LOCAL_HOSTAGE
					if (FClassnameIs(&tr.pHit->v, "hostage_entity"))
					{
						return PTRAVELS_NO;
					}
				}

				vecSrcTmp = tr.vecEndPos;

				if (tr.vecPlaneNormal.z <= MaxUnitZSlope)
				{
					if (StepTraversable(vecSrcTmp, &vecDestTmp, fNoMonsters, &tr))
					{
						if (retval == PTRAVELS_NO)
						{
							retval = PTRAVELS_STEP;
						}
					}
					else
					{
						if (!StepJumpable(vecSrcTmp, &vecDestTmp, fNoMonsters, &tr))
						{
							return PTRAVELS_NO;
						}

						if (retval == PTRAVELS_NO)
						{
							retval = PTRAVELS_STEPJUMPABLE;
						}
					}
				}
				else
				{
					if (!SlopeTraversable(vecSrcTmp, &vecDestTmp, fNoMonsters, &tr))
					{
						return PTRAVELS_NO;
					}

					if (retval == PTRAVELS_NO)
					{
						retval = PTRAVELS_SLOPE;
					}
				}
			}

			Vector const vecDropDest = vecDestTmp - Vector(0, 0, 300);

			if (PathClear(vecDestTmp, vecDropDest, fNoMonsters, &tr))
			{
				return pev->movetype == MOVETYPE_FLY ? PTRAVELS_MIDAIR : PTRAVELS_NO;
			}

			if (!tr.fStartSolid)
				vecDestTmp = tr.vecEndPos;

			vecSrcTmp = vecDestTmp;

			Vector const vecSrcThisTime = *pvecDest - vecDestTmp;

			if (m_fTargetEntHit)
				break;

			flTotal = vecSrcThisTime.Length2D();
		}

		*pvecDest = vecDestTmp;

		return retval;
	}

	bool PathClear(Vector const& vecOrigin, Vector const& vecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		g_engfuncs.pfnTraceMonsterHull(edict(), vecOrigin, vecDest, fNoMonsters, edict(), tr);

		if (tr->fStartSolid)
			return false;

		if (tr->flFraction == 1.0f)
			return true;

		if (m_pTargetEnt && tr->pHit == m_pTargetEnt.Get())
		{
			m_fTargetEntHit = true;
			return true;
		}

		return false;
	}
	bool PathClear(Vector const& vecSource, Vector const& vecDest, TRACE_FL fNoMonsters) const noexcept
	{
		TraceResult tr{};
		return PathClear(vecSource, vecDest, fNoMonsters, &tr);
	}

	bool SlopeTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		auto vecSlopeEnd = *pvecDest;
		auto vecDown = *pvecDest - vecSource;

		auto vecAngles = tr->vecPlaneNormal.VectorAngles();
		vecSlopeEnd.z = static_cast<vec_t>(vecDown.Length2D() * std::tan(double((90.0 - vecAngles.pitch) * (std::numbers::pi / 180.0))) + vecSource.z);

		if (!PathClear(vecSource, vecSlopeEnd, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
				return false;

			if ((tr->vecEndPos - vecSource).Length2D() < 1.0f)
				return false;
		}

		vecSlopeEnd = tr->vecEndPos;

		vecDown = vecSlopeEnd;
		vecDown.z -= (float)cvar_stepsize;

		if (!PathClear(vecSlopeEnd, vecDown, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
			{
				*pvecDest = vecSlopeEnd;
				return true;
			}
		}

		*pvecDest = tr->vecEndPos;
		return true;
	}

	bool LadderTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		auto vecStepStart = tr->vecEndPos;
		auto vecStepDest = vecStepStart;
		vecStepDest.z += HOSTAGE_STEPSIZE;

		if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
				return false;

			if ((tr->vecEndPos - vecStepStart).Length() < 1.0f)
				return false;
		}

		vecStepStart = tr->vecEndPos;
		pvecDest->z = tr->vecEndPos.z;

		return PathTraversable(vecStepStart, pvecDest, fNoMonsters);
	}

	bool StepTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		auto vecStepStart = vecSource;
		auto vecStepDest = *pvecDest;

		vecStepStart.z += (float)cvar_stepsize;
		vecStepDest.z = vecStepStart.z;

		if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
				return false;

			auto const flFwdFraction = (tr->vecEndPos - vecStepStart).Length();

			if (flFwdFraction < 1.0f)
				return false;
		}

		vecStepStart = tr->vecEndPos;

		vecStepDest = vecStepStart;
		vecStepDest.z -= (float)cvar_stepsize;

		if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
			{
				*pvecDest = vecStepStart;
				return true;
			}
		}

		*pvecDest = tr->vecEndPos;
		return true;
	}

	bool StepJumpable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		float flJumpHeight = (float)cvar_stepsize + 1.0f;

		auto vecStepStart = vecSource;
		vecStepStart.z += flJumpHeight;

		while (flJumpHeight < 40.0f)
		{
			auto vecStepDest = *pvecDest;
			vecStepDest.z = vecStepStart.z;

			if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
			{
				if (tr->fStartSolid)
					break;

				auto const flFwdFraction = (tr->vecEndPos - vecStepStart).Length2D();

				if (flFwdFraction < 1.0)
				{
					flJumpHeight += 10.0f;
					vecStepStart.z += 10.0f;

					continue;
				}
			}

			vecStepStart = tr->vecEndPos;
			vecStepDest = vecStepStart;
			vecStepDest.z -= (float)cvar_stepsize;

			if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
			{
				if (tr->fStartSolid)
				{
					*pvecDest = vecStepStart;
					return true;
				}
			}

			*pvecDest = tr->vecEndPos;
			return true;
		}

		return false;
	}



	Task Task_Init() noexcept
	{
		co_await 0.1f;

		pev->origin.z += 1;
		g_engfuncs.pfnDropToFloor(edict());

		// Try to move the monster to make sure it's not stuck in a brush.
		if (!g_engfuncs.pfnWalkMove(edict(), 0, 0, WALKMOVE_NORMAL))
		{
			g_engfuncs.pfnAlertMessage(at_error, "Monster %s stuck in wall--level design error", STRING(pev->classname));
			pev->effects = EF_BRIGHTFIELD;
		}
	}

	Task Task_Kill(float const flTime = 20.f) noexcept
	{
		if (flTime > 0.f)
			co_await flTime;

		pev->flags |= FL_KILLME;
		pev->solid = SOLID_NOT;
		pev->takedamage = DAMAGE_NO;

		co_return;
	}

	Task Task_StuckMonitor() noexcept
	{
		for (;;)
		{
			co_await TaskScheduler::NextFrame::Rank.back();

			m_StuckMonitor.Update(
				GetCentroid(),
				m_Scheduler.Exist(TASK_MOVE_LADDER)
			);

			if (m_Scheduler.Exist(TASK_ANIM_INTERCEPTING | TASK_ANIM_TURNING))
				m_StuckMonitor.Reset();
		}
	}

	// Movement Tasks

	Task Task_Move_Turn(bool const bSkipAnim = false) noexcept;
	Task Task_Move_Walk(Vector const vecTarget, Activity iMoveType = ACT_RUN, double const flApprox = VEC_HUMAN_HULL_MAX.x + 1.0, bool const bShouldPlayIdleAtEnd = true) noexcept;
	Task Task_Move_Ladder(CNavLadder const* ladder, NavTraverseType how, CNavArea const* pNextArea = nullptr) noexcept;

	Task Task_Anim_TurnThenBackToIdle(std::string_view szInitialAnim) noexcept
	{
		auto pInfo = PlayAnim(szInitialAnim);
		if (!pInfo)
			co_return;

		co_await pInfo->m_total_length;

		pInfo = PlayAnim("CombatIdle");
		if (!pInfo)
			co_return;

		co_await pInfo->m_total_length;

//		PlayAnim("Idle2");
	}

	Task Task_Patrolling(Vector const vecTarget) noexcept
	{
		auto const vec = pev->origin;

		for (;;)
		{
			m_Scheduler.Enroll(Task_Plot_WalkOnPath(vecTarget), TASK_PLOT_WALK_TO, true);

			while (m_Scheduler.Exist(TASK_PLOT_WALK_TO))
				co_await 1.f;

			m_Scheduler.Enroll(Task_Plot_WalkOnPath(vec), TASK_PLOT_WALK_TO, true);

			while (m_Scheduler.Exist(TASK_PLOT_WALK_TO))
				co_await 1.f;

			co_await TaskScheduler::NextFrame::Rank[1];
		}
	}

	Task Task_Plot_WalkOnPath(Vector const vecTarget) noexcept;

	Task Task_Anim_Intercepting(Activity activity) noexcept
	{
		seq_info_t const* seq{};

		if (!m_ActSequences[activity].empty())
			seq = UTIL_GetRandomOne(m_ActSequences[activity]);

		if (!seq)
			co_return;

		pev->sequence = seq->m_iSeqIdx;
		pev->animtime = gpGlobals->time;
		pev->framerate = seq->m_flFrameRate;
		pev->frame = 0;

		pev->maxspeed = seq->m_flGroundSpeed;

		// keep the task alive, occupying the channel.
		co_await seq->m_total_length;

		co_return;
	}

	Task Task_Debug_ShowPath(std::span<PathSegment const> seg, Vector const vecSrc) noexcept
	{
		static constexpr Vector VEC_OFS{ 0, 0, VEC_DUCK_HULL_MAX.z / 2.f };
		static constexpr std::array<std::string_view, NUM_TRAVERSE_TYPES+1> TRAV_MEANS =
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
			std::format("{} Segments in total.\n", seg.size()).c_str()
		);

		for (auto&& Seg : seg)
		{
			g_engfuncs.pfnServerPrint(
				std::format("    {}@ {: <6.1f}{: <6.1f}{: <6.1f}\n", TRAV_MEANS[Seg.how], Seg.pos.x, Seg.pos.y, Seg.pos.z).c_str()
			);
		}

		for (;;)
		{
			if (seg.size() >= 1)
			{
				UTIL_DrawBeamPoints(
					vecSrc + VEC_OFS,
					seg.front().pos + VEC_OFS,
					5, 255, 255, 255
				);
			}

			for (size_t i = 1; i < seg.size(); ++i)
			{
				auto& prev = seg[i - 1];
				auto& cur = seg[i];

/*
			if (prev.how != GO_LADDER_DOWN && prev.how != GO_LADDER_UP
				&& cur.how != GO_LADDER_DOWN && cur.how != GO_LADDER_UP)
			{
				continue;
			}
*/

				switch (cur.how)
				{
				case GO_LADDER_DOWN:
					UTIL_DrawBeamPoints(
						cur.ladder->m_top,
						cur.ladder->m_bottom,
						5, 128, 0, 0
					);
					break;

				case GO_LADDER_UP:
					UTIL_DrawBeamPoints(
						cur.ladder->m_bottom,
						cur.ladder->m_top,
						5, 0, 128, 0
					);
					break;

				case GO_JUMP:
					UTIL_DrawBeamPoints(
						prev.pos + VEC_OFS,
						cur.pos + VEC_OFS,
						5, 0, 0, 128
					);
					break;

					// Connect regular path with ladders.
				default:
					if (prev.how == GO_LADDER_UP)
					{
						UTIL_DrawBeamPoints(
							prev.ladder->m_top,
							cur.pos + VEC_OFS,
							5, 255, 255, 255
						);
					}
					else if (cur.how == GO_LADDER_UP)
					{
						UTIL_DrawBeamPoints(
							prev.pos + VEC_OFS,
							cur.ladder->m_bottom,
							5, 255, 255, 255
						);
					}
					else if (prev.how == GO_LADDER_DOWN)
					{
						UTIL_DrawBeamPoints(
							prev.ladder->m_bottom,
							cur.pos + VEC_OFS,
							5, 255, 255, 255
						);
					}
					else if (cur.how == GO_LADDER_DOWN)
					{
						UTIL_DrawBeamPoints(
							prev.pos + VEC_OFS,
							cur.ladder->m_top,
							5, 255, 255, 255
						);
					}
					else
					{
						UTIL_DrawBeamPoints(
							prev.pos + VEC_OFS,
							cur.pos + VEC_OFS,
							5, 255, 255, 255
						);
					}
					break;
				}

				co_await 0.01f;
			}

			UTIL_DrawBeamPoints(
				m_vecGoal,
				{ m_vecGoal.x, m_vecGoal.y, m_vecGoal.z + 36.f },
				5, 0, 255, 0
			);

			co_await 0.01f;	// avoid inf loop.
		}

		co_return;
	}

	// Static Precache

	static void PrepareActivityInfo(std::string_view szModel) noexcept;
};
