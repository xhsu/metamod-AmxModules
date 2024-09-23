import std;

import Plugin;
import Weather;

import util;


// DS's array: 0, 0.0005, 0.0009653931, 0.0023708497, 0.0028746934, 0.0029967637, 0.003405156, 0.0038615724, 0.008, 0.008999888
// native Weather_SetFog(r, g, b, Float:flDensity);
static cell Native_SetFog(AMX* amx, cell* params) noexcept
{
	UTIL_SetFog(params[1], params[2], params[3], std::bit_cast<float>(params[4]));

	return true;
}

// native Weather_SetReceiveW(EReceiveW:what);
static cell Native_SetReceiveW(AMX* amx, cell* params) noexcept
{
	UTIL_SetReceiveW((EReceiveW)params[1]);

	return true;
}

// native Weather_SetLightLevel(cLightLevel);
static cell Native_SetLightLevel(AMX* amx, cell* params) noexcept
{
	static std::array<char, 2> szLightLevel;
	char LightLevel = std::clamp<char>(params[1], 'a', 'z');

	szLightLevel[0] = LightLevel;
	g_engfuncs.pfnLightStyle(0, szLightLevel.data());

	return true;
}

// native Weather_SetWeather(EWeather:what, cLightLevel = 0);
static cell Native_SetWeather(AMX* amx, cell* params) noexcept
{
	std::span args{ &params[1], (size_t)(params[0] / sizeof(cell)) };

	char const LightLevel =
		params[2] > 0 ?
		std::clamp<char>(params[2], 'a', 'z')
		:
		(char)params[2];	// use default value if < 0.

	switch ((EWeather)params[1])
	{
	default:
	case EWeather::Sunny:
		if (LightLevel > 0)
			Sunny(LightLevel);
		else
			Sunny();
		break;

	case EWeather::Drizzle:
		if (LightLevel > 0)
			Drizzle(LightLevel);
		else
			Drizzle();
		break;

	case EWeather::ThunderStorm:
		if (LightLevel > 0)
			ThunderStorm(LightLevel);
		else
			ThunderStorm();
		break;

	case EWeather::Tempest:
		if (LightLevel > 0)
			Tempest(LightLevel);
		else
			Tempest();
		break;

	case EWeather::Snow:
		if (LightLevel > 0)
			Snow(LightLevel);
		else
			Snow();
		break;

	case EWeather::Sleet:
		if (LightLevel > 0)
			Sleet(LightLevel);
		else
			Sleet();
		break;

	case EWeather::BlackFog:
		if (LightLevel > 0)
			BlackFog(LightLevel);
		else
			BlackFog();
		break;
	}

	return true;
}


void DeployNatives() noexcept
{
	static constexpr AMX_NATIVE_INFO rgAmxNativeInfo[] =
	{
		{ "Weather_SetFog",			&Native_SetFog },
		{ "Weather_SetReceiveW",	&Native_SetReceiveW },
		{ "Weather_SetLightLevel",	&Native_SetLightLevel },
		{ "Weather_SetWeather",		&Native_SetWeather },

		{ nullptr, nullptr },
	};

	MF_AddNatives(rgAmxNativeInfo);
}
