module;

#include <assert.h>
#include <stdio.h>

export module Nav;

import std;
import hlsdk;

import CBase;
import Query;

import UtlRandom;

export import :Const;
export import :Ladder;
export import :HidingSpot;

using std::FILE;





export enum NavErrorType
{
	NAV_OK,
	NAV_CANT_ACCESS_FILE,
	NAV_INVALID_FILE,
	NAV_BAD_FILE_VERSION,
	NAV_CORRUPT_DATA,
};












export using NavAreaList = std::vector<class CNavArea*>;	// Forward declr.

// Determine how much walkable area we can see from the spot, and how far away we can see.
void ClassifySniperSpot(HidingSpot* spot) noexcept;

inline CNavArea* markedArea = nullptr;
inline CNavArea* lastSelectedArea = nullptr;
inline bool isCreatingNavArea = false;
inline bool isPlacePainting = false;

inline float editTimestamp = 0.0f;
inline float lastDrawTimestamp = 0.0f;

void EditNavAreasReset() noexcept
{
	markedArea = nullptr;
	lastSelectedArea = nullptr;
	isCreatingNavArea = false;
	isPlacePainting = false;

	editTimestamp = 0.0f;
	lastDrawTimestamp = 0.0f;
}

// The CNavAreaGrid is used to efficiently access navigation areas by world position
// Each cell of the grid contains a list of areas that overlap it
// Given a world position, the corresponding grid cell is ( x/cellsize, y/cellsize )
export class CNavAreaGrid
{
public:
	CNavAreaGrid() noexcept
	{
		Reset();
	}
	CNavAreaGrid(CNavAreaGrid const&) noexcept = delete;
	CNavAreaGrid(CNavAreaGrid&&) noexcept = delete;
	CNavAreaGrid& operator=(CNavAreaGrid const&) noexcept = delete;
	CNavAreaGrid& operator=(CNavAreaGrid&&) noexcept = delete;
	~CNavAreaGrid() noexcept = default;

	// clear the grid to empty
	void Reset() noexcept
	{
		if (!m_grid.empty())
		{
			m_grid.clear();
			m_areaCount = 0;
		}

		m_gridSizeX = 0;
		m_gridSizeY = 0;

		// clear the hash table
		m_hashTable.fill(nullptr);

		// reset static vars
		EditNavAreasReset();
	}
	// clear and reset the grid to the given extents
	void Initialize(float minX, float maxX, float minY, float maxY) noexcept
	{
		if (!m_grid.empty())
			Reset();

		m_minX = minX;
		m_minY = minY;

		m_gridSizeX = int((maxX - minX) / m_cellSize + 1);
		m_gridSizeY = int((maxY - minY) / m_cellSize + 1);

		m_grid.resize(m_gridSizeX * m_gridSizeY);
	}
	// add an area to the grid
	void AddNavArea(CNavArea* area) noexcept;
	// remove an area from the grid
	void RemoveNavArea(CNavArea* area) noexcept;
	// return total number of nav areas
	size_t GetNavAreaCount() const noexcept { return m_areaCount; }
	// given a position, return the nav area that IsOverlapping and is *immediately* beneath it
	CNavArea* GetNavArea(const Vector& pos, float const beneathLimit = 120.0f) const noexcept;
	CNavArea* GetNavAreaByID(unsigned int id) const noexcept;
	CNavArea* GetNearestNavArea(const Vector& pos, bool anyZ = false) const noexcept;

	constexpr bool IsValid() const noexcept { return !m_grid.empty() && m_areaCount > 0; }
	// return radio chatter place for given coordinate
	Place GetPlace(const Vector& pos) const noexcept;

private:
	static inline constexpr float m_cellSize = 300.f;
	std::vector<NavAreaList> m_grid{};
	size_t m_areaCount{};	// those actually put into use.
	int m_gridSizeX{};
	int m_gridSizeY{};
	float m_minX{};
	float m_minY{};

	std::array<CNavArea*, 256> m_hashTable{};// hash table to optimize lookup by ID
	inline auto ComputeHashKey(unsigned int id) const noexcept	// returns a hash key for the given nav area ID
	{
		return id & 0xFF;
	}

	inline int WorldToGridX(float wx) const noexcept
	{
		auto x = static_cast<int>((wx - m_minX) / m_cellSize);
		if (x < 0)
			x = 0;

		else if (x >= m_gridSizeX)
			x = m_gridSizeX - 1;

		return x;
	}
	inline int WorldToGridY(float wy) const noexcept
	{
		auto y = static_cast<int>((wy - m_minY) / m_cellSize);
		if (y < 0)
			y = 0;
		else if (y >= m_gridSizeY)
			y = m_gridSizeY - 1;

		return y;
	}
};

// The singleton for accessing the grid
export extern "C++" inline CNavAreaGrid TheNavAreaGrid{};




export struct SpotOrder
{
	float t{};
	union
	{
		HidingSpot* spot;
		std::uintptr_t id{};
	};
};

using SpotOrderList = std::forward_list<SpotOrder>;

export struct SpotEncounter
{
	NavConnect from{};
	NavDirType fromDir{};
	NavConnect to{};
	NavDirType toDir{};
	Ray path{};					// the path segment
	SpotOrderList spotList{};	// list of spots to look at, in order of occurrence
};

using SpotEncounterList = std::forward_list<SpotEncounter>;



export class CNavArea final
{
public:
	CNavArea() noexcept : m_id{ m_nextID++ } {}	// LUNA: not actually protected it by private them. Now no new() involved, it's much safe.
	~CNavArea() noexcept
	{
		// if we are resetting the system, don't bother cleaning up - all areas are being destroyed
		if (m_isReset)
			return;

		// tell the other areas we are going away
		for (auto&& area : m_masterlist)
		{
			if (&area == this)
				continue;

			area.OnOtherNavAreaDestroy(this);
		}

		// unhook from ladders
		for (auto&& Ladders : m_ladder)
		{
			for (auto&& Ladder : Ladders)
				Ladder->OnNavAreaDestroy(this);
		}

		// remove the area from the grid
		TheNavAreaGrid.RemoveNavArea(this);
	}

	// connect this area to given area in given direction
	void ConnectTo(CNavArea* area, NavDirType dir) noexcept
	{
		assert(area);

		// check if already connected
		for (auto&& conn : m_connect[dir])
		{
			if (conn.area == area)
				return;
		}

		auto& conn = m_connect[dir].emplace_front();
		conn.area = area;

		//static char *dirName[] = { "NORTH", "EAST", "SOUTH", "WEST" };
		//CONSOLE_ECHO("  Connected area #%d to #%d, %s\n", m_id, area->m_id, dirName[dir]);
	}
	// disconnect this area from given area
	void Disconnect(CNavArea* area) noexcept
	{
		NavConnect const connect{ .area = area };

		for (auto&& connections : m_connect)
			connections.remove(connect);
	}

#ifdef CSBOT_ENABLE_SAVE
	void Save(FILE* fp) const noexcept
	{
		fprintf(fp, "v  %f %f %f\n", m_extent.lo.x, m_extent.lo.y, m_extent.lo.z);
		fprintf(fp, "v  %f %f %f\n", m_extent.hi.x, m_extent.lo.y, m_neZ);
		fprintf(fp, "v  %f %f %f\n", m_extent.hi.x, m_extent.hi.y, m_extent.hi.z);
		fprintf(fp, "v  %f %f %f\n", m_extent.lo.x, m_extent.hi.y, m_swZ);

		static int base = 1;
		fprintf(fp, "\n\ng %04dArea%s%s%s%s\n", m_id,
			(GetAttributes() & NAV_CROUCH) ? "CROUCH" : "", (GetAttributes() & NAV_JUMP) ? "JUMP" : "",
			(GetAttributes() & NAV_PRECISE) ? "PRECISE" : "", (GetAttributes() & NAV_NO_JUMP) ? "NO_JUMP" : "");

		fprintf(fp, "f %d %d %d %d\n\n", base, base + 1, base + 2, base + 3);
		base += 4;
	}
	void Save(FILE* fd, unsigned int version) noexcept
	{
		// save ID
		std::fwrite(&m_id, sizeof(unsigned int), 1, fd);

		// save attribute flags
		std::fwrite(&m_attributeFlags, sizeof(unsigned char), 1, fd);

		// save extent of area
		std::fwrite(&m_extent, sizeof(float), 6, fd);

		// save heights of implicit corners
		std::fwrite(&m_neZ, sizeof(float), 1, fd);
		std::fwrite(&m_swZ, sizeof(float), 1, fd);

		// save connections to adjacent areas
		// in the enum order NORTH, EAST, SOUTH, WEST
		for (int d = 0; d < NUM_DIRECTIONS; d++)
		{
			// save number of connections for this direction
			size_t count = m_connect[d].size();
			std::fwrite(&count, sizeof(size_t), 1, fd);

			for (auto& connect : m_connect[d]) {
				std::fwrite(&connect.area->m_id, sizeof(unsigned int), 1, fd);
			}
		}

		// Store hiding spots for this area
		unsigned char count;
		if (m_hidingSpotList.size() > 255)
		{
			count = 255;
			CONSOLE_ECHO("Warning: NavArea #%d: Truncated hiding spot list to 255\n", m_id);
		}
		else
		{
			count = (unsigned char)m_hidingSpotList.size();
		}

		std::fwrite(&count, sizeof(unsigned char), 1, fd);

		// store HidingSpot objects
		unsigned int saveCount = 0;
		for (auto spot : m_hidingSpotList)
		{
			spot->Save(fd, version);

			// overflow check
			if (++saveCount == count)
				break;
		}

		// Save the approach areas for this area
		// save number of approach areas
		std::fwrite(&m_approachCount, sizeof(unsigned char), 1, fd);

#ifdef CSBOT_DEBUG
		if (cv_bot_debug.value > 0.0f)
		{
			CONSOLE_ECHO("  m_approachCount = %d\n", m_approachCount);
		}
#endif

		// save approach area info
		unsigned char type;
		unsigned int zero = 0;
		for (int a = 0; a < m_approachCount; a++)
		{
			if (m_approach[a].here.area)
				std::fwrite(&m_approach[a].here.area->m_id, sizeof(unsigned int), 1, fd);
			else
				std::fwrite(&zero, sizeof(unsigned int), 1, fd);

			if (m_approach[a].prev.area)
				std::fwrite(&m_approach[a].prev.area->m_id, sizeof(unsigned int), 1, fd);
			else
				std::fwrite(&zero, sizeof(unsigned int), 1, fd);

			type = (unsigned char)m_approach[a].prevToHereHow;
			std::fwrite(&type, sizeof(unsigned char), 1, fd);

			if (m_approach[a].next.area)
				std::fwrite(&m_approach[a].next.area->m_id, sizeof(unsigned int), 1, fd);
			else
				std::fwrite(&zero, sizeof(unsigned int), 1, fd);

			type = (unsigned char)m_approach[a].hereToNextHow;
			std::fwrite(&type, sizeof(unsigned char), 1, fd);
		}

		// Save encounter spots for this area
		{
			// save number of encounter paths for this area
			unsigned int count = m_spotEncounterList.size();
			std::fwrite(&count, sizeof(unsigned int), 1, fd);

#ifdef CSBOT_DEBUG
			if (cv_bot_debug.value > 0.0f)
				CONSOLE_ECHO("  m_spotEncounterList.size() = %d\n", count);
#endif

			for (auto& spote : m_spotEncounterList)
			{
				if (spote.from.area)
					std::fwrite(&spote.from.area->m_id, sizeof(unsigned int), 1, fd);
				else
					std::fwrite(&zero, sizeof(unsigned int), 1, fd);

				unsigned char dir = spote.fromDir;
				std::fwrite(&dir, sizeof(unsigned char), 1, fd);

				if (spote.to.area)
					std::fwrite(&spote.to.area->m_id, sizeof(unsigned int), 1, fd);
				else
					std::fwrite(&zero, sizeof(unsigned int), 1, fd);

				dir = spote.toDir;
				std::fwrite(&dir, sizeof(unsigned char), 1, fd);

				// write list of spots along this path
				unsigned char spotCount;
				if (spote.spotList.size() > 255)
				{
					spotCount = 255;
					CONSOLE_ECHO("Warning: NavArea #%d: Truncated encounter spot list to 255\n", m_id);
				}
				else
				{
					spotCount = (unsigned char)spote.spotList.size();
				}
				std::fwrite(&spotCount, sizeof(unsigned char), 1, fd);

				saveCount = 0;
				for (auto& order : spote.spotList)
				{
					// order->spot may be NULL if we've loaded a nav mesh that has been edited but not re-analyzed
					unsigned int id = (order.spot) ? order.spot->GetID() : 0;
					std::fwrite(&id, sizeof(unsigned int), 1, fd);

					unsigned char t = 255 * order.t;
					std::fwrite(&t, sizeof(unsigned char), 1, fd);

					// overflow check
					if (++saveCount == spotCount)
						break;
				}
			}
		}

		// store place dictionary entry
		PlaceDirectory::EntryType entry = placeDirectory.GetEntry(GetPlace());
		std::fwrite(&entry, sizeof(entry), 1, fd);
	}
#endif

