module;

#include <assert.h>

export module MonsterNav;

import std;
import hlsdk;

import CBase;
import Task;	// testing part only.

// an array of waypoints makes up the monster's route. 
// !!!LATER- this declaration doesn't belong in this file.
struct WayPoint_t
{
	Vector	vecLocation{};
	int		iType{};
};

// these MoveFlag values are assigned to a WayPoint's TYPE in order to demonstrate the 
// type of movement the monster should use to get there.
export enum EMoveFlags
{
	bits_MF_TO_TARGETENT = 1 << 0,	// local move to targetent.
	bits_MF_TO_ENEMY = 1 << 1,		// local move to enemy
	bits_MF_TO_COVER = 1 << 2,		// local move to a hiding place
	bits_MF_TO_DETOUR = 1 << 3,		// local move to detour point.
	bits_MF_TO_PATHCORNER = 1 << 4,	// local move to a path corner
	bits_MF_TO_NODE = 1 << 5,		// local move to a node
	bits_MF_TO_LOCATION = 1 << 6,	// local move to an arbitrary point
	bits_MF_IS_GOAL = 1 << 7,		// this waypoint is the goal of the whole move.
	bits_MF_DONT_SIMPLIFY = 1 << 8,	// Don't let the route code simplify this waypoint

	// If you define any flags that aren't _TO_ flags, add them here so we can mask
	// them off when doing compares.
	bits_MF_NOT_TO_MASK = bits_MF_IS_GOAL | bits_MF_DONT_SIMPLIFY,
};

export enum EMoveGoals
{
	MOVEGOAL_NONE = 0,
	MOVEGOAL_TARGETENT = bits_MF_TO_TARGETENT,
	MOVEGOAL_ENEMY = bits_MF_TO_ENEMY,
	MOVEGOAL_PATHCORNER = bits_MF_TO_PATHCORNER,
	MOVEGOAL_LOCATION = bits_MF_TO_LOCATION,
	MOVEGOAL_NODE = bits_MF_TO_NODE,
};


// CHECKLOCALMOVE result types 
export enum ELocalMoveRes
{
	LOCALMOVE_INVALID = 0,					// move is not possible;
	LOCALMOVE_INVALID_DONT_TRIANGULATE = 1,	// move is not possible, don't try to triangulate;
	LOCALMOVE_VALID = 2,					// move is possible;
};

constexpr auto MAX_PATH_SIZE = 10; // max number of nodes available for a path.;

/*
inline struct
{
	constexpr int FindNearestNode(auto&&...) const noexcept { return 0; }
	constexpr int HullIndex(auto&&...) const noexcept { return 0; }
	constexpr int FindShortestPath(auto&&...) const noexcept { return 0; }

} WorldGraph;
*/


