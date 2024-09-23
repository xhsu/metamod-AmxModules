/*

Corresponding Module Version: 1.0.0.7511

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