	void Load(SteamFile* file, unsigned int version) noexcept
	{
		// load ID
		file->Read(&m_id, sizeof(unsigned int));

		// update nextID to avoid collisions
		if (m_id >= m_nextID)
			m_nextID = m_id + 1;

		// load attribute flags
		file->Read(&m_attributeFlags, sizeof(unsigned char));

		// load extent of area
		file->Read(&m_extent, 6 * sizeof(float));
		m_center = (m_extent.lo + m_extent.hi) / 2.0f;

		// load heights of implicit corners
		file->Read(&m_neZ, sizeof(float));
		file->Read(&m_swZ, sizeof(float));

		// load connections (IDs) to adjacent areas
		// in the enum order NORTH, EAST, SOUTH, WEST
		for (auto&& Connections : m_connect)
		{
			// load number of connections for this direction
			unsigned int count{};
			file->Read(&count, sizeof(unsigned int));

			for (unsigned int i = 0; i < count; ++i)
			{
				auto& connect = Connections.emplace_front();
				file->Read(&connect.id, sizeof(unsigned int));
			}
		}

		// Load hiding spots
		// load number of hiding spots
		unsigned char hidingSpotCount = 0;
		file->Read(&hidingSpotCount, sizeof(unsigned char));
		m_hidingSpotList.reserve(hidingSpotCount);

		if (version == 1)
		{
			// load simple vector array
			Vector pos;
			for (int h = 0; h < hidingSpotCount; h++)
			{
				file->Read(&pos, 3 * sizeof(float));

				// create new hiding spot and put on master list
				auto const spot = HidingSpot::Create(pos, HidingSpot::IN_COVER);

				m_hidingSpotList.push_back(spot);
			}
		}
		else
		{
			// load HidingSpot objects for this area
			for (int h = 0; h < hidingSpotCount; h++)
			{
				// create new hiding spot and put on master list
				auto const spot = HidingSpot::Create();

				spot->Load(file, version);

				m_hidingSpotList.push_back(spot);
			}
		}

		// Load number of approach areas
		file->Read(&m_approachCount, sizeof(unsigned char));

		// load approach area info (IDs)
		unsigned char type = 0;
		for (int a = 0; a < m_approachCount; a++)
		{
			file->Read(&m_approach[a].here.id, sizeof(unsigned int));

			file->Read(&m_approach[a].prev.id, sizeof(unsigned int));
			file->Read(&type, sizeof(unsigned char));
			m_approach[a].prevToHereHow = (NavTraverseType)type;

			file->Read(&m_approach[a].next.id, sizeof(unsigned int));
			file->Read(&type, sizeof(unsigned char));
			m_approach[a].hereToNextHow = (NavTraverseType)type;
		}

		// Load encounter paths for this area
		unsigned int count;
		file->Read(&count, sizeof(unsigned int));

		if (version < 3)
		{
			// old data, read and discard
			for (unsigned int e = 0; e < count; e++)
			{
				SpotEncounter encounter{};

				file->Read(&encounter.from.id, sizeof(unsigned int));
				file->Read(&encounter.to.id, sizeof(unsigned int));

				file->Read(&encounter.path.from.x, 3 * sizeof(float));
				file->Read(&encounter.path.to.x, 3 * sizeof(float));

				// read list of spots along this path
				unsigned char spotCount = 0;
				file->Read(&spotCount, sizeof(unsigned char));

				for (int s = 0; s < spotCount; s++)
				{
					Vector pos{};
					file->Read(&pos, 3 * sizeof(float));
					file->Read(&pos, sizeof(float));
				}
			}

			return;
		}

		for (unsigned int e = 0; e < count; e++)
		{
			auto& encounter = m_spotEncounterList.emplace_front();

			file->Read(&encounter.from.id, sizeof(unsigned int));

			unsigned char dir = 0;
			file->Read(&dir, sizeof(unsigned char));
			encounter.fromDir = static_cast<NavDirType>(dir);

			file->Read(&encounter.to.id, sizeof(unsigned int));

			file->Read(&dir, sizeof(unsigned char));
			encounter.toDir = static_cast<NavDirType>(dir);

			// read list of spots along this path
			unsigned char spotCount = 0;
			file->Read(&spotCount, sizeof(unsigned char));

			for (int s = 0; s < spotCount; s++)
			{
				auto& order = encounter.spotList.emplace_front();

				file->Read(&order.id, sizeof(unsigned int));

				unsigned char t = 0;
				file->Read(&t, sizeof(unsigned char));

				order.t = float(t) / 255.0f;
			}
		}

		if (version >= NAV_VERSION)
		{
			// Load Place data
			PlaceDirectory::EntryType entry;
			file->Read(&entry, sizeof(entry));

			// convert entry to actual Place
			SetPlace(placeDirectory.EntryToPlace(entry));
		}
	}
	NavErrorType PostLoad() noexcept
	{
		NavErrorType error = NAV_OK;

		// connect areas together
		for (int d = 0; d < NUM_DIRECTIONS; d++)
		{
			for (auto& connect : m_connect[d])
			{
				auto id = connect.id;
				connect.area = TheNavAreaGrid.GetNavAreaByID(id);
				if (id && !connect.area)
				{
					CONSOLE_ECHO("ERROR: Corrupt navigation data. Cannot connect Navigation Areas.\n");
					error = NAV_CORRUPT_DATA;
				}
			}
		}

		// resolve approach area IDs
		for (int a = 0; a < m_approachCount; a++)
		{
			m_approach[a].here.area = TheNavAreaGrid.GetNavAreaByID(m_approach[a].here.id);
			if (m_approach[a].here.id && !m_approach[a].here.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing Approach Area (here).\n");
				error = NAV_CORRUPT_DATA;
			}

			m_approach[a].prev.area = TheNavAreaGrid.GetNavAreaByID(m_approach[a].prev.id);
			if (m_approach[a].prev.id && !m_approach[a].prev.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing Approach Area (prev).\n");
				error = NAV_CORRUPT_DATA;
			}

			m_approach[a].next.area = TheNavAreaGrid.GetNavAreaByID(m_approach[a].next.id);
			if (m_approach[a].next.id && !m_approach[a].next.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing Approach Area (next).\n");
				error = NAV_CORRUPT_DATA;
			}
		}

		// resolve spot encounter IDs
		for (auto& spote : m_spotEncounterList)
		{
			spote.from.area = TheNavAreaGrid.GetNavAreaByID(spote.from.id);
			if (!spote.from.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing \"from\" Navigation Area for Encounter Spot.\n");
				error = NAV_CORRUPT_DATA;
			}

			spote.to.area = TheNavAreaGrid.GetNavAreaByID(spote.to.id);
			if (!spote.to.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing \"to\" Navigation Area for Encounter Spot.\n");
				error = NAV_CORRUPT_DATA;
			}

			if (spote.from.area && spote.to.area)
			{
				// compute path
				float halfWidth{};
				ComputePortal(spote.to.area, spote.toDir, &spote.path.to, &halfWidth);
				ComputePortal(spote.from.area, spote.fromDir, &spote.path.from, &halfWidth);

				const float eyeHeight = HalfHumanHeight;
				spote.path.from.z = spote.from.area->GetZ(spote.path.from) + eyeHeight;
				spote.path.to.z = spote.to.area->GetZ(spote.path.to) + eyeHeight;
			}

