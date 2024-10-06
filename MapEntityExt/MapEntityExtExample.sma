/*

Corresponding Module Version: 1.0.0

Dev Team
 - Programmer: Luna the Reborn

*/

#include <amxmodx>
#include <fakemeta>

native MEE_RegisterEntitySpawn(const szClassName[], const szSpawnCallback[]);
native MEE_RegisterEntityKvd(const szClassName[], const szKvdCallback[]);

#pragma semicolon 1

#define PLUGIN		"Map Ent Extention Example"
#define VERSION		"1.0.0"
#define AUTHOR		"xhsu"

public plugin_init()
{
	register_plugin(PLUGIN, VERSION, AUTHOR);
}

public plugin_precache()
{
	// Must register in precache phase, or it's too late.
	// Remember: precache() is ealier than init().
	
	MEE_RegisterEntitySpawn("env_spotlight", "env_spotlight_spawn");
	MEE_RegisterEntityKvd("env_spotlight", "env_spotlight_kv");
}

public env_spotlight_spawn(iEntity)
{
	// Returning value discarded.
}

public env_spotlight_kv(iEntity, const szKey[], const szValue[])
{
	new szClassName[32];
	pev(iEntity, pev_classname, szClassName, charsmax(szClassName));

	new szText[128];
	formatex(szText, charsmax(szText), "[MEE] %s: {^"%s^": ^"%s^"}", szClassName, szKey, szValue);
	server_print(szText);

	// Returning true to tell engine it's been handled.
	return true;
}
