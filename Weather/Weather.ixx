export module Weather;

import std;

import UtlRandom;

import Task;
import Query;

import Forwards;
import Plugin;
import Resources;

export enum struct EWeather : uint8_t
{
	Sunny = 1,
	Drizzle = 2,
	ThunderStorm = 3,
	Tempest = 4,
	Snow = 5,
	Sleet = 6,
	BlackFog = 7,
};

export inline void UTIL_SetFog(uint8_t r, uint8_t g, uint8_t b, float flDensity) noexcept
{
	static int32_t iSize = -1;
	static auto const gmsgFog = gpMetaUtilFuncs->pfnGetUserMsgID(PLID, "Fog", &iSize);

	g_engfuncs.pfnMessageBegin(MSG_ALL, gmsgFog, nullptr, nullptr);
	g_engfuncs.pfnWriteByte(r);
	g_engfuncs.pfnWriteByte(g);
	g_engfuncs.pfnWriteByte(b);
	g_engfuncs.pfnWriteLong(std::bit_cast<int32_t>(flDensity));
	g_engfuncs.pfnMessageEnd();
}

export enum struct EReceiveW : uint8_t
{
	Clear = 0,
	Rain,
	Snow,
};

export inline void UTIL_SetReceiveW(EReceiveW weather) noexcept
{
	static int32_t iSize = -1;
	static auto const gmsgReceiveW = gpMetaUtilFuncs->pfnGetUserMsgID(PLID, "ReceiveW", &iSize);

	g_engfuncs.pfnMessageBegin(MSG_ALL, gmsgReceiveW, nullptr, nullptr);
	g_engfuncs.pfnWriteByte(std::to_underlying(weather));
	g_engfuncs.pfnMessageEnd();
}

inline void UTIL_StopSoundForEveryone() noexcept
{
	for (CBasePlayer* pPlayer : Query::all_players())
	{
		g_engfuncs.pfnClientCommand(pPlayer->edict(), "stopsound\n");
	}
}

Task Task_Sound_MinorRain() noexcept
{
	for (;;)
	{
		for (CBasePlayer* pPlayer : Query::all_players())
		{
			g_engfuncs.pfnClientCommand(pPlayer->edict(), "spk %s\n", SFX_RAIN[0].data());
		}

		co_await 5.f;
	}
}

Task Task_Sound_MajorRain() noexcept
{
	for (;;)
	{
		for (CBasePlayer* pPlayer : Query::all_players())
		{
			g_engfuncs.pfnClientCommand(pPlayer->edict(), "spk %s\n", SFX_RAIN[1].data());
		}

		co_await 8.f;
	}
}

Task Task_VFX_MinorThunder(std::array<char, 2> LightStyle) noexcept
{
	uint8_t iLightningCount = 0;

	for (;;)
	{
		co_await UTIL_Random(10.f, 15.f);

		MF_ExecuteForward(Forwards::OnMinorThunder, (cell)LightStyle[0]);

		for (CBasePlayer* pPlayer : Query::all_players())
		{
			g_engfuncs.pfnClientCommand(pPlayer->edict(), "spk %s\n", UTIL_GetRandomOne(thundersound).data());
		}

		for (iLightningCount = UTIL_Random(1, 2); iLightningCount; --iLightningCount)
		{
			g_engfuncs.pfnLightStyle(0, "#");

			co_await UTIL_Random(0.03f, 0.04f);

			// NVG special treatment??
			g_engfuncs.pfnLightStyle(0, LightStyle.data());

			co_await UTIL_Random(0.1f, 0.2f);
		}
	}
}