			// resolve HidingSpot IDs
			for (auto& order : spote.spotList)
			{
				order.spot = GetHidingSpotByID(order.id);
				if (!order.spot)
				{
					CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing Hiding Spot\n");
					error = NAV_CORRUPT_DATA;
				}
			}
		}

		// build overlap list
		// #PF_TODO: Optimize this
		for (auto&& area : m_masterlist)
		{
			if (&area == this)
				continue;

			if (IsOverlapping(&area))
				m_overlapList.push_back(&area);
		}

		return error;
	}

	unsigned int GetID() const noexcept { return m_id; }
	void SetAttributes(unsigned char bits) noexcept { m_attributeFlags = bits; }
	unsigned char GetAttributes() const noexcept { return m_attributeFlags; }
	void SetPlace(Place place) noexcept { m_place = place; }			// set place descriptor
	Place GetPlace() const noexcept { return m_place; }					// get place descriptor

	// return true if 'pos' is within 2D extents of area
	bool IsOverlapping(const Vector& pos) const noexcept
	{
		if (pos.x >= m_extent.lo.x && pos.x <= m_extent.hi.x &&
			pos.y >= m_extent.lo.y && pos.y <= m_extent.hi.y)
			return true;

		return false;
	}
	// return true if 'area' overlaps our 2D extents
	bool IsOverlapping(const CNavArea* area) const noexcept
	{
		if (area->m_extent.lo.x < m_extent.hi.x && area->m_extent.hi.x > m_extent.lo.x &&
			area->m_extent.lo.y < m_extent.hi.y && area->m_extent.hi.y > m_extent.lo.y)
			return true;

		return false;
	}
	// return true if 'area' overlaps our X extent
	bool IsOverlappingX(const CNavArea* area) const noexcept
	{
		if (area->m_extent.lo.x < m_extent.hi.x && area->m_extent.hi.x > m_extent.lo.x)
			return true;

		return false;
	}
	// return true if 'area' overlaps our Y extent
	bool IsOverlappingY(const CNavArea* area) const noexcept
	{
		if (area->m_extent.lo.y < m_extent.hi.y && area->m_extent.hi.y > m_extent.lo.y)
			return true;

		return false;
	}
	// return number of players with given teamID in this area (teamID == 0 means any/all)
	int GetPlayerCount(int teamID = 0, CBasePlayer* pEntIgnore = nullptr) const noexcept
	{
		int nCount = 0;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer* pPlayer = ent_cast<CBasePlayer*>(i);

			if (pPlayer == pEntIgnore || !pPlayer)
				continue;

			if (pev_valid(pPlayer->pev) != EValidity::Full || pPlayer->IsDormant())
				continue;

			if (!pPlayer->IsPlayer())
				continue;

			if (!pPlayer->IsAlive())
				continue;

			if (teamID == 0/*unassigned*/ || pPlayer->m_iTeam == teamID)
			{
				if (Contains(pPlayer->pev->origin))
					++nCount;
			}
		}

		return nCount;
	}

	// return Z of area at (x,y) of 'pos'
	float GetZ(const Vector& pos) const noexcept
	{
		float const dx = m_extent.hi.x - m_extent.lo.x;
		float const dy = m_extent.hi.y - m_extent.lo.y;

		// guard against division by zero due to degenerate areas
		if (dx == 0.0f || dy == 0.0f)
			return m_neZ;

		float u = (pos.x - m_extent.lo.x) / dx;
		float v = (pos.y - m_extent.lo.y) / dy;

		// clamp Z values to (x,y) volume
		if (u < 0.0f)
			u = 0.0f;
		else if (u > 1.0f)
			u = 1.0f;

		if (v < 0.0f)
			v = 0.0f;
		else if (v > 1.0f)
			v = 1.0f;

		float const northZ = m_extent.lo.z + u * (m_neZ - m_extent.lo.z);
		float const southZ = m_swZ + u * (m_extent.hi.z - m_swZ);

		return northZ + v * (southZ - northZ);
	}
	// return Z of area at (x,y) of 'pos'
	float GetZ(float x, float y) const noexcept { Vector pos(x, y, 0.0f); return GetZ(pos); }
	// return true if given point is on or above this area, but no others
	bool Contains(const Vector& pos) const noexcept
	{
		// check 2D overlap
		if (!IsOverlapping(pos))
			return false;

		// the point overlaps us, check that it is above us, but not above any areas that overlap us
		float const ourZ = GetZ(pos);

		// if we are above this point, fail
		if (ourZ > pos.z)
			return false;

		for (auto&& area : m_overlapList)
		{
			// skip self
			if (area == this)
				continue;

			// check 2D overlap
			if (!area->IsOverlapping(pos))
				continue;

			float const theirZ = area->GetZ(pos);
			if (theirZ > pos.z)
			{
				// they are above the point
				continue;
			}

			if (theirZ > ourZ)
			{
				// we are below an area that is closer underneath the point
				return false;
			}
		}

		return true;
	}

	// return true if this area and given area are approximately co-planar
	bool IsCoplanar(const CNavArea* area) const noexcept
	{
		Vector u, v;

		// compute our unit surface normal
		u.x = m_extent.hi.x - m_extent.lo.x;
		u.y = 0.0f;
		u.z = m_neZ - m_extent.lo.z;

		v.x = 0.0f;
		v.y = m_extent.hi.y - m_extent.lo.y;
		v.z = m_swZ - m_extent.lo.z;

		Vector const normal = CrossProduct(u, v).Normalize();

		// compute their unit surface normal
		u.x = area->m_extent.hi.x - area->m_extent.lo.x;
		u.y = 0.0f;
		u.z = area->m_neZ - area->m_extent.lo.z;

		v.x = 0.0f;
		v.y = area->m_extent.hi.y - area->m_extent.lo.y;
		v.z = area->m_swZ - area->m_extent.lo.z;

		Vector const otherNormal = CrossProduct(u, v).Normalize();

		// can only merge areas that are nearly planar, to ensure areas do not differ from underlying geometry much
		static constexpr float tolerance = 0.99f; // 0.7071f;		// 0.9
		if (DotProduct(normal, otherNormal) > tolerance)
			return true;

		return false;
	}
	// return closest point to 'pos' on this area - returned point in 'close'
	void GetClosestPointOnArea(const Vector& pos, Vector* close) const noexcept
	{
		const Extent* extent = GetExtent();
		if (pos.x < extent->lo.x)
		{
			if (pos.y < extent->lo.y)
			{
				// position is north-west of area
				*close = extent->lo;
			}
			else if (pos.y > extent->hi.y)
			{
				// position is south-west of area
				close->x = extent->lo.x;
				close->y = extent->hi.y;
			}
			else
			{
				// position is west of area
				close->x = extent->lo.x;
				close->y = pos.y;
			}
		}
		else if (pos.x > extent->hi.x)
		{
			if (pos.y < extent->lo.y)
			{
				// position is north-east of area
				close->x = extent->hi.x;
				close->y = extent->lo.y;
			}
			else if (pos.y > extent->hi.y)
			{
				// position is south-east of area
				*close = extent->hi;
			}
			else
			{
				// position is east of area
				close->x = extent->hi.x;
				close->y = pos.y;
			}
		}
		else if (pos.y < extent->lo.y)
		{
			// position is north of area
			close->x = pos.x;
			close->y = extent->lo.y;
		}
		else if (pos.y > extent->hi.y)
		{
			// position is south of area
			close->x = pos.x;
			close->y = extent->hi.y;
		}
		else
		{
			// position is inside of area - it is the 'closest point' to itself
			*close = pos;
		}

		close->z = GetZ(*close);
	}
	// return shortest distance between point and this area
	float GetDistanceSquaredToPoint(const Vector& pos) const noexcept
	{
		const Extent* extent = GetExtent();

		if (pos.x < extent->lo.x)
		{
			if (pos.y < extent->lo.y)
			{
				// position is north-west of area
				return (float)(extent->lo - pos).LengthSquared();
			}
			else if (pos.y > extent->hi.y)
			{
				// position is south-west of area
				Vector d;
				d.x = extent->lo.x - pos.x;
				d.y = extent->hi.y - pos.y;
				d.z = m_swZ - pos.z;
				return (float)d.LengthSquared();
			}
			else
			{
				// position is west of area
				auto const d = extent->lo.x - pos.x;
				return d * d;
			}
		}
		else if (pos.x > extent->hi.x)
		{
			if (pos.y < extent->lo.y)
			{
				// position is north-east of area
				Vector d;
				d.x = extent->hi.x - pos.x;
				d.y = extent->lo.y - pos.y;
				d.z = m_neZ - pos.z;
				return (float)d.LengthSquared();
			}
			else if (pos.y > extent->hi.y)
			{
				// position is south-east of area
				return (float)(extent->hi - pos).LengthSquared();
			}
			else
			{
				// position is east of area
				auto const d = pos.z - extent->hi.x;
				return d * d;
			}
		}
		else if (pos.y < extent->lo.y)
		{
			// position is north of area
			auto const d = extent->lo.y - pos.y;
			return d * d;
		}
		else if (pos.y > extent->hi.y)
		{
			// position is south of area
			auto const d = pos.y - extent->hi.y;
			return d * d;
		}
		else
		{
			// position is inside of 2D extent of area - find delta Z
			auto const z = GetZ(pos);
			auto const d = z - pos.z;
			return d * d;
		}
	}
	// return true if this area is badly formed
	bool IsDegenerate() const noexcept
	{
		return (m_extent.lo.x >= m_extent.hi.x || m_extent.lo.y >= m_extent.hi.y);
	}
	// return true if there are no bi-directional links on the given side
	bool IsEdge(NavDirType dir) const noexcept
	{
		for (auto& connect : m_connect[dir])
		{
			if (connect.area->IsConnected(this, Opposite[dir]))
				return false;
		}

		return true;
	}
	// return number of connected areas in given direction
	int GetAdjacentCount(NavDirType dir) const noexcept { return std::ssize(m_connect[dir]); }
	// return the i'th adjacent area in the given direction
	CNavArea* GetAdjacentArea(NavDirType dir, int i) const noexcept
	{
		for (auto& con : m_connect[dir])
		{
			if (i == 0)
				return con.area;
			i--;
		}

		return nullptr;
	}
	CNavArea* GetRandomAdjacentArea(NavDirType dir) const noexcept
	{
		auto const count = m_connect[dir].size();
		auto const which = UTIL_Random(0u, count - 1);

		auto it = m_connect[dir].begin();
		std::advance(it, which);
		return it->area;
	}
	const NavConnectList* GetAdjacentList(NavDirType dir) const noexcept { return &m_connect[dir]; }
	// Return true if given area is connected in given direction
	// if dir == NUM_DIRECTIONS, check all directions (direction is unknown)
	// #PF_TODO: Formalize "asymmetric" flag on connections
	bool IsConnected(const CNavArea* area, NavDirType dir) const noexcept
	{
		// we are connected to ourself
		if (area == this)
			return true;

		NavConnectList::const_iterator iter;

		if (dir == NUM_DIRECTIONS)
		{
			// search all directions
			for (auto&& Connections : m_connect)
			{
				for (auto&& conn : Connections)
				{
					if (area == conn.area)
						return true;
				}
			}

			// check ladder connections
			for (auto&& ladder : m_ladder[LADDER_UP])
			{
				if (ladder->m_topBehindArea == area || ladder->m_topForwardArea == area || ladder->m_topLeftArea == area || ladder->m_topRightArea == area)
					return true;
			}

			for (auto&& ladder : m_ladder[LADDER_DOWN])
			{
				if (ladder->m_bottomArea == area)
					return true;
			}
		}
		else
		{
			// check specific direction
			for (auto&& conn : m_connect[dir])
			{
				if (area == conn.area)
					return true;
			}
		}

		return false;
	}
	// compute change in height from this area to given area
	float ComputeHeightChange(const CNavArea* area) const noexcept
	{
		auto const ourZ = GetZ(GetCenter());
		auto const areaZ = area->GetZ(area->GetCenter());

		return areaZ - ourZ;
	}

	const NavLadderList* GetLadderList(LadderDirectionType dir) const noexcept { return &m_ladder[dir]; }

	// Compute "portal" between to adjacent areas.
	// Return center of portal opening, and half-width defining sides of portal from center.
	// NOTE: center->z is unset.
	void ComputePortal(const CNavArea* to, NavDirType dir, Vector* center, float* halfWidth) const noexcept
	{
		if (dir == NORTH || dir == SOUTH)
		{
			if (dir == NORTH)
				center->y = m_extent.lo.y;
			else
				center->y = m_extent.hi.y;

			float left = std::max(m_extent.lo.x, to->m_extent.lo.x);
			float right = std::min(m_extent.hi.x, to->m_extent.hi.x);

			// clamp to our extent in case areas are disjoint
			if (left < m_extent.lo.x)
				left = m_extent.lo.x;
			else if (left > m_extent.hi.x)
				left = m_extent.hi.x;

			if (right < m_extent.lo.x)
				right = m_extent.lo.x;
			else if (right > m_extent.hi.x)
				right = m_extent.hi.x;

			center->x = (left + right) / 2.0f;
			*halfWidth = (right - left) / 2.0f;
		}
		else	// EAST or WEST
		{
			if (dir == WEST)
				center->x = m_extent.lo.x;
			else
				center->x = m_extent.hi.x;

			float top = std::max(m_extent.lo.y, to->m_extent.lo.y);
			float bottom = std::min(m_extent.hi.y, to->m_extent.hi.y);

			// clamp to our extent in case areas are disjoint
			if (top < m_extent.lo.y)
				top = m_extent.lo.y;
			else if (top > m_extent.hi.y)
				top = m_extent.hi.y;

			if (bottom < m_extent.lo.y)
				bottom = m_extent.lo.y;
			else if (bottom > m_extent.hi.y)
				bottom = m_extent.hi.y;

			center->y = (top + bottom) / 2.0f;
			*halfWidth = (bottom - top) / 2.0f;
		}
	}
	// Compute closest point within the "portal" between to adjacent areas.
	void ComputeClosestPointInPortal(const CNavArea* to, NavDirType dir, const Vector* fromPos, Vector* closePos) const noexcept
	{
		static constexpr float margin = GenerationStepSize / 2.0f;

		if (dir == NORTH || dir == SOUTH)
		{
			if (dir == NORTH)
				closePos->y = m_extent.lo.y;
			else
				closePos->y = m_extent.hi.y;

			float left = std::max(m_extent.lo.x, to->m_extent.lo.x);
			float right = std::min(m_extent.hi.x, to->m_extent.hi.x);

			// clamp to our extent in case areas are disjoint
			if (left < m_extent.lo.x)
				left = m_extent.lo.x;
			else if (left > m_extent.hi.x)
				left = m_extent.hi.x;

			if (right < m_extent.lo.x)
				right = m_extent.lo.x;
			else if (right > m_extent.hi.x)
				right = m_extent.hi.x;

			// keep margin if against edge
			const float leftMargin = (to->IsEdge(WEST)) ? (left + margin) : left;
			const float rightMargin = (to->IsEdge(EAST)) ? (right - margin) : right;

			// limit x to within portal
			if (fromPos->x < leftMargin)
				closePos->x = leftMargin;
			else if (fromPos->x > rightMargin)
				closePos->x = rightMargin;
			else
				closePos->x = fromPos->x;

		}
		else	// EAST or WEST
		{
			if (dir == WEST)
				closePos->x = m_extent.lo.x;
			else
				closePos->x = m_extent.hi.x;

			float top = std::max(m_extent.lo.y, to->m_extent.lo.y);
			float bottom = std::min(m_extent.hi.y, to->m_extent.hi.y);

			// clamp to our extent in case areas are disjoint
			if (top < m_extent.lo.y)
				top = m_extent.lo.y;
			else if (top > m_extent.hi.y)
				top = m_extent.hi.y;

			if (bottom < m_extent.lo.y)
				bottom = m_extent.lo.y;
			else if (bottom > m_extent.hi.y)
				bottom = m_extent.hi.y;

			// keep margin if against edge
			const float topMargin = (to->IsEdge(NORTH)) ? (top + margin) : top;
			const float bottomMargin = (to->IsEdge(SOUTH)) ? (bottom - margin) : bottom;

			// limit y to within portal
			if (fromPos->y < topMargin)
				closePos->y = topMargin;
			else if (fromPos->y > bottomMargin)
				closePos->y = bottomMargin;
			else
				closePos->y = fromPos->y;
		}
	}
	// Return direction from this area to the given point
	NavDirType ComputeDirection(Vector* point) const noexcept
	{
		if (point->x >= m_extent.lo.x && point->x <= m_extent.hi.x)
		{
			if (point->y < m_extent.lo.y)
				return NORTH;
			else if (point->y > m_extent.hi.y)
				return SOUTH;
		}
		else if (point->y >= m_extent.lo.y && point->y <= m_extent.hi.y)
		{
			if (point->x < m_extent.lo.x)
				return WEST;
			else if (point->x > m_extent.hi.x)
				return EAST;
		}

		// find closest direction
		Vector const to = *point - m_center;

		if (std::fabs(to.x) > std::fabs(to.y))
		{
			if (to.x > 0.0f)
				return EAST;
			return WEST;
		}
		else
		{
			if (to.y > 0.0f)
				return SOUTH;
			return NORTH;
		}

		return NUM_DIRECTIONS;
	}
	// for hunting algorithm
	void SetClearedTimestamp(int teamID) noexcept { m_clearedTimestamp[teamID] = gpGlobals->time; }	// set this area's "clear" timestamp to now
	float GetClearedTimestamp(int teamID) const noexcept { return m_clearedTimestamp[teamID]; }			// get time this area was marked "clear"

	// hiding spots
	const HidingSpotList* GetHidingSpotList() const noexcept { return &m_hidingSpotList; }

	// analyze local area neighborhood to find "hiding spots" in this area - for map learning
	void ComputeHidingSpots() = delete;
	// Analyze local area neighborhood to find "sniper spots" for this area
	void ComputeSniperSpots() = delete;

	// Given the areas we are moving between, return the spots we will encounter
	auto GetSpotEncounter(const CNavArea* from, const CNavArea* to) const noexcept -> SpotEncounter const* = delete;
	// Compute "spot encounter" data. This is an ordered list of spots to look at
	// for each possible path thru a nav area.
	void ComputeSpotEncounters() = delete;

	// danger

	// Increase the danger of this area for the given team
	void IncreaseDanger(ECsTeams teamID, float amount) noexcept
	{
		// before we add the new value, decay what's there
		DecayDanger();

		m_danger[teamID] += amount;
		m_dangerTimestamp[teamID] = gpGlobals->time;
	}
	// return the danger of this area (decays over time)
	float GetDanger(ECsTeams teamID) noexcept
	{
		DecayDanger();
		return m_danger[teamID];
	}

	float GetSizeX() const noexcept { return m_extent.hi.x - m_extent.lo.x; }
	float GetSizeY() const noexcept { return m_extent.hi.y - m_extent.lo.y; }

	const Extent* GetExtent() const noexcept { return &m_extent; }
	Vector const& GetCenter() const noexcept { return m_center; }
	std::optional<Vector> GetCorner(NavCornerType corner) const noexcept
	{
		switch (corner)
		{
		case NORTH_WEST:
			return m_extent.lo;

		case NORTH_EAST:
			return std::optional<Vector>{
				std::in_place,
				m_extent.hi.x,
				m_extent.lo.y,
				m_neZ,
			};

		case SOUTH_WEST:
			return std::optional<Vector>{
				std::in_place,
				m_extent.lo.x,
				m_extent.hi.y,
				m_swZ,
			};

		case SOUTH_EAST:
			return m_extent.hi;
		}

		return std::nullopt;
	}

	// approach areas
	struct ApproachInfo
	{
		NavConnect here{};			// the approach area
		NavConnect prev{};			// the area just before the approach area on the path
		NavTraverseType prevToHereHow{};
		NavConnect next{};			// the area just after the approach area on the path
		NavTraverseType hereToNextHow{};
	};

	const ApproachInfo* GetApproachInfo(int i) const noexcept { return &m_approach[i]; }
	int GetApproachInfoCount() const noexcept { return m_approachCount; }
	void ComputeApproachAreas() = delete;	// determine the set of "approach areas" - for map learning

	// A* pathfinding algorithm
	static void MakeNewMarker() noexcept
	{
		if (++m_masterMarker == 0)
			m_masterMarker = 1;
	}
	void Mark() noexcept { m_marker = m_masterMarker; }
	qboolean IsMarked() const noexcept { return (m_marker == m_masterMarker) ? true : false; }
	void SetParent(CNavArea* parent, NavTraverseType how = NUM_TRAVERSE_TYPES) noexcept { m_parent = parent; m_parentHow = how; }
	CNavArea* GetParent() const noexcept { return m_parent; }
	NavTraverseType GetParentHow() const noexcept { return m_parentHow; }

	// true if on "open list"
	bool IsOpen() const noexcept
	{
		return (m_openMarker == m_masterMarker) ? true : false;
	}
	// add to open list in decreasing value order
	void AddToOpenList() noexcept
	{
		// mark as being on open list for quick check
		m_openMarker = m_masterMarker;

		// if list is empty, add and return
		if (!m_openList)
		{
			m_openList = this;
			this->m_prevOpen = nullptr;
			this->m_nextOpen = nullptr;
			return;
		}

		// insert self in ascending cost order
		CNavArea* area, * last = nullptr;
		for (area = m_openList; area; area = area->m_nextOpen)
		{
			if (this->GetTotalCost() < area->GetTotalCost())
				break;

			last = area;
		}

		if (area)
		{
			// insert before this area
			this->m_prevOpen = area->m_prevOpen;
			if (this->m_prevOpen)
				this->m_prevOpen->m_nextOpen = this;
			else
				m_openList = this;

			this->m_nextOpen = area;
			area->m_prevOpen = this;
		}
		else
		{
			// append to end of list
			last->m_nextOpen = this;

			this->m_prevOpen = last;
			this->m_nextOpen = nullptr;
		}
	}
	// A smaller value has been found, update this area on the open list
	// #PF_TODO: "bubbling" does unnecessary work, since the order of all other nodes will be unchanged - only this node is altered
	void UpdateOnOpenList() noexcept
	{
		// since value can only decrease, bubble this area up from current spot
		while (m_prevOpen && this->GetTotalCost() < m_prevOpen->GetTotalCost())
		{
			// swap position with predecessor
			CNavArea* other = m_prevOpen;
			CNavArea* before = other->m_prevOpen;
			CNavArea* after = this->m_nextOpen;

			this->m_nextOpen = other;
			this->m_prevOpen = before;

			other->m_prevOpen = this;
			other->m_nextOpen = after;

			if (before)
				before->m_nextOpen = this;
			else
				m_openList = this;

			if (after)
				after->m_prevOpen = other;
		}
	}
	void RemoveFromOpenList() noexcept
	{
		if (m_prevOpen)
			m_prevOpen->m_nextOpen = m_nextOpen;
		else
			m_openList = m_nextOpen;

		if (m_nextOpen)
			m_nextOpen->m_prevOpen = m_prevOpen;

		// zero is an invalid marker
		m_openMarker = 0;
	}
	static bool IsOpenListEmpty() noexcept
	{
		return m_openList ? false : true;
	}
	static CNavArea* PopOpenList() noexcept	// remove and return the first element of the open list
	{
		if (m_openList)
		{
			CNavArea* area = m_openList;

			// disconnect from list
			area->RemoveFromOpenList();
			return area;
		}

		return nullptr;
	}

	bool IsClosed() const noexcept	// true if on "closed list"
	{
		if (IsMarked() && !IsOpen())
			return true;

		return false;
	}
	void AddToClosedList() noexcept	// add to the closed list
	{
		Mark();
	}
	static constexpr void RemoveFromClosedList() noexcept
	{
		// since "closed" is defined as visited (marked) and not on open list, do nothing
	}

	// Clears the open and closed lists for a new search
	static void ClearSearchLists() noexcept
	{
		CNavArea::MakeNewMarker();
		m_openList = nullptr;
	}

	void SetTotalCost(float value) noexcept { m_totalCost = value; }
	float GetTotalCost() const noexcept { return m_totalCost; }

	void SetCostSoFar(float value) noexcept { m_costSoFar = value; }
	float GetCostSoFar() const noexcept { return m_costSoFar; }

	// editing

	// draw area for debugging & editing
	void Draw(std::uint8_t red, std::uint8_t green, std::uint8_t blue, int duration = 50) const noexcept
	{
		Vector nw, ne, sw, se;

		nw = m_extent.lo;
		se = m_extent.hi;
		ne.x = se.x;
		ne.y = nw.y;
		ne.z = m_neZ;
		sw.x = nw.x;
		sw.y = se.y;
		sw.z = m_swZ;

		static constexpr float Z_OFS = 18.f;
		nw.z += Z_OFS;
		ne.z += Z_OFS;
		sw.z += Z_OFS;
		se.z += Z_OFS;

		static constexpr float border = 2.0f;
		nw.x += border;
		nw.y += border;
		ne.x -= border;
		ne.y += border;
		sw.x += border;
		sw.y -= border;
		se.x -= border;
		se.y -= border;

		UTIL_DrawBeamPoints(nw, ne, duration, red, green, blue);
		UTIL_DrawBeamPoints(ne, se, duration, red, green, blue);
		UTIL_DrawBeamPoints(se, sw, duration, red, green, blue);
		UTIL_DrawBeamPoints(sw, nw, duration, red, green, blue);

		if (GetAttributes() & NAV_CROUCH)
			UTIL_DrawBeamPoints(nw, se, duration, red, green, blue);

		if (GetAttributes() & NAV_JUMP)
		{
			UTIL_DrawBeamPoints(nw, se, duration, red, green, blue);
			UTIL_DrawBeamPoints(ne, sw, duration, red, green, blue);
		}

		if (GetAttributes() & NAV_PRECISE)
		{
			static constexpr float size = 8.0f;
			Vector const up(m_center.x, m_center.y - size, m_center.z + Z_OFS);
			Vector const down(m_center.x, m_center.y + size, m_center.z + Z_OFS);
			UTIL_DrawBeamPoints(up, down, duration, red, green, blue);

			Vector const left(m_center.x - size, m_center.y, m_center.z + Z_OFS);
			Vector const right(m_center.x + size, m_center.y, m_center.z + Z_OFS);
			UTIL_DrawBeamPoints(left, right, duration, red, green, blue);
		}

		if (GetAttributes() & NAV_NO_JUMP)
		{
			static constexpr float size = 8.0f;
			Vector const up(m_center.x, m_center.y - size, m_center.z + Z_OFS);
			Vector const down(m_center.x, m_center.y + size, m_center.z + Z_OFS);
			Vector const left(m_center.x - size, m_center.y, m_center.z + Z_OFS);
			Vector const right(m_center.x + size, m_center.y, m_center.z + Z_OFS);
			UTIL_DrawBeamPoints(up, right, duration, red, green, blue);
			UTIL_DrawBeamPoints(right, down, duration, red, green, blue);
			UTIL_DrawBeamPoints(down, left, duration, red, green, blue);
			UTIL_DrawBeamPoints(left, up, duration, red, green, blue);
		}
	}
	// Draw ourselves and adjacent areas
	void DrawConnectedAreas() const noexcept
	{
		static constexpr float Z_OFS = 18.f;

		CBasePlayer* pLocalPlayer = UTIL_GetLocalPlayer();
		if (!pLocalPlayer)
			return;

		static constexpr float maxRange = 500.0f;

		// draw self
		Draw(255, 255, 0, 3);
		DrawHidingSpots();

		// randomize order of directions to make sure all connected areas are
		// drawn, since we may have too many to render all at once
		std::array<int, NUM_DIRECTIONS> dirSet{};
		for (auto i = 0; i < NUM_DIRECTIONS; ++i)
			dirSet[i] = i;

		// shuffle dirSet[]
		UTIL_Shuffle(dirSet);

		// draw connected areas
		for (auto i = 0; i < NUM_DIRECTIONS; ++i)
		{
			NavDirType dir = (NavDirType)dirSet[i];

			auto const count = GetAdjacentCount(dir);
			for (int a = 0; a < count; a++)
			{
				CNavArea* adj = GetAdjacentArea(dir, a);

				if (adj->IsDegenerate())
				{
					static IntervalTimer blink;
					static bool blinkOn = false;

					if (blink.GetElapsedTime() > 1.0f)
					{
						blink.Reset();
						blinkOn = !blinkOn;
					}

					if (blinkOn)
						adj->Draw(255, 255, 255, 3);
					else
						adj->Draw(255, 0, 255, 3);
				}
				else
				{
					adj->Draw(255, 0, 0, 3);
				}

				adj->DrawHidingSpots();

				Vector from{}, to{};
				Vector hookPos{};
				float halfWidth{};
				ComputePortal(adj, dir, &hookPos, &halfWidth);

				static constexpr float size = 5.0f;
				switch (dir)
				{
				case NORTH:
					from = hookPos + Vector(0.0f, size, 0.0f);
					to = hookPos + Vector(0.0f, -size, 0.0f);
					break;
				case SOUTH:
					from = hookPos + Vector(0.0f, -size, 0.0f);
					to = hookPos + Vector(0.0f, size, 0.0f);
					break;
				case EAST:
					from = hookPos + Vector(-size, 0.0f, 0.0f);
					to = hookPos + Vector(+size, 0.0f, 0.0f);
					break;
				case WEST:
					from = hookPos + Vector(size, 0.0f, 0.0f);
					to = hookPos + Vector(-size, 0.0f, 0.0f);
					break;
				}

				from.z = GetZ(from) + Z_OFS;
				to.z = adj->GetZ(to) + Z_OFS;

				Vector drawTo{};
				adj->GetClosestPointOnArea(to, &drawTo);

				if (adj->IsConnected(this, Opposite[dir]))
					UTIL_DrawBeamPoints(from, drawTo, 3, 0, 255, 255);
				else
					UTIL_DrawBeamPoints(from, drawTo, 3, 0, 0, 255);
			}
		}
	}
	// Draw selected corner for debugging
	void DrawMarkedCorner(NavCornerType corner, std::uint8_t red, std::uint8_t green, std::uint8_t blue, int duration = 50) const noexcept
	{
		static constexpr float Z_OFS = 18.f;

		Vector nw, ne, sw, se;

		nw = m_extent.lo;
		se = m_extent.hi;
		ne.x = se.x;
		ne.y = nw.y;
		ne.z = m_neZ;
		sw.x = nw.x;
		sw.y = se.y;
		sw.z = m_swZ;

		nw.z += Z_OFS;
		ne.z += Z_OFS;
		sw.z += Z_OFS;
		se.z += Z_OFS;

		static constexpr float border = 2.0f;
		nw.x += border;
		nw.y += border;
		ne.x -= border;
		ne.y += border;
		sw.x += border;
		sw.y -= border;
		se.x -= border;
		se.y -= border;

		switch (corner)
		{
		case NORTH_WEST:
			UTIL_DrawBeamPoints(nw + Vector(0, 0, 10), nw, duration, red, green, blue);
			break;
		case NORTH_EAST:
			UTIL_DrawBeamPoints(ne + Vector(0, 0, 10), ne, duration, red, green, blue);
			break;
		case SOUTH_EAST:
			UTIL_DrawBeamPoints(se + Vector(0, 0, 10), se, duration, red, green, blue);
			break;
		case SOUTH_WEST:
			UTIL_DrawBeamPoints(sw + Vector(0, 0, 10), sw, duration, red, green, blue);
			break;
		}
	}

	void DrawHidingSpots() const noexcept
	{
		for (auto&& spot : m_hidingSpotList)
		{
			int r{}, g{}, b{};

			if (spot->IsIdealSniperSpot())
			{
				r = 255; g = 0; b = 0;
			}
			else if (spot->IsGoodSniperSpot())
			{
				r = 255; g = 0; b = 255;
			}
			else if (spot->HasGoodCover())
			{
				r = 0; g = 255; b = 0;
			}
			else
			{
				r = 0; g = 0; b = 1;
			}

			UTIL_DrawBeamPoints(spot->GetPosition(), spot->GetPosition() + Vector(0, 0, 50), 3, r, g, b);
		}
	}

	bool SplitEdit(bool splitAlongX, float splitEdge, CNavArea** outAlpha = nullptr, CNavArea** outBeta = nullptr) = delete;	// split this area into two areas at the given edge
	bool MergeEdit(CNavArea* adj) = delete;											// merge this area and given adjacent area
	bool SpliceEdit(CNavArea* other) = delete;										// create a new area between this area and given area
	void RaiseCorner(NavCornerType corner, int amount) = delete;						// raise/lower a corner (or all corners if corner == NUM_CORNERS)

	// ladders
	void AddLadderUp(CNavLadder* ladder) noexcept { m_ladder[LADDER_UP].push_back(ladder); }
	void AddLadderDown(CNavLadder* ladder) noexcept { m_ladder[LADDER_DOWN].push_back(ladder); }

	// Master list
	static inline std::forward_list<CNavArea> m_masterlist{};

