export module LocalNav;

import std;
import hlsdk;

import CBase;
import ConsoleVar;
import Task;

using std::uint8_t;
using std::uint32_t;
using std::int32_t;

export enum ETraversable
{
	PTRAVELS_NO,
	PTRAVELS_SLOPE,
	PTRAVELS_STEP,
	PTRAVELS_STEPJUMPABLE,
	PTRAVELS_MIDAIR,
};

export using node_index_t = int;
export inline constexpr node_index_t NODE_INVALID_EMPTY = { -1 };

// instead of MaxSlope, we are using the following max Z component of a unit normal
export inline constexpr float MaxUnitZSlope = 0.7f;

export inline constexpr float HOSTAGE_STEPSIZE = 26.0f;
inline constexpr float MAX_HOSTAGES_RESCUE_RADIUS = 256.0f; // rescue zones from legacy info_*
extern "C++" inline console_variable_t cvar_stepsize{"sv_stepsize", "NaNf",};	// owned by engine.


export struct localnode_t
{
	Vector vecLoc{};
	int32_t offsetX{};
	int32_t offsetY{};
	uint8_t bDepth{};
	bool fSearched{};
	node_index_t nindexParent{};
};

export struct CLocalNav
{
	CLocalNav() noexcept
		: m_pTargetEnt{ nullptr }, m_nodeArr{}
	{
		//m_hHostages.emplace_back(pOwner);
		m_nodeArr.resize(0x80);
	}
	CLocalNav(CBaseEntity* pOwner) noexcept
		: m_pOwner{ pOwner }, m_pTargetEnt{ nullptr }, m_nodeArr{}
	{
		//m_hHostages.emplace_back(pOwner);
		m_nodeArr.resize(0x80);
	}
	CLocalNav(CLocalNav const&) noexcept = delete;
	CLocalNav(CLocalNav &&) noexcept = delete;
	CLocalNav& operator=(CLocalNav const&) noexcept = delete;
	CLocalNav& operator=(CLocalNav &&) noexcept = delete;
	virtual ~CLocalNav() noexcept = default;

	void SetTargetEnt(CBaseEntity* pTarget) noexcept
	{
		if (pTarget)
			m_pTargetEnt = pTarget->edict();
		else
			m_pTargetEnt = nullptr;
	}

	node_index_t FindPath(Vector const& vecStart, Vector const& vecDest, float flTargetRadius, TRACE_FL fNoMonsters) noexcept
	{
		node_index_t nIndexBest = FindDirectPath(vecStart, vecDest, flTargetRadius, fNoMonsters);

		if (nIndexBest != NODE_INVALID_EMPTY)
		{
			return nIndexBest;
		}

		m_vecStartingLoc = vecStart;
		m_nindexAvailableNode = 0;

		AddPathNodes(NODE_INVALID_EMPTY, fNoMonsters);
		nIndexBest = GetBestNode(vecStart, vecDest);

		while (nIndexBest != NODE_INVALID_EMPTY)
		{
			auto& node = m_nodeArr[nIndexBest];
			node.fSearched = true;

			auto const& vecNodeLoc = node.vecLoc;
			auto const flDistToDest = (vecDest - node.vecLoc).Length2D();

			if (flDistToDest <= flTargetRadius)
				break;

			if (flDistToDest <= HOSTAGE_STEPSIZE)
				break;

			if (((flDistToDest - flTargetRadius) > ((m_nodeArr.size() - m_nindexAvailableNode) * HOSTAGE_STEPSIZE))
				|| m_nindexAvailableNode == m_nodeArr.size())
			{
				nIndexBest = NODE_INVALID_EMPTY;
				break;
			}

			AddPathNodes(nIndexBest, fNoMonsters);
			nIndexBest = GetBestNode(vecNodeLoc, vecDest);
		}

		if (m_nindexAvailableNode <= 10)
			m_NodeValue += 2;

		else if (m_nindexAvailableNode <= 20)
			m_NodeValue += 4;

		else if (m_nindexAvailableNode <= 30)
			m_NodeValue += 8;

		else if (m_nindexAvailableNode <= 40)
			m_NodeValue += 13;

		else if (m_nindexAvailableNode <= 50)
			m_NodeValue += 19;

		else if (m_nindexAvailableNode <= 60)
			m_NodeValue += 26;

		else if (m_nindexAvailableNode <= 70)
			m_NodeValue += 34;

		else if (m_nindexAvailableNode <= 80)
			m_NodeValue += 43;

		else if (m_nindexAvailableNode <= 90)
			m_NodeValue += 53;

		else if (m_nindexAvailableNode <= 100)
			m_NodeValue += 64;

		else if (m_nindexAvailableNode <= 110)
			m_NodeValue += 76;

		else if (m_nindexAvailableNode <= 120)
			m_NodeValue += 89;

		else if (m_nindexAvailableNode <= 130)
			m_NodeValue += 103;

		else if (m_nindexAvailableNode <= 140)
			m_NodeValue += 118;

		else if (m_nindexAvailableNode <= 150)
			m_NodeValue += 134;

		else if (m_nindexAvailableNode <= 160)
			m_NodeValue += 151;
		else
			m_NodeValue += 169;

		return nIndexBest;
	}

