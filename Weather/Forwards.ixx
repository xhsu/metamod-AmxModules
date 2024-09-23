export module Forwards;

import std.compat;

import Plugin;

export namespace Forwards
{
	inline int32_t OnMinorThunder = 0;
	inline int32_t OnMajorThunder = 0;
	inline int32_t OnWeatherChange = 0;
}

// must be called in AMXX_PluginsLoaded()
export void DeployForwards() noexcept
{
	using namespace Forwards;

	OnMinorThunder = MF_RegisterForward("WeatherF_OnMinorThunder", ET_IGNORE, FP_CELL, FP_DONE);	// lightstyle
	OnMajorThunder = MF_RegisterForward("WeatherF_OnMajorThunder", ET_IGNORE, FP_CELL, FP_DONE);	// lightstyle
	OnWeatherChange = MF_RegisterForward("WeatherF_OnWeatherChange", ET_IGNORE, FP_CELL, FP_CELL, FP_DONE);	// new weather, lightlevel
}