private:
//	friend void ConnectGeneratedAreas();
//	friend void MergeGeneratedAreas();
//	friend void MarkJumpAreas();
//	friend bool SaveNavigationMap(const char* filename);
	friend NavErrorType LoadNavigationMap() noexcept;
	friend void DestroyNavigationMap() noexcept;
	friend void DestroyHidingSpots() noexcept;
//	friend void StripNavigationAreas();
	friend class CNavAreaGrid;
//	friend class CCSBotManager;

	static inline bool m_isReset{ false };		// if true, don't bother cleaning up in destructor since everything is going away
	static inline unsigned int m_nextID{ 1 };	// used to allocate unique IDs
	unsigned int m_id{};						// unique area ID
	Extent m_extent{};							// extents of area in world coords (NOTE: lo.z is not necessarily the minimum Z, but corresponds to Z at point (lo.x, lo.y), etc
	Vector m_center{};							// centroid of area
	unsigned char m_attributeFlags{};			// set of attribute bit flags (see NavAttributeType)
	Place m_place{};							// place descriptor

	// height of the implicit corners
	float m_neZ{};
	float m_swZ{};

	// for hunting
	std::array<float, 5> m_clearedTimestamp{};	// time this area was last "cleared" of enemies

	// danger
	std::array<float, 5> m_danger{};			// danger of this area, allowing bots to avoid areas where they died in the past - zero is no danger
	std::array<float, 5> m_dangerTimestamp;		// time when danger value was set - used for decaying
	void DecayDanger() noexcept
	{
		// one kill == 1.0, which we will forget about in two minutes
		static constexpr float decayRate = 1.0f / 120.0f;

		for (auto&& [danger, dangerTimestamp] : std::views::zip(m_danger, m_dangerTimestamp))
		{
			float const deltaT = gpGlobals->time - dangerTimestamp;
			float const decayAmount = decayRate * deltaT;

			danger -= decayAmount;
			if (danger < 0.0f)
				danger = 0.0f;

			// update timestamp
			dangerTimestamp = gpGlobals->time;
		}
	}

	// hiding spots
	HidingSpotList m_hidingSpotList{};
	bool IsHidingSpotCollision(const Vector* pos) const noexcept = delete;	// returns true if an existing hiding spot is too close to given position

	// encounter spots
	SpotEncounterList m_spotEncounterList{};	// list of possible ways to move thru this area, and the spots to look at as we do
	void AddSpotEncounters(const CNavArea* from, NavDirType fromDir, const CNavArea* to, NavDirType toDir) = delete;	// Add spot encounter data when moving from area to area

	// approach areas
	std::array<ApproachInfo, 16> m_approach{};
	unsigned char m_approachCount{};

	void Strip() = delete;						// remove "analyzed" data from nav area

	// A* pathfinding algorithm
	static inline unsigned int m_masterMarker{ 1 };
	unsigned int m_marker{};			// used to flag the area as visited
	CNavArea* m_parent{};				// the area just prior to this on in the search path
	NavTraverseType m_parentHow{};		// how we get from parent to us
	float m_totalCost{};				// the distance so far plus an estimate of the distance left
	float m_costSoFar{};				// distance travelled so far

	static inline CNavArea* m_openList{};
	CNavArea* m_nextOpen{}, * m_prevOpen{};	// only valid if m_openMarker == m_masterMarker
	unsigned int m_openMarker{};			// if this equals the current marker value, we are on the open list

	// connections to adjacent areas
	std::array<NavConnectList, NUM_DIRECTIONS> m_connect{};		// a list of adjacent areas for each direction
	std::array<NavLadderList, NUM_LADDER_DIRECTIONS> m_ladder{};// list of ladders leading up and down from this area

	void FinishMerge(CNavArea* adjArea) = delete;								// recompute internal data once nodes have been adjusted during merge
	void MergeAdjacentConnections(CNavArea* adjArea) = delete;					// for merging with "adjArea" - pick up all of "adjArea"s connections
	void AssignNodes(CNavArea* area) = delete;									// assign internal nodes to the given area
	void FinishSplitEdit(CNavArea* newArea, NavDirType ignoreEdge) = delete;	// given the portion of the original area, update its internal data

	NavAreaList m_overlapList{};							// list of areas that overlap this area
	void OnOtherNavAreaDestroy(CNavArea* dead) noexcept		// invoked when given area is going away
	{
		NavConnect const con{ .area{ dead } };

		for (auto&& c : m_connect)
			c.remove(con);

		std::erase(m_overlapList, dead);
	}

	CNavArea* m_prevHash{}, * m_nextHash{};					// for hash table in CNavAreaGrid
};

