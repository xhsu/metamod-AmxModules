export module Nav:HidingSpot;

import std;
import hlsdk;

import :Const;

export class HidingSpot final
{
public:
	HidingSpot(HidingSpot const&) noexcept = default;
	HidingSpot(HidingSpot&&) noexcept = default;
	HidingSpot& operator=(HidingSpot const&) noexcept = default;
	HidingSpot& operator=(HidingSpot&&) noexcept = default;

	static HidingSpot* Create(const Vector& pos, unsigned char flags) noexcept
	{
		HidingSpot obj{ pos, flags };
		auto& ref = m_masterlist.emplace_front(std::move(obj));
		return &ref;
	}

	static HidingSpot* Create() noexcept
	{
		HidingSpot obj{};
		auto& ref = m_masterlist.emplace_front(std::move(obj));
		return &ref;
	}

	enum : std::uint8_t
	{
		IN_COVER = 0x01,
		GOOD_SNIPER_SPOT = 0x02,
		IDEAL_SNIPER_SPOT = 0x04
	};

	constexpr bool HasGoodCover() const noexcept { return (m_flags & IN_COVER) ? true : false; }
	constexpr bool IsGoodSniperSpot() const noexcept { return (m_flags & GOOD_SNIPER_SPOT) ? true : false; }
	constexpr bool IsIdealSniperSpot() const noexcept { return (m_flags & IDEAL_SNIPER_SPOT) ? true : false; }

	constexpr void SetFlags(unsigned char flags) noexcept { m_flags |= flags; }
	constexpr unsigned char GetFlags() const noexcept { return m_flags; }

#ifdef CSBOT_SAVE
	void Save(FILE* fd, unsigned int version) const noexcept
	{
		std::fwrite(&m_id, sizeof(unsigned int), 1, fd);
		std::fwrite(&m_pos, sizeof(float), 3, fd);
		std::fwrite(&m_flags, sizeof(unsigned char), 1, fd);
	}
#endif

	void Load(SteamFile* file, unsigned int version) noexcept
	{
		file->Read(&m_id, sizeof(unsigned int));
		file->Read(&m_pos, 3 * sizeof(float));
		file->Read(&m_flags, sizeof(unsigned char));

		// update next ID to avoid ID collisions by later spots
		if (m_id >= m_nextID)
			m_nextID = m_id + 1;
	}

	constexpr const Vector& GetPosition() const noexcept { return m_pos; }
	constexpr unsigned int GetID() const noexcept { return m_id; }

	void Mark() noexcept { m_marker = m_masterMarker; }
	bool IsMarked() const noexcept { return (m_marker == m_masterMarker) ? true : false; }

	static void ChangeMasterMarker() noexcept { m_masterMarker++; }

	static inline std::forward_list<HidingSpot> m_masterlist{};

private:
	HidingSpot() noexcept = default;
	HidingSpot(const Vector& pos, unsigned char flags) noexcept : m_pos{ pos }, m_flags{ flags }, m_id{ m_nextID++ } {}

	friend void DestroyHidingSpots() noexcept;

	Vector m_pos{};
	unsigned int m_id{};
	unsigned int m_marker{};
	unsigned char m_flags{};

	static inline unsigned int m_nextID{ 1 };
	static inline unsigned int m_masterMarker{ 0 };
};

export using HidingSpotList = std::vector<HidingSpot*>;
export extern "C++" inline auto& TheHidingSpotList{ HidingSpot::m_masterlist };

// Given a HidingSpot ID, return the associated HidingSpot
HidingSpot* GetHidingSpotByID(unsigned int id) noexcept
{
	for (auto&& spot : TheHidingSpotList)
	{
		if (spot.GetID() == id)
			return &spot;
	}

	return nullptr;
}

bool IsHidingSpotInCover(const Vector& spot) noexcept
{
	int coverCount = 0;
	TraceResult result{};

	Vector const from{ spot.Make2D(), spot.z + HalfHumanHeight };

	// if we are crouched underneath something, that counts as good cover
	auto to = from + Vector(0, 0, 20.0f);
	g_engfuncs.pfnTraceLine(from, to, ignore_monsters | dont_ignore_glass, nullptr, &result);
	if (result.flFraction != 1.0f)
		return true;

	static constexpr auto coverRange = 100.0f;
	static constexpr auto inc = std::numbers::pi / 8.0f;

	for (auto angle = 0.0; angle < 2.0 * std::numbers::pi; angle += inc)
	{
		to = from + Vector(coverRange * std::cos(angle), coverRange * std::sin(angle), HalfHumanHeight);

		g_engfuncs.pfnTraceLine(from, to, ignore_monsters | dont_ignore_glass, nullptr, &result);

		// if traceline hit something, it hit "cover"
		if (result.flFraction != 1.0f)
			coverCount++;
	}

	// if more than half of the circle has no cover, the spot is not "in cover"
	static constexpr int halfCover = 8;
	if (coverCount < halfCover)
		return false;

	return true;
}