export struct MonsterNav
{
	//=========================================================
	// FTriangulate - tries to overcome local obstacles by 
	// triangulating a path around them.
	//
	// iApexDist is how far the obstruction that we are trying
	// to triangulate around is from the monster.
	//=========================================================
	bool FTriangulate(const Vector& vecStart, const Vector& vecEnd, float flDist, CBaseEntity* pTargetEnt, Vector* pApex) const noexcept
	{
		// If the hull width is less than 24, use 24 because CheckLocalMove uses a min of 24
		auto sizeX = pev->size.x;
		if (sizeX < 24.0)
			sizeX = 24.0;
		else if (sizeX > 48.0)
			sizeX = 48.0;
		auto const sizeZ = pev->size.z;
		//if (sizeZ < 24.0)
		//	sizeZ = 24.0;

		auto const vecForward = (vecEnd - vecStart).Normalize();

		Vector vecDirUp(0, 0, 1);
		auto vecDir = CrossProduct(vecForward, vecDirUp);

		// start checking right about where the object is, picking two equidistant starting points, one on
		// the left, one on the right. As we progress through the loop, we'll push these away from the obstacle, 
		// hoping to find a way around on either side. pev->size.x is added to the ApexDist in order to help select
		// an apex point that insures that the monster is sufficiently past the obstacle before trying to turn back
		// onto its original course.

		// the spot we'll try to triangulate to on the left
		auto vecLeft = pev->origin + (vecForward * (flDist + sizeX)) - vecDir * (sizeX * 3);
		// the spot we'll try to triangulate to on the right
		auto vecRight = pev->origin + (vecForward * (flDist + sizeX)) + vecDir * (sizeX * 3);

		Vector vecTop{};	// the spot we'll try to triangulate to on the top
		Vector vecBottom{};// the spot we'll try to triangulate to on the bottom
		if (pev->movetype == MOVETYPE_FLY)
		{
			vecTop = pev->origin + (vecForward * flDist) + (vecDirUp * sizeZ * 3);
			vecBottom = pev->origin + (vecForward * flDist) - (vecDirUp * sizeZ * 3);
		}

		// the spot that we'll move to after hitting the triangulated point, before moving on to our normal goal.
		auto const vecFarSide = m_Route[m_iRouteIndex].vecLocation;

		vecDir = vecDir * sizeX * 2;
		if (pev->movetype == MOVETYPE_FLY)
			vecDirUp = vecDirUp * sizeZ * 2;

		for (auto i = 0; i < 8; i++)
		{
			// Debug, Draw the triangulation
#if 0
			MESSAGE_BEGIN(MSG_BROADCAST, SVC_TEMPENTITY);
			WRITE_BYTE(TE_SHOWLINE);
			WRITE_COORD(pev->origin.x);
			WRITE_COORD(pev->origin.y);
			WRITE_COORD(pev->origin.z);
			WRITE_COORD(vecRight.x);
			WRITE_COORD(vecRight.y);
			WRITE_COORD(vecRight.z);
			MESSAGE_END();

			MESSAGE_BEGIN(MSG_BROADCAST, SVC_TEMPENTITY);
			WRITE_BYTE(TE_SHOWLINE);
			WRITE_COORD(pev->origin.x);
			WRITE_COORD(pev->origin.y);
			WRITE_COORD(pev->origin.z);
			WRITE_COORD(vecLeft.x);
			WRITE_COORD(vecLeft.y);
			WRITE_COORD(vecLeft.z);
			MESSAGE_END();
#endif

#if 0
			if (pev->movetype == MOVETYPE_FLY)
			{
				MESSAGE_BEGIN(MSG_BROADCAST, SVC_TEMPENTITY);
				WRITE_BYTE(TE_SHOWLINE);
				WRITE_COORD(pev->origin.x);
				WRITE_COORD(pev->origin.y);
				WRITE_COORD(pev->origin.z);
				WRITE_COORD(vecTop.x);
				WRITE_COORD(vecTop.y);
				WRITE_COORD(vecTop.z);
				MESSAGE_END();

				MESSAGE_BEGIN(MSG_BROADCAST, SVC_TEMPENTITY);
				WRITE_BYTE(TE_SHOWLINE);
				WRITE_COORD(pev->origin.x);
				WRITE_COORD(pev->origin.y);
				WRITE_COORD(pev->origin.z);
				WRITE_COORD(vecBottom.x);
				WRITE_COORD(vecBottom.y);
				WRITE_COORD(vecBottom.z);
				MESSAGE_END();
			}
#endif

			if (CheckLocalMove(pev->origin, vecRight, pTargetEnt, nullptr) == LOCALMOVE_VALID)
			{
				if (CheckLocalMove(vecRight, vecFarSide, pTargetEnt, nullptr) == LOCALMOVE_VALID)
				{
					if (pApex)
					{
						*pApex = vecRight;
					}

					return true;
				}
			}
			if (CheckLocalMove(pev->origin, vecLeft, pTargetEnt, nullptr) == LOCALMOVE_VALID)
			{
				if (CheckLocalMove(vecLeft, vecFarSide, pTargetEnt, nullptr) == LOCALMOVE_VALID)
				{
					if (pApex)
					{
						*pApex = vecLeft;
					}

					return true;
				}
			}

			if (pev->movetype == MOVETYPE_FLY)
			{
				if (CheckLocalMove(pev->origin, vecTop, pTargetEnt, nullptr) == LOCALMOVE_VALID)
				{
					if (CheckLocalMove(vecTop, vecFarSide, pTargetEnt, nullptr) == LOCALMOVE_VALID)
					{
						if (pApex)
						{
							*pApex = vecTop;
							//ALERT(at_aiconsole, "triangulate over\n");
						}

						return true;
					}
				}
#if 1
				if (CheckLocalMove(pev->origin, vecBottom, pTargetEnt, nullptr) == LOCALMOVE_VALID)
				{
					if (CheckLocalMove(vecBottom, vecFarSide, pTargetEnt, nullptr) == LOCALMOVE_VALID)
					{
						if (pApex)
						{
							*pApex = vecBottom;
							//ALERT(at_aiconsole, "triangulate under\n");
						}

						return true;
					}
				}
#endif
			}

			vecRight += vecDir;
			vecLeft -= vecDir;
			if (pev->movetype == MOVETYPE_FLY)
			{
				vecTop += vecDirUp;
				vecBottom -= vecDirUp;
			}
		}

		return false;
	}