export extern "C++" inline auto& TheNavAreaList{ CNavArea::m_masterlist };


#pragma region CNavAreaGrid
void CNavAreaGrid::AddNavArea(CNavArea* area) noexcept
{
	// add to grid
	auto const extent = area->GetExtent();

	int const loX = WorldToGridX(extent->lo.x);
	int const loY = WorldToGridY(extent->lo.y);
	int const hiX = WorldToGridX(extent->hi.x);
	int const hiY = WorldToGridY(extent->hi.y);

	for (int y = loY; y <= hiY; y++)
	{
		for (int x = loX; x <= hiX; x++)
			m_grid[x + y * m_gridSizeX].push_back(const_cast<CNavArea*>(area));
	}

	// add to hash table
	auto const key = ComputeHashKey(area->GetID());

	if (m_hashTable[key])
	{
		// add to head of list in this slot
		area->m_prevHash = nullptr;
		area->m_nextHash = m_hashTable[key];
		m_hashTable[key]->m_prevHash = area;
		m_hashTable[key] = area;
	}
	else
	{
		// first entry in this slot
		m_hashTable[key] = area;
		area->m_nextHash = nullptr;
		area->m_prevHash = nullptr;
	}

	m_areaCount++;
}

void CNavAreaGrid::RemoveNavArea(CNavArea* area) noexcept
{
	// add to grid
	const Extent* extent = area->GetExtent();

	int const loX = WorldToGridX(extent->lo.x);
	int const loY = WorldToGridY(extent->lo.y);
	int const hiX = WorldToGridX(extent->hi.x);
	int const hiY = WorldToGridY(extent->hi.y);

	for (int y = loY; y <= hiY; y++)
	{
		for (int x = loX; x <= hiX; x++)
			std::erase(m_grid[x + y * m_gridSizeX], area);
	}

	// remove from hash table
	int const key = ComputeHashKey(area->GetID());

	if (area->m_prevHash)
	{
		area->m_prevHash->m_nextHash = area->m_nextHash;
	}
	else
	{
		// area was at start of list
		m_hashTable[key] = area->m_nextHash;

		if (m_hashTable[key])
			m_hashTable[key]->m_prevHash = nullptr;
	}

	if (area->m_nextHash)
	{
		area->m_nextHash->m_prevHash = area->m_prevHash;
	}

	m_areaCount--;
}