Task Task_VFX_MajorThunder(std::array<char, 2> LightStyle) noexcept
{
	uint8_t iLightningCount = 0;

	for (;;)
	{
		co_await UTIL_Random(2.f, 4.5f);

		MF_ExecuteForward(Forwards::OnMajorThunder, (cell)LightStyle[0]);

		// Make lightning
		for (iLightningCount = UTIL_Random(1, 2); iLightningCount; --iLightningCount)
		{
			// Line of sight change 'cause of lightning.
			UTIL_SetFog(0, 0, 0, 0);
			g_engfuncs.pfnLightStyle(0, "#");

			std::vector<Vector> rgvecCandidates;
			for (CBasePlayer* pPlayer : Query::all_living_players())
			{
				TraceResult tr{};
				g_engfuncs.pfnTraceLine(
					pPlayer->pev->origin,
					pPlayer->pev->origin + Vector{ 0, 0, 8192 },
					ignore_monsters | ignore_glass,
					pPlayer->edict(),
					&tr
				);

				if (g_engfuncs.pfnPointContents(tr.vecEndPos) == CONTENTS_SKY)
					rgvecCandidates.push_back(pPlayer->pev->origin);
			}

			if (!rgvecCandidates.empty())
			{
				constexpr auto SPARK_SIZE = 20.f;
				std::array<Vector, 3> rgvecLightningEnds{ UTIL_GetRandomOne(rgvecCandidates) };

				rgvecLightningEnds[1] =
					rgvecLightningEnds[0] + Vector{ UTIL_Random(-48.f, 48.f), UTIL_Random(-48.f, 48.f), UTIL_Random(30.f, 35.f) } *SPARK_SIZE;
				rgvecLightningEnds[2] =
					rgvecLightningEnds[0] + Vector{ UTIL_Random(-1.f, 1.f), UTIL_Random(-1.f, 1.f), UTIL_Random(50.f, 55.f) } *SPARK_SIZE;

				for (size_t i = 1; i < rgvecLightningEnds.size(); ++i)
				{
					g_engfuncs.pfnMessageBegin(MSG_BROADCAST, SVC_TEMPENTITY, nullptr, nullptr);
					g_engfuncs.pfnWriteByte(TE_BEAMPOINTS);
					g_engfuncs.pfnWriteCoord(rgvecLightningEnds[i - 1][0]);
					g_engfuncs.pfnWriteCoord(rgvecLightningEnds[i - 1][1]);
					g_engfuncs.pfnWriteCoord(rgvecLightningEnds[i - 1][2]);
					g_engfuncs.pfnWriteCoord(rgvecLightningEnds[i][0]);
					g_engfuncs.pfnWriteCoord(rgvecLightningEnds[i][1]);
					g_engfuncs.pfnWriteCoord(rgvecLightningEnds[i][2]);
					g_engfuncs.pfnWriteShort(g_fxbeam);
					g_engfuncs.pfnWriteByte(0);
					g_engfuncs.pfnWriteByte(2);
					g_engfuncs.pfnWriteByte(2);
					g_engfuncs.pfnWriteByte(15);
					g_engfuncs.pfnWriteByte(150);
					g_engfuncs.pfnWriteByte(255);
					g_engfuncs.pfnWriteByte(255);
					g_engfuncs.pfnWriteByte(255);
					g_engfuncs.pfnWriteByte(255);
					g_engfuncs.pfnWriteByte(UTIL_Random(20, 30));
					g_engfuncs.pfnMessageEnd();
				}

				co_await 0.01f;

				for (auto&& v : rgvecCandidates)
				{
					g_engfuncs.pfnMessageBegin(MSG_BROADCAST, SVC_TEMPENTITY, nullptr, nullptr);
					g_engfuncs.pfnWriteByte(TE_SPARKS);
					g_engfuncs.pfnWriteCoord(v.x);
					g_engfuncs.pfnWriteCoord(v.y);
					g_engfuncs.pfnWriteCoord(v.z);
					g_engfuncs.pfnMessageEnd();
				}
			}

			co_await UTIL_Random(0.03f, 0.04f);

			// NVG special treatment??
			UTIL_SetFog(5, 5, 5, 0.003f);
			g_engfuncs.pfnLightStyle(0, LightStyle.data());

			co_await UTIL_Random(0.1f, 0.2f);
		}

		// Thunder sfx, a bit late to simulate speed of sound.
		co_await UTIL_Random(0.1f, 0.2f);

		for (CBasePlayer* pPlayer : Query::all_players())
			g_engfuncs.pfnClientCommand(pPlayer->edict(), "spk %s\n", UTIL_GetRandomOne(thunderclap).data());
	}
}

Task Task_VFX_Sleet() noexcept
{
	for (;;)
	{
		co_await 0.01f;

		UTIL_SetReceiveW(EReceiveW::Rain);

		co_await 0.01f;

		UTIL_SetReceiveW(EReceiveW::Snow);
	}
}

export void Sunny(char LightLevel = 'f') noexcept
{
	static std::array<char, 2> szLightLevel;
	szLightLevel[0] = LightLevel;

	TaskScheduler::Clear();

	UTIL_StopSoundForEveryone();
	UTIL_SetFog(0, 0, 0, 0);
	UTIL_SetReceiveW(EReceiveW::Clear);
	g_engfuncs.pfnLightStyle(0, szLightLevel.data());

	MF_ExecuteForward(Forwards::OnWeatherChange, (cell)EWeather::Sunny, (cell)LightLevel);
}

