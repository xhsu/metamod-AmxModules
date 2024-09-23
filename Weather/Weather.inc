/*

Corresponding Module Version: 1.1.0.7512

Counter-Strike: Modern Warfare Dev Team
 - Programmer: Luna the Reborn

*/

enum EWeather
{
    W_Sunny = 1,
    W_Drizzle = 2,
    W_ThunderStorm = 3,
    W_Tempest = 4,
    W_Snow = 5,
    W_Sleet = 6,
    W_BlackFog = 7,
};

enum EReceiveW
{
    RW_Clear = 0,
    RW_Rain,
    RW_Snow,
};

/*
 * Purpose: Cast fog on the map.
 * @Argument r, g, b: byte color, ranging from 0-255.
 * @Argument flDensity: thickness of the fog, should be a extremely small decimal number, like 0.001
 * @Returning: NONE.
*/
native Weather_SetFog(r, g, b, Float:flDensity);

/*
 * Purpose: Cast a built-in weather on the map.
 * @Argument what: can be either clear, rain or snow.
 * @Returning: NONE.
*/
native Weather_SetReceiveW(EReceiveW:what);

/*
 * Purpose: Set a constant lighting level of the map.
 * @Argument cLightLevel: Ranging from 'a' to 'z', from dimmest to brightest.
 * @Returning: NONE.
*/
native Weather_SetLightLevel(cLightLevel);

/*
 * Purpose: Cast a module-managed weather on the map.
 * @Argument what: Check the corresponding  enum for more information.
 * @Argument cLightLevel: Ranging from 'a' to 'z', from dimmest to brightest. Set to 0 or lower to use the default lighting style.
 * @Returning: NONE.
*/
native Weather_SetWeather(EWeather:what, cLightLevel = 0);

/*
 * Event: lightning roars during a thunderstorm.
 * @Parameter cLightLevel: What lighting level will return after current lightning?
 * @Stage: Pre.
 * @Interception: Cannot be intercepted by all means.
 * @Returning: Discarded.
 *
 * Note: Undefined behavior will be invoked should the weather be altered in the forward.
*/
forward WeatherF_OnMinorThunder(cLightLevel);

/*
 * Event: lightning roars during a tempest.
 * @Parameter cLightLevel: What lighting level will return after current lightning?
 * @Stage: Pre.
 * @Interception: Cannot be intercepted by all means.
 * @Returning: Discarded.
 *
 * Note: Undefined behavior will be invoked should the weather be altered in the forward.
*/
forward WeatherF_OnMajorThunder(cLightLevel);

/*
 * Event: Weather changed.
 * @Parameter iNewWeather: What weather will be changed into?
 * @Parameter cLightLevel: What lighting level will be considered as the baseline?
 * @Stage: Post.
 * @Interception: Cannot be intercepted by all means.
 * @Returning: Discarded.
 *
 * Note: Stack overflow or infinite recursion will occur should the weather be altered again in the forward.
*/
forward WeatherF_OnWeatherChange(EWeather:iNewWeather, cLightLevel);