	constexpr bool ShouldSimplify(int routeType) const noexcept
	{
		routeType &= ~bits_MF_IS_GOAL;

		if ((routeType == bits_MF_TO_PATHCORNER) || (routeType & bits_MF_DONT_SIMPLIFY))
			return false;

		return true;
	}

	//=========================================================
	// RouteSimplify
	//
	// Attempts to make the route more direct by cutting out
	// unnecessary nodes & cutting corners.
	//
	//=========================================================
	void RouteSimplify(CBaseEntity* pTargetEnt) noexcept
	{
		// #PF_BUGBUG: this doesn't work 100% yet

		// Any points except the ends can turn into 2 points in the simplified route
		std::array<WayPoint_t, ROUTE_SIZE * 2> outRoute{};

		auto count = 0;

		for (auto i = m_iRouteIndex; i < ROUTE_SIZE; i++)
		{
			if (!m_Route[i].iType)
				break;
			else
				count++;

			if (m_Route[i].iType & bits_MF_IS_GOAL)
				break;
		}
		// Can't simplify a direct route!
		if (count < 2)
		{
//			DrawRoute( pev, m_Route, m_iRouteIndex, 0, 0, 255 );
			return;
		}

		auto outCount = 0;
		auto vecStart = pev->origin;
		auto i{ 0 };
		for (; i < count - 1; i++)
		{
			// Don't eliminate path_corners
			if (!ShouldSimplify(m_Route[m_iRouteIndex + i].iType))
			{
				outRoute[outCount] = m_Route[m_iRouteIndex + i];
				outCount++;
			}
			else if (CheckLocalMove(vecStart, m_Route[m_iRouteIndex + i + 1].vecLocation, pTargetEnt, nullptr) == LOCALMOVE_VALID)
			{
				// Skip vert
				continue;
			}
			else
			{
				// Halfway between this and next
				auto const vecTest = (m_Route[m_iRouteIndex + i + 1].vecLocation + m_Route[m_iRouteIndex + i].vecLocation) * 0.5;

				// Halfway between this and previous
				auto const vecSplit = (m_Route[m_iRouteIndex + i].vecLocation + vecStart) * 0.5;

				int iType = (m_Route[m_iRouteIndex + i].iType | bits_MF_TO_DETOUR) & ~bits_MF_NOT_TO_MASK;
				if (CheckLocalMove(vecStart, vecTest, pTargetEnt, nullptr) == LOCALMOVE_VALID)
				{
					outRoute[outCount].iType = iType;
					outRoute[outCount].vecLocation = vecTest;
				}
				else if (CheckLocalMove(vecSplit, vecTest, pTargetEnt, nullptr) == LOCALMOVE_VALID)
				{
					outRoute[outCount].iType = iType;
					outRoute[outCount].vecLocation = vecSplit;
					outRoute[outCount + 1].iType = iType;
					outRoute[outCount + 1].vecLocation = vecTest;
					outCount++; // Adding an extra point
				}
				else
				{
					outRoute[outCount] = m_Route[m_iRouteIndex + i];
				}
			}
			// Get last point
			vecStart = outRoute[outCount].vecLocation;
			outCount++;
		}
		assert(i < count);
		outRoute[outCount] = m_Route[m_iRouteIndex + i];
		outCount++;

		// Terminate
		outRoute[outCount].iType = 0;
		assert(outCount < (ROUTE_SIZE * 2));

		// Copy the simplified route, disable for testing
		m_iRouteIndex = 0;
		for (i = 0; i < ROUTE_SIZE && i < outCount; i++)
		{
			m_Route[i] = outRoute[i];
		}

		// Terminate route
		if (i < ROUTE_SIZE)
			m_Route[i].iType = 0;

		// Debug, test movement code
#if 0
//		if ( CVAR_GET_FLOAT( "simplify" ) != 0 )
		DrawRoute(pev, outRoute, 0, 255, 0, 0);
		//	else
		DrawRoute(pev, m_Route, m_iRouteIndex, 0, 255, 0);
#endif
	}

