module;

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

export module Nav:Const;

import std;
import hlsdk;

#pragma region steam_util.h
export class SteamFile
{
public:
	SteamFile(const char* filename) noexcept
	{
		m_fileData = (std::byte*)g_engfuncs.pfnLoadFileForMe(filename, &m_fileDataLength);
		m_cursor = m_fileData;
		m_bytesLeft = m_fileDataLength;
	}
	~SteamFile() noexcept
	{
		if (m_fileData)
		{
			g_engfuncs.pfnFreeFile(m_fileData);
			m_fileData = nullptr;
		}
	}

	bool IsValid() const noexcept { return (m_fileData) ? true : false; }
	bool Read(void* data, int length) noexcept
	{
		if (length > m_bytesLeft || !m_cursor || m_bytesLeft <= 0)
			return false;

		std::byte* readCursor = static_cast<std::byte*>(data);
		for (int i = 0; i < length; i++)
		{
			*readCursor++ = *m_cursor++;
			m_bytesLeft--;
		}

		return true;
	}

private:
	std::byte* m_fileData{};
	int m_fileDataLength{};

	std::byte* m_cursor{};
	int m_bytesLeft{};
};
#pragma endregion steam_util.h

#pragma region bot_util.h
void CONSOLE_ECHO(const char* pszMsg, ...) noexcept
{
	va_list argptr;
	static char szStr[1024]{};

	va_start(argptr, pszMsg);
	_vsnprintf(szStr, sizeof(szStr) - 1, pszMsg, argptr);
	va_end(argptr);

	g_engfuncs.pfnServerPrint(szStr);
}

// Simple class for counting down a short interval of time
export class CountdownTimer
{
public:
	void Reset() noexcept { m_timestamp = gpGlobals->time + m_duration; }

	void Start(float duration) noexcept { m_timestamp = gpGlobals->time + duration; m_duration = duration; }
	bool HasStarted() const noexcept { return (m_timestamp > 0.0f); }

	void Invalidate() noexcept { m_timestamp = -1.0f; }
	bool IsElapsed() const noexcept { return (gpGlobals->time > m_timestamp); }

private:
	float m_duration{ 0.f };
	float m_timestamp{ -1.f };
};

#pragma endregion bot_util.h

#pragma region Extent
export struct Extent final
{
	Vector lo{};
	Vector hi{};

	constexpr float SizeX() const noexcept { return hi.x - lo.x; }
	constexpr float SizeY() const noexcept { return hi.y - lo.y; }
	constexpr float SizeZ() const noexcept { return hi.z - lo.z; }
	constexpr float Area()  const noexcept { return SizeX() * SizeY(); }

	// return true if 'pos' is inside of this extent
	constexpr bool Contains(const Vector* pos) const noexcept
	{
		return (pos->x >= lo.x && pos->x <= hi.x &&
			pos->y >= lo.y && pos->y <= hi.y &&
			pos->z >= lo.z && pos->z <= hi.z);
	}
};
#pragma endregion Extent

#pragma region Place
// A place is a named group of navigation areas
export using Place = unsigned int;

// ie: "no place"
export inline constexpr Place UNDEFINED_PLACE = 0;
export inline constexpr Place ANY_PLACE = 0xFFFF;

export inline constexpr std::string_view g_rgszDefaultPlaceNames[] =
{
	"BombsiteA",
	"BombsiteB",
	"BombsiteC",
	"Hostages",
	"HostageRescueZone",
	"VipRescueZone",
	"CTSpawn",
	"TSpawn",
	"Bridge",
	"Middle",
	"House",
	"Apartment",
	"Apartments",
	"Market",
	"Sewers",
	"Tunnel",
	"Ducts",
	"Village",
	"Roof",
	"Upstairs",
	"Downstairs",
	"Basement",
	"Crawlspace",
	"Kitchen",
	"Inside",
	"Outside",
	"Tower",
	"WineCellar",
	"Garage",
	"Courtyard",
	"Water",
	"FrontDoor",
	"BackDoor",
	"SideDoor",
	"BackWay",
	"FrontYard",
	"BackYard",
	"SideYard",
	"Lobby",
	"Vault",
	"Elevator",
	"DoubleDoors",
	"SecurityDoors",
	"LongHall",
	"SideHall",
	"FrontHall",
	"BackHall",
	"MainHall",
	"FarSide",
	"Windows",
	"Window",
	"Attic",
	"StorageRoom",
	"ProjectorRoom",
	"MeetingRoom",
	"ConferenceRoom",
	"ComputerRoom",
	"BigOffice",
	"LittleOffice",
	"Dumpster",
	"Airplane",
	"Underground",
	"Bunker",
	"Mines",
	"Front",
	"Back",
	"Rear",
	"Side",
	"Ramp",
	"Underpass",
	"Overpass",
	"Stairs",
	"Ladder",
	"Gate",
	"GateHouse",
	"LoadingDock",
	"GuardHouse",
	"Entrance",
	"VendingMachines",
	"Loft",
	"Balcony",
	"Alley",
	"BackAlley",
	"SideAlley",
	"FrontRoom",
	"BackRoom",
	"SideRoom",
	"Crates",
	"Truck",
	"Bedroom",
	"FamilyRoom",
	"Bathroom",
	"LivingRoom",
	"Den",
	"Office",
	"Atrium",
	"Entryway",
	"Foyer",
	"Stairwell",
	"Fence",
	"Deck",
	"Porch",
	"Patio",
	"Wall"
};