export void Drizzle(char LightLevel = 'e') noexcept
{
	static std::array<char, 2> szLightLevel;
	szLightLevel[0] = LightLevel;

	TaskScheduler::Clear();

	UTIL_StopSoundForEveryone();
	UTIL_SetFog(0, 0, 0, 0);
	UTIL_SetReceiveW(EReceiveW::Rain);
	g_engfuncs.pfnLightStyle(0, szLightLevel.data());

	TaskScheduler::Enroll(Task_Sound_MinorRain());

	MF_ExecuteForward(Forwards::OnWeatherChange, (cell)EWeather::Drizzle, (cell)LightLevel);
}

export void ThunderStorm(char LightLevel = 'c') noexcept
{
	static std::array<char, 2> szLightLevel;
	szLightLevel[0] = LightLevel;

	TaskScheduler::Clear();

	UTIL_StopSoundForEveryone();
	UTIL_SetFog(0, 0, 0, 0);
	UTIL_SetReceiveW(EReceiveW::Rain);
	g_engfuncs.pfnLightStyle(0, szLightLevel.data());

	TaskScheduler::Enroll(Task_Sound_MinorRain());
	TaskScheduler::Enroll(Task_VFX_MinorThunder(szLightLevel));

	MF_ExecuteForward(Forwards::OnWeatherChange, (cell)EWeather::ThunderStorm, (cell)LightLevel);
}

export void Tempest(char LightLevel = 'b') noexcept
{
	static std::array<char, 2> szLightLevel;
	szLightLevel[0] = LightLevel;

	TaskScheduler::Clear();

	UTIL_StopSoundForEveryone();
	UTIL_SetFog(5, 5, 5, 0.003f);
	UTIL_SetReceiveW(EReceiveW::Rain);
	g_engfuncs.pfnLightStyle(0, szLightLevel.data());

	TaskScheduler::Enroll(Task_Sound_MajorRain());
	TaskScheduler::Enroll(Task_VFX_MajorThunder(szLightLevel));

	MF_ExecuteForward(Forwards::OnWeatherChange, (cell)EWeather::Tempest, (cell)LightLevel);
}

export void Snow(char LightLevel = 'e') noexcept
{
	static std::array<char, 2> szLightLevel;
	szLightLevel[0] = LightLevel;

	TaskScheduler::Clear();

	UTIL_StopSoundForEveryone();
	UTIL_SetFog(200, 200, 200, 0.00386f);
	UTIL_SetReceiveW(EReceiveW::Snow);
	g_engfuncs.pfnLightStyle(0, szLightLevel.data());

	MF_ExecuteForward(Forwards::OnWeatherChange, (cell)EWeather::Snow, (cell)LightLevel);
}

export void Fog(char LightLevel = 'e') noexcept
{
	static std::array<char, 2> szLightLevel;
	szLightLevel[0] = LightLevel;

	TaskScheduler::Clear();

	UTIL_StopSoundForEveryone();
	UTIL_SetFog(100, 100, 100, 0.0034f);
	UTIL_SetReceiveW(EReceiveW::Clear);
	g_engfuncs.pfnLightStyle(0, szLightLevel.data());
}

export void BlackFog(char LightLevel = 'e') noexcept
{
	static std::array<char, 2> szLightLevel;
	szLightLevel[0] = LightLevel;

	TaskScheduler::Clear();

	UTIL_StopSoundForEveryone();
	UTIL_SetFog(0, 0, 0, 0.0023708497f);
	UTIL_SetReceiveW(EReceiveW::Clear);
	g_engfuncs.pfnLightStyle(0, szLightLevel.data());

	MF_ExecuteForward(Forwards::OnWeatherChange, (cell)EWeather::BlackFog, (cell)LightLevel);
}

export void Sleet(char LightLevel = 'e') noexcept
{
	static std::array<char, 2> szLightLevel;
	szLightLevel[0] = LightLevel;

	TaskScheduler::Clear();

	UTIL_StopSoundForEveryone();
	UTIL_SetFog(200, 200, 200, 0.001f);
	UTIL_SetReceiveW(EReceiveW::Snow);
	g_engfuncs.pfnLightStyle(0, szLightLevel.data());

	TaskScheduler::Enroll(Task_VFX_Sleet());

	MF_ExecuteForward(Forwards::OnWeatherChange, (cell)EWeather::Sleet, (cell)LightLevel);
}
