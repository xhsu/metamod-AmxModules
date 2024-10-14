export module BaseMonster;

import std;
import hlsdk;

import Prefab;
import Task;
import Models;

import Improvisational;
import LocalNav;

export enum ETaskIdMonster : std::uint64_t
{
	TASK_INIT = 1ull << 1,
	TASK_REMOVE = 1ull << 2,

	TASK_MOVE_TURNING = 1ull << 8,
	TASK_MOVE_WALKING = 1ull << 9,

	TASK_ANIM_TURNING = 1ull << 16,

	TASK_PLOT_WALK_TO = 1ull << 24,
	TASK_PLOT_PATROL = 1ull << 25,
};

export struct CBaseAI : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "mob_hgrunt";

	static inline decltype(GoldSrc::m_StudioInfo)::value_type::second_type* m_ModelInfo{};

	CNavPath m_path{};
	Vector m_vecGoal{};
	EHANDLE<CBaseEntity> m_pTargetEnt{};
	bool m_bYawReady : 1 { false };
	mutable bool m_fTargetEntHit : 1 { false };

	// Entity properties

	void Spawn() noexcept override
	{
		GoldSrc::CacheStudioModelInfo("models/hgrunt.mdl");
		m_ModelInfo = &GoldSrc::m_StudioInfo.at("models/hgrunt.mdl");

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
	}

	constexpr int Classify() noexcept override { return CLASS_HUMAN_MILITARY; }

	// Plots

	void Plot_PathToLocation(Vector const& vecTarget) noexcept
	{
		[[unlikely]]
		if (!m_path.Compute(pev->origin, vecTarget, HostagePathCost{}))
		{
			g_engfuncs.pfnServerPrint("No path found on AI!\n");
			return;
		}

		m_Scheduler.Enroll(Task_WalkOnPath(SimplifiedPath(dont_ignore_glass | dont_ignore_monsters)), TASK_PLOT_WALK_TO, true);
	}

	// Util

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

	inline bool IsAnimPlaying(std::string_view what) const noexcept
	{
		if (auto const it = m_ModelInfo->find(what); it != m_ModelInfo->end())
		{
			return pev->sequence == it->second.m_iSeqIdx;
		}

		// No info.
		return false;
	}


	// Path Verification

	auto SimplifiedPath(TRACE_FL fNoMonsters) noexcept -> decltype(this->m_path.Inspect())
	{
		auto const Path = m_path.Inspect();

		// Counting down from the farthest.
		for (size_t nCount = Path.size() - 1; auto&& Seg : Path | std::views::reverse)
		{
			// The node gets modified in the process.
			if (PathTraversable(pev->origin, &Seg.pos, fNoMonsters) != PTRAVELS_NO)
				return Path.subspan(nCount);

			--nCount;
		}

		return {};
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
				return PTRAVELS_NO;
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

	Task Task_Turn() noexcept;

	Task Task_Walk(Vector const vecTarget, bool const bShouldPlayIdleAtEnd = true) noexcept;

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
			Plot_PathToLocation(vecTarget);

			while (m_Scheduler.Exist(TASK_PLOT_WALK_TO))
				co_await 1.f;

			Plot_PathToLocation(vec);

			while (m_Scheduler.Exist(TASK_PLOT_WALK_TO))
				co_await 1.f;
		}
	}

	Task Task_WalkOnPath(std::span<PathSegment const> Path) noexcept
	{
		if (Path.empty())
			co_return;

		auto const itLast = Path.end() - 1;	// Not end, still accessable.

		for (auto it = Path.begin(); it != Path.end(); ++it)
		{
			pev->maxspeed = 60.f;
			m_Scheduler.Enroll(Task_Walk(it->pos, it == itLast), TASK_MOVE_WALKING, true);

			while (m_Scheduler.Exist(TASK_MOVE_WALKING))
				co_await (it == itLast ? 1.f : TaskScheduler::NextFrame::Rank[3]);

			co_await TaskScheduler::NextFrame::Rank[3];
		}
	}
};