export Place Place_NameToID(const char* name) noexcept
{
	for (Place place = 0; place < std::ssize(g_rgszDefaultPlaceNames); place++)
	{
		if (!_stricmp(g_rgszDefaultPlaceNames[place].data(), name))
			return place + 1;
	}

	return UNDEFINED_PLACE;
}

export auto Place_IDToName(Place place) noexcept -> std::optional<std::string_view>
{
	if (place <= 0 || place > std::ssize(g_rgszDefaultPlaceNames))
		return std::nullopt;

	return g_rgszDefaultPlaceNames[place - 1];
}

#pragma endregion Place

#pragma region CNavArea

export enum NavCornerType
{
	NORTH_WEST = 0,
	NORTH_EAST,
	SOUTH_EAST,
	SOUTH_WEST,

	NUM_CORNERS
};

export enum NavAttributeType
{
	NAV_CROUCH = 0x01, // must crouch to use this node/area
	NAV_JUMP = 0x02, // must jump to traverse this area
	NAV_PRECISE = 0x04, // do not adjust for obstacles, just move along area
	NAV_NO_JUMP = 0x08, // inhibit discontinuity jumping
};

// Defines possible ways to move from one area to another
export enum NavTraverseType
{
	// NOTE: First 4 directions MUST match NavDirType
	GO_NORTH = 0,
	GO_EAST,
	GO_SOUTH,
	GO_WEST,
	GO_LADDER_UP,
	GO_LADDER_DOWN,
	GO_JUMP,

	NUM_TRAVERSE_TYPES
};

export union NavConnect
{
	std::uintptr_t id{};
	class CNavArea* area;

	bool operator==(const NavConnect& other) const noexcept { return (area == other.area) ? true : false; }
};

export using NavConnectList = std::list<NavConnect>;

export struct Ray
{
	Vector from;
	Vector to;
};

export enum LadderDirectionType
{
	LADDER_UP = 0,
	LADDER_DOWN,
	NUM_LADDER_DIRECTIONS
};

export enum RouteType
{
	FASTEST_ROUTE,
	SAFEST_ROUTE,
};

#pragma endregion CNavArea

#pragma region nav_file
// version
// 1 = hiding spots as plain vector array
// 2 = hiding spots as HidingSpot objects
// 3 = Encounter spots use HidingSpot ID's instead of storing vector again
// 4 = Includes size of source bsp file to verify nav data correlation
// ---- Beta Release at V4 -----
// 5 = Added Place info
export inline constexpr auto NAV_VERSION = 5;

// The 'place directory' is used to save and load places from
// nav files in a size-efficient manner that also allows for the
// order of the place ID's to change without invalidating the
// nav files.
//
// The place directory is stored in the nav file as a list of
// place name strings.  Each nav area then contains an index
// into that directory, or zero if no place has been assigned to
// that area.
export class PlaceDirectory
{
public:
	typedef unsigned short EntryType;

	void Reset() noexcept
	{
		m_directory.clear();
	}

	// return true if this place is already in the directory
	bool IsKnown(Place place) const noexcept
	{
		auto it = std::find(m_directory.begin(), m_directory.end(), place);
		return (it != m_directory.end());
	}

	// return the directory entry corresponding to this Place (0 = no entry)
	EntryType GetEntry(Place place) const noexcept
	{
		if (place == UNDEFINED_PLACE)
			return 0;

		auto it = std::find(m_directory.begin(), m_directory.end(), place);
		if (it == m_directory.end())
		{
			assert(false && "PlaceDirectory::GetEntry failure");
			return 0;
		}

		return 1 + (it - m_directory.begin());
	}

	// add the place to the directory if not already known
	void AddPlace(Place place) noexcept
	{
		if (place == UNDEFINED_PLACE)
			return;

		assert(place < 1000);

		if (IsKnown(place))
			return;

		m_directory.push_back(place);
	}

	// given an entry, return the Place
	Place EntryToPlace(EntryType entry) const noexcept
	{
		if (entry == 0 || !m_directory.size())
			return UNDEFINED_PLACE;

		auto const i = entry - 1;
		if (i > std::ssize(m_directory))
		{
			assert(false && "PlaceDirectory::EntryToPlace: Invalid entry");
			return UNDEFINED_PLACE;
		}

		return m_directory[i];
	}

#ifdef CSBOT_ENABLE_SAVE
	// store the directory
	void Save(int fd);
#endif

	// load the directory
	void Load(SteamFile* file) noexcept
	{
		// read number of entries
		EntryType count;
		file->Read(&count, sizeof(EntryType));

		m_directory.reserve(count);

		// read each entry
		char placeName[256]{};
		unsigned short len{};

		for (int i = 0; i < count; i++)
		{
			file->Read(&len, sizeof(unsigned short));
			file->Read(placeName, len);

#ifdef CSBOT_PHRASES
			Place place = TheBotPhrases->NameToID(placeName);
			if (!TheBotPhrases->IsValid() && place == UNDEFINED_PLACE)
				place = TheNavAreaGrid.NameToID(placeName);
#endif
			auto const place = Place_NameToID(placeName);
			AddPlace(place);
		}
	}

private:
	std::vector<Place> m_directory{};
};

#ifdef CSBOT_ENABLE_SAVE
bool SaveNavigationMap(const char* filename);
void LoadLocationFile(const char* filename);
void SanityCheckNavigationMap(const char* mapName);	// Performs a lightweight sanity-check of the specified map's nav mesh
NavErrorType LoadNavigationMap();
#endif

export inline PlaceDirectory placeDirectory{};

#pragma endregion nav_file
