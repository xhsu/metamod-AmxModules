/*

Corresponding Module Version: 1.1.0

Dev Team
 - Programmer: Luna the Reborn

*/

#include <amxmodx>
#include <fakemeta>

/*
* Purpose: Inject an entity class into the engine, and register a callback function of its Spawn()
* @param szClassName: Entity class name, same as the one typed into LINK_ENTITY_TO_CLASS() macro in HLSDK.
* @param szSpawnCallback: The local AMX callback function which will be invoked during DispatchSpawn() phase.
						The local AMX callback function must come with a signature the same as Ham_Spawn.
* @return int32_t: 0 if fail, other numbers if succeed.
* @except AMX_ERR_NATIVE: Throw if no such AMX callback is found.
*/
native MEE_RegisterEntitySpawn(const szClassName[], const szSpawnCallback[]);

/*
* Purpose: Inject an entity class into the engine, and register a callback function of its KeyValue()
* @param szClassName: Entity class name, same as the one typed into LINK_ENTITY_TO_CLASS() macro in HLSDK.
* @param szKvdCallback:	The local AMX callback function which will be invoked during DispatchKeyValue() phase.
						The local AMX callback function must come with a signature as follows:
						public callback(iEntity, const szKey[], const szValue[]) -> bool
* @return int32_t: 0 if fail, other numbers if succeed.
* @except AMX_ERR_NATIVE: Throw if no such AMX callback is found.
*/
native MEE_RegisterEntityKvd(const szClassName[], const szKvdCallback[]);

/*
* Purpose: Replace a managing class of one entity with another.
* @param szOriginalClassName: Entity class name, same as the one typed into LINK_ENTITY_TO_CLASS() macro in HLSDK.
* @param szDestClassName: Entity class name, same as the one typed into LINK_ENTITY_TO_CLASS() macro in HLSDK.
* @return bool: 0 on fail, 1 on success.
* @except AMX_ERR_NATIVE: Another replacement had been registered and clashed.
* @note: Be extremely careful with this one, as it might cause memory access errors everywhere.
A safe example will be to treat spawning points from DoDC, TFC maps as if they were the CSCZ one.
*/
native MEE_ReplaceLinkedClass(const szOriginalClassName[], const szDestClassName[]);

#pragma semicolon 1

#define PLUGIN		"Map Ent Extention Example"
#define VERSION		"1.1.0"
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