CNavArea* CNavAreaGrid::GetNavArea(const Vector& pos, float const beneathLimit) const noexcept
{
	if (m_grid.empty())
		return nullptr;

	// get list in cell that contains position
	auto const x = WorldToGridX(pos.x);
	auto const y = WorldToGridY(pos.y);
	auto const& list = m_grid[x + y * m_gridSizeX];

	// search cell list to find correct area
	CNavArea* use = nullptr;
	float useZ = -99999999.9f;
	Vector const testPos = pos + Vector(0, 0, 5);

	for (auto&& area : list)
	{
		// check if position is within 2D boundaries of this area
		if (area->IsOverlapping(testPos))
		{
			// project position onto area to get Z
			auto const z = area->GetZ(testPos);

			// if area is above us, skip it
			if (z > testPos.z)
				continue;

			// if area is too far below us, skip it
			if (z < pos.z - beneathLimit)
				continue;

			// if area is higher than the one we have, use this instead
			if (z > useZ)
			{
				use = area;
				useZ = z;
			}
		}
	}

	return use;
}

CNavArea* CNavAreaGrid::GetNavAreaByID(unsigned int id) const noexcept
{
	if (id == 0)
		return nullptr;

	int key = ComputeHashKey(id);
	for (CNavArea* area = m_hashTable[key]; area; area = area->m_nextHash)
	{
		if (area->GetID() == id)
			return area;
	}

	return nullptr;
}

