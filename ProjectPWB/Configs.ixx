export module Configs;

import ConsoleVar;


export console_variable_t cvar_throwingweaponvelocity{ "weaponphys_throwingweapon_velocity", "350.0" };	// 擲出武器的初始速度。單位：英吋/秒
//export console_variable_t cvar_grenadehitvelocity{ "weaponphys_grenade_hit_velocity", "400.0" };		// 手榴彈至少要達到多少速度方可造成鈍擊傷害？
//export console_variable_t cvar_grenadehitdamage{ "weaponphys_grenade_hit_damage", "5.0" };				// 手榴彈的鈍擊傷害
export console_variable_t cvar_gunshotenergyconv{ "weaponphys_gunshot_energy_convert", "20.0" };		// W模型被命中後有多少傷害被轉換成動能？
export console_variable_t cvar_friction{ "weaponphys_friction", "0.7" };								// W模型楊氏模量(自行Google檢索)
export console_variable_t cvar_gravity{ "weaponphys_gravity", "1.4" };									// W模型所受重力倍數
export console_variable_t cvar_velocitydecay{ "weaponphys_velocity_decay", "0.95" };					// W模型摩擦係數