	//=========================================================
	// FGetNodeRoute - tries to build an entire node path from
	// the callers origin to the passed vector. If this is 
	// possible, ROUTE_SIZE waypoints will be copied into the
	// callers m_Route. true is returned if the operation 
	// succeeds (path is valid) or false if failed (no path 
	// exists )
	//=========================================================
/*
	bool FGetNodeRoute(Vector const& vecDest) noexcept
	{
		auto iSrcNode = WorldGraph.FindNearestNode(pev->origin, this);
		auto iDestNode = WorldGraph.FindNearestNode(vecDest, this);

		if (iSrcNode == -1)
		{
			// no node nearest self
	//		ALERT ( at_aiconsole, "FGetNodeRoute: No valid node near self!\n" );
			return false;
		}
		else if (iDestNode == -1)
		{
			// no node nearest target
	//		ALERT ( at_aiconsole, "FGetNodeRoute: No valid node near target!\n" );
			return false;
		}

		// valid src and dest nodes were found, so it's safe to proceed with
		// find shortest path
		std::array<int, MAX_PATH_SIZE> iPath{};
		auto iNodeHull = WorldGraph.HullIndex(this); // make this a monster virtual function
		auto iResult = WorldGraph.FindShortestPath(iPath, iSrcNode, iDestNode, iNodeHull, m_afCapability);

		if (!iResult)
		{
#if 1
			g_engfuncs.pfnAlertMessage(at_aiconsole, "No Path from %d to %d!\n", iSrcNode, iDestNode);
			return false;
#else
			BOOL bRoutingSave = WorldGraph.m_fRoutingComplete;
			WorldGraph.m_fRoutingComplete = false;
			iResult = WorldGraph.FindShortestPath(iPath, iSrcNode, iDestNode, iNodeHull, m_afCapability);
			WorldGraph.m_fRoutingComplete = bRoutingSave;
			if (!iResult)
			{
				ALERT(at_aiconsole, "No Path from %d to %d!\n", iSrcNode, iDestNode);
				return false;
			}
			else
			{
				ALERT(at_aiconsole, "Routing is inconsistent!");
			}
#endif
		}

		// there's a valid path within iPath now, so now we will fill the route array
		// up with as many of the waypoints as it will hold.

		// don't copy ROUTE_SIZE entries if the path returned is shorter
		// than ROUTE_SIZE!!!
		int iNumToCopy{};
		if (iResult < ROUTE_SIZE)
		{
			iNumToCopy = iResult;
		}
		else
		{
			iNumToCopy = ROUTE_SIZE;
		}

		for (auto i = 0; i < iNumToCopy; i++)
		{
			m_Route[i].vecLocation = WorldGraph.m_pNodes[iPath[i]].m_vecOrigin;
			m_Route[i].iType = bits_MF_TO_NODE;
		}

		if (iNumToCopy < ROUTE_SIZE)
		{
			m_Route[iNumToCopy].vecLocation = vecDest;
			m_Route[iNumToCopy].iType |= bits_MF_IS_GOAL;
		}

		return true;
	}
*/