CNavArea* CNavAreaGrid::GetNearestNavArea(const Vector& pos, bool anyZ) const noexcept
{
	if (m_grid.empty())
		return nullptr;

	CNavArea* close = nullptr;
	double closeDistSq = 100000000.0f;

	// quick check
	close = GetNavArea(pos);
	if (close)
		return close;

	// ensure source position is well behaved
	Vector source{ pos.Make2D(), 0 };

	if (GetGroundHeight(pos, &source.z) == false)
		return nullptr;

	source.z += HalfHumanHeight;

	// #PF_TODO: Step incrementally using grid for speed

	// find closest nav area
	for (auto&& area : TheNavAreaList)
	{
		Vector areaPos{};
		area.GetClosestPointOnArea(source, &areaPos);

		auto const distSq = (areaPos - source).LengthSquared();

		// keep the closest area
		if (distSq < closeDistSq)
		{
			// check LOS to area
			if (!anyZ)
			{
				TraceResult result{};
				g_engfuncs.pfnTraceLine(source, areaPos + Vector(0, 0, HalfHumanHeight), ignore_monsters | ignore_glass, nullptr, &result);
				if (result.flFraction != 1.0f)
					continue;
			}

			closeDistSq = distSq;
			close = &area;
		}
	}

	return close;
}

Place CNavAreaGrid::GetPlace(const Vector& pos) const noexcept
{
	CNavArea* area = GetNearestNavArea(pos, true);
	if (area)
	{
		return area->GetPlace();
	}

	return UNDEFINED_PLACE;
}
#pragma endregion CNavAreaGrid

#pragma region MISC
void DestroyHidingSpots() noexcept
{
	// remove all hiding spot references from the nav areas
	for (auto&& area : TheNavAreaList)
		area.m_hidingSpotList.clear();

	HidingSpot::m_nextID = 1;

	// free all the HidingSpots - the list owns them.
	TheHidingSpotList.clear();
}

__forceinline void DestroyLadders() noexcept
{
	TheNavLadderList.clear();
}

// Free navigation map data
export void DestroyNavigationMap() noexcept
{
	CNavArea::m_isReset = true;

	// remove each element of the list and delete them
	TheNavAreaList.clear();

	CNavArea::m_isReset = false;

	// destroy ladder representations
	DestroyLadders();

	// destroy all hiding spots
	DestroyHidingSpots();

	// reset the grid
	TheNavAreaGrid.Reset();
}

void ClassifySniperSpot(HidingSpot* spot) noexcept
{
	// assume we are crouching
	auto const eye = spot->GetPosition() + Vector(0, 0, HalfHumanHeight);
	Vector walkable{};
	TraceResult result{};

	Extent sniperExtent{};
	double farthestRangeSq = 0;
	constexpr auto minSniperRangeSq = 1000.0 * 1000.0;
	bool found = false;

	for (auto&& area : TheNavAreaList)
	{
		auto const extent = area.GetExtent();

		// scan this area
		for (walkable.y = extent->lo.y + GenerationStepSize / 2.0f; walkable.y < extent->hi.y; walkable.y += GenerationStepSize)
		{
			for (walkable.x = extent->lo.x + GenerationStepSize / 2.0f; walkable.x < extent->hi.x; walkable.x += GenerationStepSize)
			{
				walkable.z = area.GetZ(walkable) + HalfHumanHeight;

				// check line of sight
				g_engfuncs.pfnTraceLine(eye, walkable, ignore_monsters | dont_ignore_glass, nullptr, &result);

				if (result.flFraction == 1.0f && !result.fStartSolid)
				{
					// can see this spot

					// keep track of how far we can see
					auto const rangeSq = (eye - walkable).LengthSquared();
					if (rangeSq > farthestRangeSq)
					{
						farthestRangeSq = rangeSq;

						if (rangeSq >= minSniperRangeSq)
						{
							// this is a sniper spot
							// determine how good of a sniper spot it is by keeping track of the snipable area
							if (found)
							{
								if (walkable.x < sniperExtent.lo.x)
									sniperExtent.lo.x = walkable.x;
								if (walkable.x > sniperExtent.hi.x)
									sniperExtent.hi.x = walkable.x;

								if (walkable.y < sniperExtent.lo.y)
									sniperExtent.lo.y = walkable.y;
								if (walkable.y > sniperExtent.hi.y)
									sniperExtent.hi.y = walkable.y;
							}
							else
							{
								sniperExtent.lo = walkable;
								sniperExtent.hi = walkable;
								found = true;
							}
						}
					}
				}
			}
		}
	}

	if (found)
	{
		// if we can see a large snipable area, it is an "ideal" spot
		float snipableArea = sniperExtent.Area();

		const float minIdealSniperArea = 200.0f * 200.0f;
		const float longSniperRangeSq = 1500.0f * 1500.0f;

		if (snipableArea >= minIdealSniperArea || farthestRangeSq >= longSniperRangeSq)
			spot->SetFlags(HidingSpot::IDEAL_SNIPER_SPOT);
		else
			spot->SetFlags(HidingSpot::GOOD_SNIPER_SPOT);
	}
}

inline CNavArea* FindFirstAreaInDirection(const Vector& start, NavDirType dir, float range, float beneathLimit, CBaseEntity* traceIgnore = nullptr, Vector* closePos = nullptr) noexcept
{
	CNavArea* area = nullptr;
	Vector pos{ start };
	auto const end = int((range / GenerationStepSize) + 0.5f);

	for (int i = 1; i <= end; i++)
	{
		AddDirectionVector(&pos, dir, GenerationStepSize);

		// make sure we dont look thru the wall
		TraceResult result;

		if (traceIgnore)
			g_engfuncs.pfnTraceLine(start, pos, ignore_monsters | dont_ignore_glass, traceIgnore->edict(), &result);
		else
			g_engfuncs.pfnTraceLine(start, pos, ignore_monsters | dont_ignore_glass, nullptr, &result);

		if (result.flFraction != 1.0f)
			break;

		area = TheNavAreaGrid.GetNavArea(pos, beneathLimit);

		if (area)
		{
			if (closePos)
			{
				closePos->x = pos.x;
				closePos->y = pos.y;
				closePos->z = area->GetZ(pos.x, pos.y);
			}

			break;
		}
	}

	return area;
}

#pragma endregion MISC

#pragma region NAV_FILE