	void SetupPathNodes(node_index_t nindex, std::vector<Vector>* vecNodes) const noexcept
	{
		node_index_t nCurrentIndex = nindex;
		vecNodes->clear();

		while (nCurrentIndex != NODE_INVALID_EMPTY)
		{
			auto const& nodeCurrent = m_nodeArr[nCurrentIndex];
			vecNodes->emplace_back(nodeCurrent.vecLoc);

			nCurrentIndex = nodeCurrent.nindexParent;
		}
	}

	node_index_t GetFurthestTraversableNode(Vector const& vecStartingLoc, std::vector<Vector>* prgvecNodes, TRACE_FL fNoMonsters) const noexcept
	{
		for (int nCount = 0; auto&& vecNode : *prgvecNodes)
		{
			if (PathTraversable(vecStartingLoc, &vecNode, fNoMonsters) != PTRAVELS_NO)
				return nCount;

			++nCount;
		}

		return NODE_INVALID_EMPTY;
	}

	ETraversable PathTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters) const noexcept
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
				return m_pOwner->pev->movetype == MOVETYPE_FLY ? PTRAVELS_MIDAIR : PTRAVELS_NO;
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
		g_engfuncs.pfnTraceMonsterHull(m_pOwner->edict(), vecOrigin, vecDest, fNoMonsters, m_pOwner->edict(), tr);

		if (tr->fStartSolid)
			return false;

		if (tr->flFraction == 1.0f)
			return true;

		if (tr->pHit == m_pTargetEnt.Get())
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

	node_index_t AddNode(node_index_t nindexParent, Vector const& vecLoc, int offsetX = 0, int offsetY = 0, uint8_t bDepth = 0) noexcept
	{
		if (m_nindexAvailableNode == std::ssize(m_nodeArr))
			return NODE_INVALID_EMPTY;

		auto& nodeNew = m_nodeArr[m_nindexAvailableNode];

		nodeNew.vecLoc = vecLoc;
		nodeNew.offsetX = offsetX;
		nodeNew.offsetY = offsetY;
		nodeNew.bDepth = bDepth;
		nodeNew.fSearched = false;
		nodeNew.nindexParent = nindexParent;

		return m_nindexAvailableNode++;
	}

	node_index_t NodeExists(int offsetX, int offsetY) const noexcept
	{
		node_index_t nindexCurrent = NODE_INVALID_EMPTY;

		for (nindexCurrent = m_nindexAvailableNode - 1; nindexCurrent != NODE_INVALID_EMPTY; nindexCurrent--)
		{
			auto const& nodeCurrent = m_nodeArr[nindexCurrent];

			if (nodeCurrent.offsetX == offsetX && nodeCurrent.offsetY == offsetY)
			{
				break;
			}
		}

		return nindexCurrent;
	}

	void AddPathNodes(node_index_t nindexSource, TRACE_FL fNoMonsters) noexcept
	{
		AddPathNode(nindexSource, 1, 0, fNoMonsters);
		AddPathNode(nindexSource, -1, 0, fNoMonsters);
		AddPathNode(nindexSource, 0, 1, fNoMonsters);
		AddPathNode(nindexSource, 0, -1, fNoMonsters);
		AddPathNode(nindexSource, 1, 1, fNoMonsters);
		AddPathNode(nindexSource, 1, -1, fNoMonsters);
		AddPathNode(nindexSource, -1, 1, fNoMonsters);
		AddPathNode(nindexSource, -1, -1, fNoMonsters);
	}

	void AddPathNode(node_index_t nindexSource, int offsetX, int offsetY, TRACE_FL fNoMonsters) noexcept
	{
		Vector vecSource{}, vecDest{};
		int offsetXAbs{}, offsetYAbs{};
		uint8_t bDepth{};

		if (nindexSource == NODE_INVALID_EMPTY)
		{
			bDepth = 1;

			offsetXAbs = offsetX;
			offsetYAbs = offsetY;

			vecSource = m_vecStartingLoc;
			vecDest = vecSource + Vector(double(offsetX) * HOSTAGE_STEPSIZE, double(offsetY) * HOSTAGE_STEPSIZE, 0);
		}
		else
		{
			auto nodeCurrent = &m_nodeArr[nindexSource];
			offsetXAbs = offsetX + nodeCurrent->offsetX;
			offsetYAbs = offsetY + nodeCurrent->offsetY;

			if (m_nindexAvailableNode >= std::ssize(m_nodeArr))
			{
				m_nodeArr.resize(m_nindexAvailableNode * 2);
				g_engfuncs.pfnServerPrint(std::format("ARR EXPANDED: {}\n", m_nindexAvailableNode * 2).c_str());
			}

			auto nodeSource = &m_nodeArr[m_nindexAvailableNode];	// we actually need ptr here.

			// if there exists a node, then to ignore adding a the new node
			if (NodeExists(offsetXAbs, offsetYAbs) != NODE_INVALID_EMPTY)
			{
				return;
			}

			vecSource = nodeCurrent->vecLoc;
			vecDest = vecSource + Vector((double(offsetX) * HOSTAGE_STEPSIZE), (double(offsetY) * HOSTAGE_STEPSIZE), 0);

			if (m_nindexAvailableNode)
			{
				auto nindexCurrent = m_nindexAvailableNode;

				do
				{
					nodeSource--;
					nindexCurrent--;

					offsetX = (nodeSource->offsetX - offsetXAbs);

					if (offsetX >= 0)
					{
						if (offsetX > 1)
						{
							continue;
						}
					}
					else
					{
						if (-offsetX > 1)
						{
							continue;
						}
					}

					offsetY = (nodeSource->offsetY - offsetYAbs);

					if (offsetY >= 0)
					{
						if (offsetY > 1)
						{
							continue;
						}
					}
					else
					{
						if (-offsetY > 1)
						{
							continue;
						}
					}

					if (PathTraversable(nodeSource->vecLoc, &vecDest, fNoMonsters) != PTRAVELS_NO)
					{
						nodeCurrent = nodeSource;
						nindexSource = nindexCurrent;
					}
				} while (nindexCurrent);
			}

			vecSource = nodeCurrent->vecLoc;
			bDepth = (nodeCurrent->bDepth + 1) & 0xff;
		}

		if (PathTraversable(vecSource, &vecDest, fNoMonsters) != PTRAVELS_NO)
		{
			AddNode(nindexSource, vecDest, offsetXAbs, offsetYAbs, bDepth);
		}
	}

	node_index_t GetBestNode(Vector const& vecOrigin, Vector const& vecDest) const noexcept
	{
		auto nindexBest = NODE_INVALID_EMPTY;
		auto nindexCurrent = 0;
		auto flBestVal = 1000000.0;

		while (nindexCurrent < m_nindexAvailableNode)
		{
			auto const& nodeCurrent = m_nodeArr[nindexCurrent];

			if (!nodeCurrent.fSearched)
			{
				auto flZDiff = -1.0;
				auto const flDistFromStart = (vecDest - nodeCurrent.vecLoc).Length();
				auto const flDistToDest = nodeCurrent.vecLoc.z - vecDest.z;

				if (flDistToDest >= 0.0)
				{
					flZDiff = 1.0;
				}

				if ((flDistToDest * flZDiff) <= (float)cvar_stepsize)
					flZDiff = 1.0;
				else
					flZDiff = 1.25;

				auto const flCurrentVal = flZDiff * (double(nodeCurrent.bDepth) * HOSTAGE_STEPSIZE + flDistFromStart);
				if (flCurrentVal < flBestVal)
				{
					flBestVal = flCurrentVal;
					nindexBest = nindexCurrent;
				}
			}

			nindexCurrent++;
		}

		return nindexBest;
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

	node_index_t FindDirectPath(Vector const& vecStart, Vector const& vecDest, float flTargetRadius, TRACE_FL fNoMonsters) noexcept
	{
		auto const vecPathDir = (vecStart - vecDest).Normalize();
		auto vecActualDest = vecDest - (vecPathDir * flTargetRadius);

		if (PathTraversable(vecStart, &vecActualDest, fNoMonsters) == PTRAVELS_NO)
		{
			return NODE_INVALID_EMPTY;
		}

		auto nIndexLast = NODE_INVALID_EMPTY;
		auto vecNodeLoc = vecStart;
		m_nindexAvailableNode = 0;

		while ((vecNodeLoc - vecActualDest).Length2D() >= HOSTAGE_STEPSIZE)
		{
			node_index_t nindexCurrent = nIndexLast;

			vecNodeLoc = vecNodeLoc + (vecPathDir * HOSTAGE_STEPSIZE);
			nIndexLast = AddNode(nindexCurrent, vecNodeLoc);

			if (nIndexLast == NODE_INVALID_EMPTY)
				break;
		}

		return nIndexLast;
	}

	bool LadderHit(Vector const& vecSource, Vector const& vecDest, TraceResult* tr) const noexcept
	{
		auto const vecAngles = (-tr->vecPlaneNormal).VectorAngles();
		auto [vecFwd, vecRight, vecUp] = vecAngles.AngleVectors();
		auto vecOrigin = tr->vecEndPos + (vecFwd * 15) + (vecUp * 36);

		if (g_engfuncs.pfnPointContents(vecOrigin) == CONTENTS_LADDER)
			return true;

		vecOrigin = tr->vecEndPos + (vecFwd * 15) - (vecUp * 36);

		if (g_engfuncs.pfnPointContents(vecOrigin) == CONTENTS_LADDER)
			return true;

		vecOrigin = tr->vecEndPos + (vecFwd * 15) + (vecRight * 16) + (vecUp * 36);

		if (g_engfuncs.pfnPointContents(vecOrigin) == CONTENTS_LADDER)
			return true;

		vecOrigin = tr->vecEndPos + (vecFwd * 15) - (vecRight * 16) + (vecUp * 36);

		if (g_engfuncs.pfnPointContents(vecOrigin) == CONTENTS_LADDER)
			return true;

		vecOrigin = tr->vecEndPos + (vecFwd * 15) + (vecRight * 16) - (vecUp * 36);

		if (g_engfuncs.pfnPointContents(vecOrigin) == CONTENTS_LADDER)
			return true;

		vecOrigin = tr->vecEndPos + (vecFwd * 15) - (vecRight * 16) + (vecUp * 36);

		if (g_engfuncs.pfnPointContents(vecOrigin) == CONTENTS_LADDER)
			return true;

		return false;
	}

/*
	static void Think() noexcept
	{
		if (gpGlobals->time >= m_flNextCvarCheck)
		{
			m_flStepSize = (float)cvar_stepsize;
			m_flNextCvarCheck = gpGlobals->time + 1.0f;
		}

		HostagePrethink();

		float flElapsedTime = gpGlobals->time - m_flLastThinkTime;
		m_NodeValue -= int(flElapsedTime * 250.f);
		m_flLastThinkTime = gpGlobals->time;

		if (m_NodeValue < 0)
			m_NodeValue = 0;

		else if (m_NodeValue > 17)
			return;

		if (m_NumRequest)
		{
			auto& hHostage = m_hQueue[m_CurRequest];
			while (!hHostage && m_NumRequest > 0)
			{
				if (++m_CurRequest == m_hQueue.max_size())
					m_CurRequest = 0;

				m_NumRequest--;
				if (m_NumRequest <= 0)
				{
					hHostage = nullptr;
					break;
				}

				hHostage = m_hQueue[m_CurRequest];
			}

			if (hHostage)
			{
				if (++m_CurRequest == m_hQueue.max_size())
					m_CurRequest = 0;

				m_NumRequest--;
				hHostage->NavReady();
			}
		}
	}
	static void RequestNav(CBaseEntity* pCaller) noexcept
	{
		auto& curr = m_CurRequest;

		if (m_NodeValue <= 17 && !m_NumRequest)
		{
			pCaller->NavReady();
			return;
		}

		if (m_NumRequest >= m_hQueue.max_size())
		{
			return;
		}

		for (int i = 0; i < m_NumRequest; i++)
		{
			if (m_hQueue[curr] == pCaller)
				return;

			if (++curr == m_hQueue.max_size())
				curr = 0;
		}

		m_hQueue[curr] = pCaller;
		m_NumRequest++;
	}
	static void Reset() noexcept
	{
		m_flNextCvarCheck = 0.0f;
		m_flLastThinkTime = 0.0f;

		m_NumRequest = 0;
		m_CurRequest = 0;
		m_NumHostages = 0;
		m_NodeValue = 0;
	}
	static void HostagePrethink() noexcept
	{
		for (auto&& Hostage : m_hHostages)
		{
			if (Hostage)
				Hostage->PreThink();
		}
	}
	static inline float m_flStepSize{};
*/

	static Task Task_LocalNav() noexcept
	{
		for (;;)
		{
			float flElapsedTime = gpGlobals->time - m_flLastThinkTime;
			m_NodeValue -= int(flElapsedTime * 250.f);
			m_flLastThinkTime = gpGlobals->time;

			if (m_NodeValue < 0)
				m_NodeValue = 0;

			co_await TaskScheduler::NextFrame::Rank[0];
		}
	}

//private:
	//static inline std::array<EHANDLE<CBaseEntity>, 20> m_hQueue{};
	//static inline std::vector<EHANDLE<CBaseEntity>> m_hHostages{};
	//static inline int m_CurRequest{};
	//static inline int m_NumRequest{};
	//static inline int m_NumHostages{ 0 };
	static inline int m_NodeValue{};
	//static inline float m_flNextCvarCheck{};
	static inline float m_flLastThinkTime{};

	EHANDLE<CBaseEntity> m_pOwner{};
	EHANDLE<CBaseEntity> m_pTargetEnt{};
	mutable bool m_fTargetEntHit{ false };
	std::vector<localnode_t> m_nodeArr{};
	node_index_t m_nindexAvailableNode{};
	Vector m_vecStartingLoc{};
};