	//=========================================================
	// CheckLocalMove - returns true if the caller can walk a 
	// straight line from its current origin to the given 
	// location. If so, don't use the node graph!
	//
	// if a valid pointer to a int is passed, the function
	// will fill that int with the distance that the check 
	// reached before hitting something. THIS ONLY HAPPENS
	// IF THE LOCAL MOVE CHECK FAILS!
	//
	// !!!PERFORMANCE - should we try to load balance this?
	// DON"T USE SETORIGIN! 
	//=========================================================
#define	LOCAL_STEP_SIZE	16
	ELocalMoveRes CheckLocalMove(const Vector& vecStart, const Vector& vecEnd, CBaseEntity* pTarget, float* pflDist) const noexcept
	{
		auto const vecStartPos = pev->origin;	// record monster's position before trying the move
		auto const flYaw = g_engfuncs.pfnVecToYaw(vecEnd - vecStart);// build a yaw that points to the goal.
		auto const flDist = (vecEnd - vecStart).Length2D();// get the distance.
		auto iReturn = LOCALMOVE_VALID;	// assume everything will be ok.

		// move the monster to the start of the local move that's to be checked.
		g_engfuncs.pfnSetOrigin(pev->pContainingEntity, vecStart);// !!!BUGBUG - won't this fire triggers? - nope, SetOrigin doesn't fire

		if (!(pev->flags & (FL_FLY | FL_SWIM)))
		{
			g_engfuncs.pfnDropToFloor(pev->pContainingEntity);	//make sure monster is on the floor!
		}

		//pev->origin.z = vecStartPos.z;//!!!HACKHACK

//		pev->origin = vecStart;

/*
		if ( flDist > 1024 )
		{
			// !!!PERFORMANCE - this operation may be too CPU intensive to try checks this large.
			// We don't lose much here, because a distance this great is very likely
			// to have something in the way.

			// since we've actually moved the monster during the check, undo the move.
			pev->origin = vecStartPos;
			return false;
		}
*/
	// this loop takes single steps to the goal.
		for (std::remove_cvref_t<decltype(flDist)> flStep = 0; flStep < flDist; flStep += LOCAL_STEP_SIZE)
		{
			double stepSize = LOCAL_STEP_SIZE;

			if ((flStep + LOCAL_STEP_SIZE) >= (flDist - 1))
				stepSize = (flDist - flStep) - 1;

//			UTIL_ParticleEffect ( pev->origin, g_vecZero, 255, 25 );

			if (!g_engfuncs.pfnWalkMove(pev->pContainingEntity, flYaw, (float)stepSize, WALKMOVE_CHECKONLY))
			{// can't take the next step, fail!

				if (pflDist != nullptr)
				{
					*pflDist = (float)flStep;
				}
				if (pTarget && pTarget->edict() == gpGlobals->trace_ent)
				{
					// if this step hits target ent, the move is legal.
					iReturn = LOCALMOVE_VALID;
					break;
				}
				else
				{
					// If we're going toward an entity, and we're almost getting there, it's OK.
//				if ( pTarget && fabs( flDist - iStep ) < LOCAL_STEP_SIZE )
//					fReturn = true;
//				else
					iReturn = LOCALMOVE_INVALID;
					break;
				}

			}
		}

		if (iReturn == LOCALMOVE_VALID && !(pev->flags & (FL_FLY | FL_SWIM)) && (!pTarget || (pTarget->pev->flags & FL_ONGROUND)))
		{
			// The monster can move to a spot UNDER the target, but not to it. Don't try to triangulate, go directly to the node graph.
			// UNDONE: Magic # 64 -- this used to be pev->size.z but that won't work for small creatures like the headcrab
			if (fabs(vecEnd.z - pev->origin.z) > 64)
			{
				iReturn = LOCALMOVE_INVALID_DONT_TRIANGULATE;
			}
		}
		/*
		// uncommenting this block will draw a line representing the nearest legal move.
		WRITE_BYTE(MSG_BROADCAST, SVC_TEMPENTITY);
		WRITE_BYTE(MSG_BROADCAST, TE_SHOWLINE);
		WRITE_COORD(MSG_BROADCAST, pev->origin.x);
		WRITE_COORD(MSG_BROADCAST, pev->origin.y);
		WRITE_COORD(MSG_BROADCAST, pev->origin.z);
		WRITE_COORD(MSG_BROADCAST, vecStart.x);
		WRITE_COORD(MSG_BROADCAST, vecStart.y);
		WRITE_COORD(MSG_BROADCAST, vecStart.z);
		*/

		// since we've actually moved the monster during the check, undo the move.
		g_engfuncs.pfnSetOrigin(pev->pContainingEntity, vecStartPos);

		return iReturn;
	}