// For each ladder in the map, create a navigation representation of it.
void BuildLadders() noexcept
{
	// remove any left-over ladders
	DestroyLadders();

	TraceResult result{};
	for (CBaseEntity* pEntity :
		Query::all_nonplayer_entities()
		| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "func_ladder"); })
		)
	{
		// add ladder to global list
		auto& ladder = TheNavLadderList.emplace_front();

		// compute top & bottom of ladder
		ladder.m_top.x = (pEntity->pev->absmin.x + pEntity->pev->absmax.x) / 2.0f;
		ladder.m_top.y = (pEntity->pev->absmin.y + pEntity->pev->absmax.y) / 2.0f;
		ladder.m_top.z = pEntity->pev->absmax.z;

		ladder.m_bottom.x = ladder.m_top.x;
		ladder.m_bottom.y = ladder.m_top.y;
		ladder.m_bottom.z = pEntity->pev->absmin.z;

		// determine facing - assumes "normal" runged ladder
		float xSize = pEntity->pev->absmax.x - pEntity->pev->absmin.x;
		float ySize = pEntity->pev->absmax.y - pEntity->pev->absmin.y;

		if (xSize > ySize)
		{
			// ladder is facing north or south - determine which way
			// "pull in" traceline from bottom and top in case ladder abuts floor and/or ceiling
			auto const from = ladder.m_bottom + Vector(0.0f, GenerationStepSize, GenerationStepSize);
			auto const to = ladder.m_top + Vector(0.0f, GenerationStepSize, -GenerationStepSize);

			g_engfuncs.pfnTraceLine(from, to, ignore_monsters | dont_ignore_glass, pEntity->edict(), &result);

			if (result.flFraction != 1.0f || result.fStartSolid)
				ladder.m_dir = NORTH;
			else
				ladder.m_dir = SOUTH;
		}
		else
		{
			// ladder is facing east or west - determine which way
			auto const from = ladder.m_bottom + Vector(GenerationStepSize, 0.0f, GenerationStepSize);
			auto const to = ladder.m_top + Vector(GenerationStepSize, 0.0f, -GenerationStepSize);

			g_engfuncs.pfnTraceLine(from, to, ignore_monsters | dont_ignore_glass, pEntity->edict(), &result);

			if (result.flFraction != 1.0f || result.fStartSolid)
				ladder.m_dir = WEST;
			else
				ladder.m_dir = EAST;
		}

		// adjust top and bottom of ladder to make sure they are reachable
		// (cs_office has a crate right in front of the base of a ladder)
		auto const vecDiff = ladder.m_top - ladder.m_bottom;
		auto const along = vecDiff.Normalize();
		auto const length = (float)vecDiff.Length();

		static constexpr float minLadderClearance = 32.0f;

		// adjust bottom to bypass blockages
		static constexpr float inc = 10.0f;
		float t{};

		for (t = 0.0f; t <= length; t += inc)
		{
			auto const on = ladder.m_bottom + t * along;
			auto out = on;

			AddDirectionVector(&out, ladder.m_dir, minLadderClearance);
			g_engfuncs.pfnTraceLine(on, out, ignore_monsters | dont_ignore_glass, pEntity->edict(), &result);

			if (result.flFraction == 1.0f && !result.fStartSolid)
			{
				// found viable ladder bottom
				ladder.m_bottom = on;
				break;
			}
		}

		// adjust top to bypass blockages
		for (t = 0.0f; t <= length; t += inc)
		{
			auto const on = ladder.m_top - t * along;
			auto out = on;

			AddDirectionVector(&out, ladder.m_dir, minLadderClearance);
			g_engfuncs.pfnTraceLine(on, out, ignore_monsters | dont_ignore_glass, pEntity->edict(), &result);

			if (result.flFraction == 1.0f && !result.fStartSolid)
			{
				// found viable ladder top
				ladder.m_top = on;
				break;
			}
		}

		ladder.m_length = (float)(ladder.m_top - ladder.m_bottom).Length();
		DirectionToVector2D(ladder.m_dir, &ladder.m_dirVector);

		ladder.m_entity = pEntity;
		static constexpr float nearLadderRange = 75.0f;

		// Find naviagtion area at bottom of ladder
		// get approximate postion of player on ladder

		auto center = ladder.m_bottom + Vector(0, 0, GenerationStepSize);
		AddDirectionVector(&center, ladder.m_dir, HalfHumanWidth);

		ladder.m_bottomArea = TheNavAreaGrid.GetNearestNavArea(center, true);
		if (!ladder.m_bottomArea)
			g_engfuncs.pfnAlertMessage(at_console, "ERROR: Unconnected ladder bottom at (%g, %g, %g)\n", ladder.m_bottom.x, ladder.m_bottom.y, ladder.m_bottom.z);
		else
			// store reference to ladder in the area
			ladder.m_bottomArea->AddLadderUp(&ladder);

		// Find adjacent navigation areas at the top of the ladder
		// get approximate postion of player on ladder
		center = ladder.m_top + Vector(0, 0, GenerationStepSize);
		AddDirectionVector(&center, ladder.m_dir, HalfHumanWidth);

		// find "ahead" area
		ladder.m_topForwardArea = FindFirstAreaInDirection(center, Opposite[ladder.m_dir], nearLadderRange, 120.0f, pEntity);
		if (ladder.m_topForwardArea == ladder.m_bottomArea)
			ladder.m_topForwardArea = nullptr;

		// find "left" area
		ladder.m_topLeftArea = FindFirstAreaInDirection(center, DirectionLeft(ladder.m_dir), nearLadderRange, 120.0f, pEntity);
		if (ladder.m_topLeftArea == ladder.m_bottomArea)
			ladder.m_topLeftArea = nullptr;

		// find "right" area
		ladder.m_topRightArea = FindFirstAreaInDirection(center, DirectionRight(ladder.m_dir), nearLadderRange, 120.0f, pEntity);
		if (ladder.m_topRightArea == ladder.m_bottomArea)
			ladder.m_topRightArea = nullptr;

		// find "behind" area - must look farther, since ladder is against the wall away from this area
		ladder.m_topBehindArea = FindFirstAreaInDirection(center, ladder.m_dir, 2.0f * nearLadderRange, 120.0f, pEntity);
		if (ladder.m_topBehindArea == ladder.m_bottomArea)
			ladder.m_topBehindArea = nullptr;

		// can't include behind area, since it is not used when going up a ladder
		if (!ladder.m_topForwardArea && !ladder.m_topLeftArea && !ladder.m_topRightArea)
			g_engfuncs.pfnAlertMessage(at_console, "ERROR: Unconnected ladder top at (%g, %g, %g)\n", ladder.m_top.x, ladder.m_top.y, ladder.m_top.z);

		// store reference to ladder in the area(s)
		if (ladder.m_topForwardArea)
			ladder.m_topForwardArea->AddLadderDown(&ladder);

		if (ladder.m_topLeftArea)
			ladder.m_topLeftArea->AddLadderDown(&ladder);

		if (ladder.m_topRightArea)
			ladder.m_topRightArea->AddLadderDown(&ladder);

		if (ladder.m_topBehindArea)
			ladder.m_topBehindArea->AddLadderDown(&ladder);

		// adjust top of ladder to highest connected area
		float topZ = -99999.9f;
		bool topAdjusted = false;

		std::array const topAreaList
		{
			ladder.m_topForwardArea,	//	NORTH_WEST,
			ladder.m_topLeftArea,		//	NORTH_EAST,
			ladder.m_topRightArea,		//	SOUTH_EAST,
			ladder.m_topBehindArea,	//	SOUTH_WEST,
		};

		for (auto&& topArea : topAreaList)
		{
			if (!topArea)
				continue;

			Vector close{};
			topArea->GetClosestPointOnArea(ladder.m_top, &close);
			if (topZ < close.z)
			{
				topZ = close.z;
				topAdjusted = true;
			}
		}

		if (topAdjusted)
			ladder.m_top.z = topZ;

		// Determine whether this ladder is "dangling" or not
		// "Dangling" ladders are too high to go up
		ladder.m_isDangling = false;
		if (ladder.m_bottomArea)
		{
			Vector bottomSpot{};
			ladder.m_bottomArea->GetClosestPointOnArea(ladder.m_bottom, &bottomSpot);
			if (ladder.m_bottom.z - bottomSpot.z > HumanHeight)
				ladder.m_isDangling = true;
		}
	}
}

export NavErrorType LoadNavigationMap() noexcept
{
	// since the navigation map is destroyed on map change,
	// if it exists it has already been loaded for this map
	if (!TheNavAreaList.empty())
		return NAV_OK;

	// nav filename is derived from map filename
	auto const filename = std::format("maps\\{}.nav", STRING(gpGlobals->mapname));

	// free previous navigation map data
	DestroyNavigationMap();
	placeDirectory.Reset();

	CNavArea::m_nextID = 1;

	SteamFile navFile(filename.c_str());

	if (!navFile.IsValid())
		return NAV_CANT_ACCESS_FILE;

	// check magic number
	bool result;
	unsigned int magic;
	result = navFile.Read(&magic, sizeof(unsigned int));
	if (!result || magic != NAV_MAGIC_NUMBER)
	{
		CONSOLE_ECHO("ERROR: Invalid navigation file '%s'.\n", filename);
		return NAV_INVALID_FILE;
	}

	// read file version number
	unsigned int version;
	result = navFile.Read(&version, sizeof(unsigned int));
	if (!result || version > NAV_VERSION)
	{
		CONSOLE_ECHO("ERROR: Unknown navigation file version.\n");
		return NAV_BAD_FILE_VERSION;
	}

	if (version >= 4)
	{
		// get size of source bsp file and verify that the bsp hasn't changed
		unsigned int saveBspSize;
		navFile.Read(&saveBspSize, sizeof(unsigned int));

		// verify size
		auto const bspFilename = std::format("maps\\{}.bsp", STRING(gpGlobals->mapname));
		unsigned int bspSize = (unsigned int)g_engfuncs.pfnGetFileSize(bspFilename.c_str());
		if (bspSize != saveBspSize)
		{
			// this nav file is out of date for this bsp file
			auto const msg = "*** WARNING ***\nThe AI navigation data is from a different version of this map.\nThe CPU players will likely not perform well.\n";
			CONSOLE_ECHO("\n-----------------\n");
			CONSOLE_ECHO(msg);
			CONSOLE_ECHO("-----------------\n\n");
		}
	}

	// load Place directory
	if (version >= NAV_VERSION)
	{
		placeDirectory.Load(&navFile);
	}

	// get number of areas
	unsigned int count;
	result = navFile.Read(&count, sizeof(unsigned int));

	Extent extent;
	extent.lo.x = 9999999999.9f;
	extent.lo.y = 9999999999.9f;
	extent.hi.x = -9999999999.9f;
	extent.hi.y = -9999999999.9f;

	// load the areas and compute total extent
	for (unsigned int i = 0; i < count; i++)
	{
		auto& area = TheNavAreaList.emplace_front();
		area.Load(&navFile, version);

		auto const areaExtent = area.GetExtent();

		// check validity of nav area
		if (areaExtent->lo.x >= areaExtent->hi.x || areaExtent->lo.y >= areaExtent->hi.y) {
			CONSOLE_ECHO("WARNING: Degenerate Navigation Area #%d at ( %g, %g, %g )\n", area.GetID(), area.m_center.x, area.m_center.y, area.m_center.z);
		}

		if (areaExtent->lo.x < extent.lo.x)
			extent.lo.x = areaExtent->lo.x;

		if (areaExtent->lo.y < extent.lo.y)
			extent.lo.y = areaExtent->lo.y;

		if (areaExtent->hi.x > extent.hi.x)
			extent.hi.x = areaExtent->hi.x;

		if (areaExtent->hi.y > extent.hi.y)
			extent.hi.y = areaExtent->hi.y;
	}

	// add the areas to the grid
	TheNavAreaGrid.Initialize(extent.lo.x, extent.hi.x, extent.lo.y, extent.hi.y);

	for (auto&& area : TheNavAreaList)
		TheNavAreaGrid.AddNavArea(&area);

	// allow areas to connect to each other, etc
	for (auto&& area : TheNavAreaList)
		area.PostLoad();

#ifdef CSBOT_PHRASE
	// load legacy location file (Places)
	if (version < NAV_VERSION)
	{
		LoadLocationFile(filename);
	}
#endif

	// Set up all the ladders
	BuildLadders();
	return NAV_OK;
}

#pragma endregion NAV_FILE
