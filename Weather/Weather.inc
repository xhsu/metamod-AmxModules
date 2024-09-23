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

*/
native Weather_SetFog(r, g, b, Float:flDensity);
native Weather_SetReceiveW(EReceiveW:what);
native Weather_SetLightLevel(cLightLevel);
native Weather_SetWeather(EWeather:what, cLightLevel = 0);