	int RouteClassify(int iMoveFlag) noexcept
	{
		auto movementGoal = MOVEGOAL_NONE;

		if (iMoveFlag & bits_MF_TO_TARGETENT)
			movementGoal = MOVEGOAL_TARGETENT;
		else if (iMoveFlag & bits_MF_TO_ENEMY)
			movementGoal = MOVEGOAL_ENEMY;
		else if (iMoveFlag & bits_MF_TO_PATHCORNER)
			movementGoal = MOVEGOAL_PATHCORNER;
		else if (iMoveFlag & bits_MF_TO_NODE)
			movementGoal = MOVEGOAL_NODE;
		else if (iMoveFlag & bits_MF_TO_LOCATION)
			movementGoal = MOVEGOAL_LOCATION;

		return movementGoal;
	}

	//=========================================================
	// BuildRoute
	//=========================================================
	bool BuildRoute(const Vector& vecGoal, int iMoveFlag, CBaseEntity* pTarget) noexcept
	{
		float	flDist{};
		Vector	vecApex{};

		RouteNew();
		m_movementGoal = RouteClassify(iMoveFlag);

		// so we don't end up with no moveflags
		m_Route[0].vecLocation = vecGoal;
		m_Route[0].iType = iMoveFlag | bits_MF_IS_GOAL;

		// check simple local move
		auto iLocalMove = CheckLocalMove(pev->origin, vecGoal, pTarget, &flDist);

		if (iLocalMove == LOCALMOVE_VALID)
		{
			// monster can walk straight there!
			return true;
		}
		// try to triangulate around any obstacles.
		else if (iLocalMove != LOCALMOVE_INVALID_DONT_TRIANGULATE && FTriangulate(pev->origin, vecGoal, flDist, pTarget, &vecApex))
		{
			// there is a slightly more complicated path that allows the monster to reach vecGoal
			m_Route[0].vecLocation = vecApex;
			m_Route[0].iType = (iMoveFlag | bits_MF_TO_DETOUR);

			m_Route[1].vecLocation = vecGoal;
			m_Route[1].iType = iMoveFlag | bits_MF_IS_GOAL;

			/*
			WRITE_BYTE(MSG_BROADCAST, SVC_TEMPENTITY);
			WRITE_BYTE(MSG_BROADCAST, TE_SHOWLINE);
			WRITE_COORD(MSG_BROADCAST, vecApex.x );
			WRITE_COORD(MSG_BROADCAST, vecApex.y );
			WRITE_COORD(MSG_BROADCAST, vecApex.z );
			WRITE_COORD(MSG_BROADCAST, vecApex.x );
			WRITE_COORD(MSG_BROADCAST, vecApex.y );
			WRITE_COORD(MSG_BROADCAST, vecApex.z + 128 );
			*/

			RouteSimplify(pTarget);
			return true;
		}

		// last ditch, try nodes
/*
		if (FGetNodeRoute(vecGoal))
		{
//			ALERT ( at_console, "Can get there on nodes\n" );

			m_vecMoveGoal = vecGoal;
			RouteSimplify(pTarget);
			return true;
		}
*/

		// b0rk
		return false;
	}


	//=========================================================
	// Route New - clears out a route to be changed, but keeps
	//				goal intact.
	//=========================================================
	constexpr void RouteNew(void) noexcept
	{
		m_Route[0].iType = 0;
		m_iRouteIndex = 0;
	}

	//=========================================================
	// FRefreshRoute - after calculating a path to the monster's
	// target, this function copies as many waypoints as possible
	// from that path to the monster's Route array
	//=========================================================
	bool FRefreshRoute(void) noexcept
	{
		RouteNew();

		switch (m_movementGoal)
		{
		case MOVEGOAL_PATHCORNER:
		{
			// monster is on a path_corner loop
			auto pPathCorner = m_pGoalEnt;
			auto i = 0;

			while (pPathCorner && i < ROUTE_SIZE)
			{
				m_Route[i].iType = bits_MF_TO_PATHCORNER;
				m_Route[i].vecLocation = pPathCorner->pev->origin;

				pPathCorner = pPathCorner->GetNextTarget();

				// Last path_corner in list?
				if (!pPathCorner)
					m_Route[i].iType |= bits_MF_IS_GOAL;

				++i;
			}

			return true;
		}

		case MOVEGOAL_ENEMY:
			return BuildRoute(m_vecEnemyLKP, bits_MF_TO_ENEMY, m_hEnemy);

		case MOVEGOAL_LOCATION:
			return BuildRoute(m_vecMoveGoal, bits_MF_TO_LOCATION, nullptr);

		case MOVEGOAL_TARGETENT:
			if (m_hTargetEnt != 0)
			{
				return BuildRoute(m_hTargetEnt->pev->origin, bits_MF_TO_TARGETENT, m_hTargetEnt);
			}
			return false;

		case MOVEGOAL_NODE:
			/*return FGetNodeRoute(m_vecMoveGoal);*/
//			if ( returnCode )
//				RouteSimplify( nullptr );

		default:
			break;
		}

		return false;
	}

	entvars_t* pev{};

	// path corners
	CBaseEntity* m_pGoalEnt{};				// path corner we are heading towards


	std::array<WayPoint_t, ROUTE_SIZE> m_Route{};	// Positions of movement
	int					m_movementGoal{};	// Goal that defines route
	int					m_iRouteIndex{};	// index into m_Route[]
	float				m_moveWaitTime{};	// How long I should wait for something to move

	Vector				m_vecEnemyLKP{};	// last known position of enemy. (enemy's origin)

	// these fields have been added in the process of reworking the state machine. (sjb)
	EHANDLE<CBaseEntity>	m_hEnemy{};		// the entity that the monster is fighting.
	EHANDLE<CBaseEntity>	m_hTargetEnt{};	// the entity that the monster is trying to reach

	Vector				m_vecMoveGoal{};	// kept around for node graph moves, so we know our ultimate goal
	Activity			m_movementActivity{};	// When moving, set this activity

	int					m_afCapability{};	// tells us what a monster can/can't do.

};

#pragma region Testing

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

Task Task_ShowMN(MonsterNav const& MN, Vector const vecSrc) noexcept
{
	for (;;)
	{
		UTIL_DrawBeamPoints(vecSrc, MN.m_Route[MN.m_iRouteIndex].vecLocation, 9, 255, 255, 255);

		co_await 0.01f;

		for (auto i = MN.m_iRouteIndex + 1; i < std::ssize(MN.m_Route); ++i)
		{
			if (MN.m_Route[i].iType == 0)
				break;

			UTIL_DrawBeamPoints(MN.m_Route[i - 1].vecLocation, MN.m_Route[i - 1].vecLocation, 9, 255, 255, 255);
			co_await 0.01f;
		}

		co_await 0.1f;
	}

	co_return;
}

export void MonsterNav_Test(CBasePlayer* pPlayer, Vector const& vecTarget) noexcept
{
	static MonsterNav MN{
		.pev = pPlayer->pev,
	};

	if (MN.BuildRoute(vecTarget, bits_MF_TO_LOCATION, nullptr))
	{
		TaskScheduler::Enroll(Task_ShowMN(MN, pPlayer->pev->origin), (1 << 0), true);
	}
	else
		g_engfuncs.pfnServerPrint("No path found!\n");
}

#pragma endregion Testing
