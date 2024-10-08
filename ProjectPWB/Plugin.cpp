import std;
import hlsdk;

import FileSystem;
import Plugin;
import Task;
import Uranus;



// DllFunctions.cpp
extern void fw_GameInit_Post() noexcept;
extern auto fw_Spawn_Post(edict_t* pEdict) noexcept -> qboolean;
extern void fw_Touch_Post(edict_t* pentTouched, edict_t* pentOther) noexcept;
extern void fw_ServerActivate_Post(edict_t* pEdictList, int edictCount, int clientMax) noexcept;
extern void fw_ServerDeactivate_Post() noexcept;
extern META_RES fw_PM_Move(playermove_t* ppmove, qboolean server) noexcept;
extern void fw_PM_Move_Post(playermove_t* ppmove, qboolean server) noexcept;
extern META_RES fw_PlayerPostThink(edict_t*) noexcept;
extern void fw_PlayerPostThink_Post(edict_t* pEdict) noexcept;
extern qboolean fw_AddToFullPack_Post(entity_state_t* pState, int iEntIndex, edict_t* pEdict, edict_t* pClientSendTo, qboolean cl_lw, qboolean bIsPlayer, unsigned char* pSet) noexcept;
//

// EngineAPI.cpp
extern auto fw_PrecacheModel(const char* psz) noexcept -> int;
extern void fw_SetModel(edict_t* pEdict, const char* pszModel) noexcept;
extern auto fw_ModelIndex(const char* pszModel) noexcept -> int;
//


// Receive engine function table from engine.
// This appears to be the _first_ DLL routine called by the engine, so we do some setup operations here.
void __stdcall GiveFnptrsToDll(enginefuncs_t *pengfuncsFromEngine, globalvars_t *pGlobals) noexcept
{
	std::memcpy(&g_engfuncs, pengfuncsFromEngine, sizeof(enginefuncs_t));
	gpGlobals = pGlobals;
}

static int HookGameDLLExportedFn(DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion) noexcept
{
	static constexpr DLL_FUNCTIONS gFunctionTable =
	{
		.pfnGameInit	= nullptr,
		.pfnSpawn		= nullptr,
		.pfnThink		= nullptr,
		.pfnUse			= nullptr,
		.pfnTouch		= nullptr,
		.pfnBlocked		= nullptr,
		.pfnKeyValue	= nullptr,
		.pfnSave		= nullptr,
		.pfnRestore		= nullptr,
		.pfnSetAbsBox	= nullptr,

		.pfnSaveWriteFields	= nullptr,
		.pfnSaveReadFields	= nullptr,

		.pfnSaveGlobalState		= nullptr,
		.pfnRestoreGlobalState	= nullptr,
		.pfnResetGlobalState	= nullptr,

		.pfnClientConnect		= nullptr,
		.pfnClientDisconnect	= nullptr,
		.pfnClientKill			= nullptr,
		.pfnClientPutInServer	= nullptr,
		.pfnClientCommand		= nullptr,
		.pfnClientUserInfoChanged	= nullptr,
		.pfnServerActivate		= nullptr,
		.pfnServerDeactivate	= nullptr,

		.pfnPlayerPreThink	= nullptr,
		.pfnPlayerPostThink	= [](edict_t* pPlayer) noexcept { gpMetaGlobals->mres = fw_PlayerPostThink(pPlayer); },

		.pfnStartFrame		= nullptr,
		.pfnParmsNewLevel	= nullptr,
		.pfnParmsChangeLevel= nullptr,

		.pfnGetGameDescription	= nullptr,
		.pfnPlayerCustomization	= nullptr,

		.pfnSpectatorConnect	= nullptr,
		.pfnSpectatorDisconnect	= nullptr,
		.pfnSpectatorThink		= nullptr,

		.pfnSys_Error	= nullptr,

		.pfnPM_Move				= [](playermove_t* ppmove, qboolean server) noexcept { gpMetaGlobals->mres = fw_PM_Move(ppmove, server); },
		.pfnPM_Init				= nullptr,
		.pfnPM_FindTextureType	= nullptr,

		.pfnSetupVisibility	= nullptr,
		.pfnUpdateClientData= nullptr,
		.pfnAddToFullPack	= nullptr,
		.pfnCreateBaseline	= nullptr,
		.pfnRegisterEncoders= nullptr,
		.pfnGetWeaponData	= nullptr,
		.pfnCmdStart		= nullptr,
		.pfnCmdEnd			= nullptr,
		.pfnConnectionlessPacket	= nullptr,
		.pfnGetHullBounds			= nullptr,
		.pfnCreateInstancedBaselines= nullptr,
		.pfnInconsistentFile		= nullptr,
		.pfnAllowLagCompensation	= nullptr,
	};

	if (!pFunctionTable) [[unlikely]]
	{
		gpMetaUtilFuncs->pfnLogError(PLID, "Function 'HookGameDLLExportedFn' called with null 'pFunctionTable' parameter.");
		return false;
	}
	else if (*interfaceVersion != INTERFACE_VERSION) [[unlikely]]
	{
		gpMetaUtilFuncs->pfnLogError(PLID, "Function 'HookGameDLLExportedFn' called with version mismatch. Expected: %d, receiving: %d.", INTERFACE_VERSION, *interfaceVersion);

		//! Tell metamod what version we had, so it can figure out who is out of date.
		*interfaceVersion = INTERFACE_VERSION;
		return false;
	}

	std::memcpy(pFunctionTable, &gFunctionTable, sizeof(DLL_FUNCTIONS));
	return true;
}

static int HookGameDLLExportedFn_Post(DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion) noexcept
{
	static constexpr DLL_FUNCTIONS gFunctionTable =
	{
		.pfnGameInit	= &fw_GameInit_Post,
		.pfnSpawn		= &fw_Spawn_Post,
		.pfnThink		= nullptr,
		.pfnUse			= nullptr,
		.pfnTouch		= &fw_Touch_Post,
		.pfnBlocked		= nullptr,
		.pfnKeyValue	= nullptr,
		.pfnSave		= nullptr,
		.pfnRestore		= nullptr,
		.pfnSetAbsBox	= nullptr,

		.pfnSaveWriteFields	= nullptr,
		.pfnSaveReadFields	= nullptr,

		.pfnSaveGlobalState		= nullptr,
		.pfnRestoreGlobalState	= nullptr,
		.pfnResetGlobalState	= nullptr,

		.pfnClientConnect		= nullptr,
		.pfnClientDisconnect	= nullptr,
		.pfnClientKill			= nullptr,
		.pfnClientPutInServer	= nullptr,
		.pfnClientCommand		= nullptr,
		.pfnClientUserInfoChanged	= nullptr,
		.pfnServerActivate		= &fw_ServerActivate_Post,
		.pfnServerDeactivate	= &fw_ServerDeactivate_Post,

		.pfnPlayerPreThink	= nullptr,
		.pfnPlayerPostThink	= &fw_PlayerPostThink_Post,

		.pfnStartFrame		= &TaskScheduler::Think,
		.pfnParmsNewLevel	= nullptr,
		.pfnParmsChangeLevel= nullptr,

		.pfnGetGameDescription	= nullptr,
		.pfnPlayerCustomization	= nullptr,

		.pfnSpectatorConnect	= nullptr,
		.pfnSpectatorDisconnect	= nullptr,
		.pfnSpectatorThink		= nullptr,

		.pfnSys_Error	= nullptr,

		.pfnPM_Move				= &fw_PM_Move_Post,
		.pfnPM_Init				= nullptr,
		.pfnPM_FindTextureType	= nullptr,

		.pfnSetupVisibility	= nullptr,
		.pfnUpdateClientData= nullptr,
		.pfnAddToFullPack	= &fw_AddToFullPack_Post,
		.pfnCreateBaseline	= nullptr,
		.pfnRegisterEncoders= nullptr,
		.pfnGetWeaponData	= nullptr,
		.pfnCmdStart		= nullptr,
		.pfnCmdEnd			= nullptr,
		.pfnConnectionlessPacket	= nullptr,
		.pfnGetHullBounds			= nullptr,
		.pfnCreateInstancedBaselines= nullptr,
		.pfnInconsistentFile		= nullptr,
		.pfnAllowLagCompensation	= nullptr,
	};

	if (!pFunctionTable) [[unlikely]]
	{
		gpMetaUtilFuncs->pfnLogError(PLID, "Function 'HookGameDLLExportedFn_Post' called with null 'pFunctionTable' parameter.");
		return false;
	}
	else if (*interfaceVersion != INTERFACE_VERSION) [[unlikely]]
	{
		gpMetaUtilFuncs->pfnLogError(PLID, "Function 'HookGameDLLExportedFn_Post' called with version mismatch. Expected: %d, receiving: %d.", INTERFACE_VERSION, *interfaceVersion);

		//! Tell metamod what version we had, so it can figure out who is out of date.
		*interfaceVersion = INTERFACE_VERSION;
		return false;
	}

	std::memcpy(pFunctionTable, &gFunctionTable, sizeof(DLL_FUNCTIONS));
	return true;
}

static int HookEngineAPI(enginefuncs_t *pengfuncsFromEngine, int *interfaceVersion) noexcept
{
	static constexpr enginefuncs_t meta_engfuncs = 
	{
		.pfnPrecacheModel	= &fw_PrecacheModel,
		.pfnPrecacheSound	= nullptr,
		.pfnSetModel		= &fw_SetModel,
		.pfnModelIndex		= &fw_ModelIndex,
		.pfnModelFrames		= nullptr,

		.pfnSetSize			= nullptr,
		.pfnChangeLevel		= nullptr,
		.pfnGetSpawnParms	= nullptr,
		.pfnSaveSpawnParms	= nullptr,

		.pfnVecToYaw		= nullptr,
		.pfnVecToAngles		= nullptr,
		.pfnMoveToOrigin	= nullptr,
		.pfnChangeYaw		= nullptr,
		.pfnChangePitch		= nullptr,

		.pfnFindEntityByString	= nullptr,
		.pfnGetEntityIllum		= nullptr,
		.pfnFindEntityInSphere	= nullptr,
		.pfnFindClientInPVS		= nullptr,
		.pfnEntitiesInPVS		= nullptr,

		.pfnMakeVectors		= nullptr,
		.pfnAngleVectors	= nullptr,

		.pfnCreateEntity		= nullptr,
		.pfnRemoveEntity		= nullptr,
		.pfnCreateNamedEntity	= nullptr,

		.pfnMakeStatic		= nullptr,
		.pfnEntIsOnFloor	= nullptr,
		.pfnDropToFloor		= nullptr,

		.pfnWalkMove		= nullptr,
		.pfnSetOrigin		= nullptr,

		.pfnEmitSound		= nullptr,
		.pfnEmitAmbientSound= nullptr,

		.pfnTraceLine		= nullptr,
		.pfnTraceToss		= nullptr,
		.pfnTraceMonsterHull= nullptr,
		.pfnTraceHull		= nullptr,
		.pfnTraceModel		= nullptr,
		.pfnTraceTexture	= nullptr,
		.pfnTraceSphere		= nullptr,
		.pfnGetAimVector	= nullptr,

		.pfnServerCommand	= nullptr,
		.pfnServerExecute	= nullptr,
		.pfnClientCommand	= nullptr,

		.pfnParticleEffect	= nullptr,
		.pfnLightStyle		= nullptr,
		.pfnDecalIndex		= nullptr,
		.pfnPointContents	= nullptr,

		.pfnMessageBegin	= nullptr,
		.pfnMessageEnd		= nullptr,

		.pfnWriteByte	= nullptr,
		.pfnWriteChar	= nullptr,
		.pfnWriteShort	= nullptr,
		.pfnWriteLong	= nullptr,
		.pfnWriteAngle	= nullptr,
		.pfnWriteCoord	= nullptr,
		.pfnWriteString	= nullptr,
		.pfnWriteEntity	= nullptr,

		.pfnCVarRegister	= nullptr,
		.pfnCVarGetFloat	= nullptr,
		.pfnCVarGetString	= nullptr,
		.pfnCVarSetFloat	= nullptr,
		.pfnCVarSetString	= nullptr,

		.pfnAlertMessage	= nullptr,
		.pfnEngineFprintf	= nullptr,

		.pfnPvAllocEntPrivateData	= nullptr,
		.pfnPvEntPrivateData		= nullptr,
		.pfnFreeEntPrivateData		= nullptr,

		.pfnSzFromIndex		= nullptr,
		.pfnAllocString		= nullptr,

		.pfnGetVarsOfEnt		= nullptr,
		.pfnPEntityOfEntOffset	= nullptr,
		.pfnEntOffsetOfPEntity	= nullptr,
		.pfnIndexOfEdict		= nullptr,
		.pfnPEntityOfEntIndex	= nullptr,
		.pfnFindEntityByVars	= nullptr,
		.pfnGetModelPtr			= nullptr,

		.pfnRegUserMsg		= nullptr,

		.pfnAnimationAutomove	= nullptr,
		.pfnGetBonePosition		= nullptr,

		.pfnFunctionFromName	= nullptr,
		.pfnNameForFunction		= nullptr,

		.pfnClientPrintf	= nullptr,
		.pfnServerPrint		= nullptr,

		.pfnCmd_Args	= nullptr,
		.pfnCmd_Argv	= nullptr,
		.pfnCmd_Argc	= nullptr,

		.pfnGetAttachment	= nullptr,

		.pfnCRC32_Init			= nullptr,
		.pfnCRC32_ProcessBuffer	= nullptr,
		.pfnCRC32_ProcessByte	= nullptr,
		.pfnCRC32_Final			= nullptr,

		.pfnRandomLong	= nullptr,
		.pfnRandomFloat	= nullptr,

		.pfnSetView			= nullptr,
		.pfnTime			= nullptr,
		.pfnCrosshairAngle	= nullptr,

		.pfnLoadFileForMe	= nullptr,
		.pfnFreeFile		= nullptr,

		.pfnEndSection		= nullptr,
		.pfnCompareFileTime	= nullptr,
		.pfnGetGameDir		= nullptr,
		.pfnCvar_RegisterVariable	= nullptr,
		.pfnFadeClientVolume	= nullptr,
		.pfnSetClientMaxspeed	= nullptr,
		.pfnCreateFakeClient	= nullptr,
		.pfnRunPlayerMove		= nullptr,
		.pfnNumberOfEntities	= nullptr,

		.pfnGetInfoKeyBuffer	= nullptr,
		.pfnInfoKeyValue		= nullptr,
		.pfnSetKeyValue			= nullptr,
		.pfnSetClientKeyValue	= nullptr,

		.pfnIsMapValid		= nullptr,
		.pfnStaticDecal		= nullptr,
		.pfnPrecacheGeneric	= nullptr,
		.pfnGetPlayerUserId	= nullptr,
		.pfnBuildSoundMsg	= nullptr,
		.pfnIsDedicatedServer	= nullptr,
		.pfnCVarGetPointer	= nullptr,
		.pfnGetPlayerWONId	= nullptr,

		.pfnInfo_RemoveKey		= nullptr,
		.pfnGetPhysicsKeyValue	= nullptr,
		.pfnSetPhysicsKeyValue	= nullptr,
		.pfnGetPhysicsInfoString= nullptr,
		.pfnPrecacheEvent		= nullptr,
		.pfnPlaybackEvent		= nullptr,

		.pfnSetFatPVS		= nullptr,
		.pfnSetFatPAS		= nullptr,

		.pfnCheckVisibility	= nullptr,

		.pfnDeltaSetField			= nullptr,
		.pfnDeltaUnsetField			= nullptr,
		.pfnDeltaAddEncoder			= nullptr,
		.pfnGetCurrentPlayer		= nullptr,
		.pfnCanSkipPlayer			= nullptr,
		.pfnDeltaFindField			= nullptr,
		.pfnDeltaSetFieldByIndex	= nullptr,
		.pfnDeltaUnsetFieldByIndex	= nullptr,

		.pfnSetGroupMask			= nullptr,

		.pfnCreateInstancedBaseline	= nullptr,
		.pfnCvar_DirectSet			= nullptr,

		.pfnForceUnmodified			= nullptr,

		.pfnGetPlayerStats			= nullptr,

		.pfnAddServerCommand		= nullptr,

		// Added in SDK 2.2:
		.pfnVoice_GetClientListening	= nullptr,
		.pfnVoice_SetClientListening	= nullptr,

		// Added for HL 1109 (no SDK update):
		.pfnGetPlayerAuthId	= nullptr,

		// Added 2003/11/10 (no SDK update):
		.pfnSequenceGet							= nullptr,
		.pfnSequencePickSentence				= nullptr,
		.pfnGetFileSize							= nullptr,
		.pfnGetApproxWavePlayLen				= nullptr,
		.pfnIsCareerMatch						= nullptr,
		.pfnGetLocalizedStringLength			= nullptr,
		.pfnRegisterTutorMessageShown			= nullptr,
		.pfnGetTimesTutorMessageShown			= nullptr,
		.pfnProcessTutorMessageDecayBuffer		= nullptr,
		.pfnConstructTutorMessageDecayBuffer	= nullptr,
		.pfnResetTutorMessageDecayData			= nullptr,

		// Added Added 2005-08-11 (no SDK update)
		.pfnQueryClientCvarValue	= nullptr,
		// Added Added 2005-11-22 (no SDK update)
		.pfnQueryClientCvarValue2	= nullptr,
		// Added 2009-06-17 (no SDK update)
		.pfnEngCheckParm			= nullptr,
	};

	if (!pengfuncsFromEngine) [[unlikely]]
	{
		gpMetaUtilFuncs->pfnLogError(PLID, "Function 'HookEngineAPI' called with null 'pengfuncsFromEngine' parameter.");
		return false;
	}
	else if (*interfaceVersion != ENGINE_INTERFACE_VERSION) [[unlikely]]
	{
		gpMetaUtilFuncs->pfnLogError(PLID, "Function 'HookEngineAPI' called with version mismatch. Expected: %d, receiving: %d.", ENGINE_INTERFACE_VERSION, *interfaceVersion);

		// Tell metamod what version we had, so it can figure out who is out of date.
		*interfaceVersion = ENGINE_INTERFACE_VERSION;
		return false;
	}

	std::memcpy(pengfuncsFromEngine, &meta_engfuncs, sizeof(enginefuncs_t));
	return true;
}

// Must provide at least one of these..
inline constexpr META_FUNCTIONS gMetaFunctionTable =
{
	.pfnGetEntityAPI			= nullptr,						// HL SDK; called before game DLL
	.pfnGetEntityAPI_Post		= nullptr,						// META; called after game DLL
	.pfnGetEntityAPI2			= &HookGameDLLExportedFn,		// HL SDK2; called before game DLL
	.pfnGetEntityAPI2_Post		= &HookGameDLLExportedFn_Post,	// META; called after game DLL
	.pfnGetNewDLLFunctions		= nullptr,						// HL SDK2; called before game DLL
	.pfnGetNewDLLFunctions_Post	= nullptr,						// META; called after game DLL
	.pfnGetEngineFunctions		= &HookEngineAPI,				// META; called before HL engine
	.pfnGetEngineFunctions_Post	= nullptr,						// META; called after HL engine
};

// Metamod requesting info about this plugin:
//  ifvers			(given) interface_version metamod is using
//  pPlugInfo		(requested) struct with info about plugin
//  pMetaUtilFuncs	(given) table of utility functions provided by metamod
int Meta_Query(const char *pszInterfaceVersion, plugin_info_t const **pPlugInfo, mutil_funcs_t *pMetaUtilFuncs) noexcept
{
	*pPlugInfo = PLID;
	gpMetaUtilFuncs = pMetaUtilFuncs;

	return true;
}
static_assert(std::same_as<decltype(&Meta_Query), META_QUERY_FN>);

// Metamod attaching plugin to the server.
//  now				(given) current phase, ie during map, during changelevel, or at startup
//  pFunctionTable	(requested) table of function tables this plugin catches
//  pMGlobals		(given) global vars from metamod
//  pGamedllFuncs	(given) copy of function tables from game dll
int Meta_Attach(PLUG_LOADTIME iCurrentPhase, META_FUNCTIONS *pFunctionTable, meta_globals_t *pMGlobals, gamedll_funcs_t *pGamedllFuncs) noexcept
{
	if (!pMGlobals) [[unlikely]]
	{
		gpMetaUtilFuncs->pfnLogError(PLID, "Function 'Meta_Attach' called with null 'pMGlobals' parameter.");
		return false;
	}

	gpMetaGlobals = pMGlobals;

	if (!pFunctionTable) [[unlikely]]
	{
		gpMetaUtilFuncs->pfnLogError(PLID, "Function 'Meta_Attach' called with null 'pFunctionTable' parameter.");
		return false;
	}

	std::memcpy(pFunctionTable, &gMetaFunctionTable, sizeof(META_FUNCTIONS));
	gpGamedllFuncs = pGamedllFuncs;
	return true;
}
static_assert(std::same_as<decltype(&Meta_Attach), META_ATTACH_FN>);

// Metamod detaching plugin from the server.
// now		(given) current phase, ie during map, etc
// reason	(given) why detaching (refresh, console unload, forced unload, etc)
int Meta_Detach(PLUG_LOADTIME iCurrentPhase, PL_UNLOAD_REASON iReason) noexcept
{
	return true;
}
static_assert(std::same_as<decltype(&Meta_Detach), META_DETACH_FN>);

/************* AMXX Stuff *************/
// *** Exports ***
int AMXX_Query(int* interfaceVersion, amxx_module_info_t* moduleInfo) noexcept
{
	// check parameters
	if (!interfaceVersion || !moduleInfo)
		return AMXX_PARAM;

	// check interface version
	if (*interfaceVersion != AMXX_INTERFACE_VERSION)
	{
		// Tell amxx core our interface version
		*interfaceVersion = AMXX_INTERFACE_VERSION;
		return AMXX_IFVERS;
	}

	// copy module info
	std::memcpy(moduleInfo, &g_ModuleInfo, sizeof(amxx_module_info_t));

	// Everything ok :)
	return AMXX_OK;
}

// request function
#define REQFUNC(name, fptr, type) if ((fptr = (type)reqFnptrFunc(name)) == 0) return AMXX_FUNC_NOT_PRESENT
// request optional function
#define REQFUNC_OPT(name, fptr, type) fptr = (type)reqFnptrFunc(name)

int AMXX_CheckGame(const char* game) noexcept
{
	return AMXX_GAME_OK;
}

int AMXX_Attach(PFN_REQ_FNPTR reqFnptrFunc) noexcept
{
	// Check pointer
	if (!reqFnptrFunc)
		return AMXX_PARAM;

	MF_RequestFunction = reqFnptrFunc;

	// Req all known functions
	// Misc
	REQFUNC("BuildPathname", MF_BuildPathname, PFN_BUILD_PATHNAME);
	REQFUNC("BuildPathnameR", MF_BuildPathnameR, PFN_BUILD_PATHNAME_R);
	REQFUNC("PrintSrvConsole", MF_PrintSrvConsole, PFN_PRINT_SRVCONSOLE);
	REQFUNC("GetModname", MF_GetModname, PFN_GET_MODNAME);
	REQFUNC("Log", MF_Log, PFN_LOG);
	REQFUNC("LogError", MF_LogError, PFN_LOG_ERROR);
	REQFUNC("MergeDefinitionFile", MF_MergeDefinitionFile, PFN_MERGEDEFINITION_FILE);
	REQFUNC("Format", MF_Format, PFN_FORMAT);
	REQFUNC("RegisterFunction", MF_RegisterFunction, PFN_REGISTERFUNCTION);
	REQFUNC("RegisterFunctionEx", MF_RegisterFunctionEx, PFN_REGISTERFUNCTIONEX);

	// Amx scripts
	REQFUNC("GetAmxScript", MF_GetScriptAmx, PFN_GET_AMXSCRIPT);
	REQFUNC("FindAmxScriptByAmx", MF_FindScriptByAmx, PFN_FIND_AMXSCRIPT_BYAMX);
	REQFUNC("FindAmxScriptByName", MF_FindScriptByName, PFN_FIND_AMXSCRIPT_BYNAME);
	REQFUNC("LoadAmxScript", MF_LoadAmxScript, PFN_LOAD_AMXSCRIPT);
	REQFUNC("UnloadAmxScript", MF_UnloadAmxScript, PFN_UNLOAD_AMXSCRIPT);
	REQFUNC("GetAmxScriptName", MF_GetScriptName, PFN_GET_AMXSCRIPTNAME);

	// String / mem in amx scripts support
	REQFUNC("SetAmxString", MF_SetAmxString, PFN_SET_AMXSTRING);
	REQFUNC("GetAmxString", MF_GetAmxString, PFN_GET_AMXSTRING);
	REQFUNC("GetAmxStringLen", MF_GetAmxStringLen, PFN_GET_AMXSTRINGLEN);
	REQFUNC("FormatAmxString", MF_FormatAmxString, PFN_FORMAT_AMXSTRING);
	REQFUNC("CopyAmxMemory", MF_CopyAmxMemory, PFN_COPY_AMXMEMORY);
	REQFUNC("GetAmxAddr", MF_GetAmxAddr, PFN_GET_AMXADDR);

	REQFUNC("amx_Exec", MF_AmxExec, PFN_AMX_EXEC);
	REQFUNC("amx_Execv", MF_AmxExecv, PFN_AMX_EXECV);
	REQFUNC("amx_FindPublic", MF_AmxFindPublic, PFN_AMX_FINDPUBLIC);
	REQFUNC("amx_Allot", MF_AmxAllot, PFN_AMX_ALLOT);
	REQFUNC("amx_FindNative", MF_AmxFindNative, PFN_AMX_FINDNATIVE);

	// Natives / Forwards
	REQFUNC("AddNatives", MF_AddNatives, PFN_ADD_NATIVES);
	REQFUNC("AddNewNatives", MF_AddNewNatives, PFN_ADD_NEW_NATIVES);
	REQFUNC("RaiseAmxError", MF_RaiseAmxError, PFN_RAISE_AMXERROR);
	REQFUNC("RegisterForward", MF_RegisterForward, PFN_REGISTER_FORWARD);
	REQFUNC("RegisterSPForward", MF_RegisterSPForward, PFN_REGISTER_SPFORWARD);
	REQFUNC("RegisterSPForwardByName", MF_RegisterSPForwardByName, PFN_REGISTER_SPFORWARD_BYNAME);
	REQFUNC("UnregisterSPForward", MF_UnregisterSPForward, PFN_UNREGISTER_SPFORWARD);
	REQFUNC("ExecuteForward", MF_ExecuteForward, PFN_EXECUTE_FORWARD);
	REQFUNC("PrepareCellArray", MF_PrepareCellArray, PFN_PREPARE_CELLARRAY);
	REQFUNC("PrepareCharArray", MF_PrepareCharArray, PFN_PREPARE_CHARARRAY);
	REQFUNC("PrepareCellArrayA", MF_PrepareCellArrayA, PFN_PREPARE_CELLARRAY_A);
	REQFUNC("PrepareCharArrayA", MF_PrepareCharArrayA, PFN_PREPARE_CHARARRAY_A);
	// Player
	REQFUNC("IsPlayerValid", MF_IsPlayerValid, PFN_IS_PLAYER_VALID);
	REQFUNC("GetPlayerName", MF_GetPlayerName, PFN_GET_PLAYER_NAME);
	REQFUNC("GetPlayerIP", MF_GetPlayerIP, PFN_GET_PLAYER_IP);
	REQFUNC("IsPlayerInGame", MF_IsPlayerIngame, PFN_IS_PLAYER_INGAME);
	REQFUNC("IsPlayerBot", MF_IsPlayerBot, PFN_IS_PLAYER_BOT);
	REQFUNC("IsPlayerAuthorized", MF_IsPlayerAuthorized, PFN_IS_PLAYER_AUTHORIZED);
	REQFUNC("GetPlayerTime", MF_GetPlayerTime, PFN_GET_PLAYER_TIME);
	REQFUNC("GetPlayerPlayTime", MF_GetPlayerPlayTime, PFN_GET_PLAYER_PLAYTIME);
	REQFUNC("GetPlayerCurweapon", MF_GetPlayerCurweapon, PFN_GET_PLAYER_CURWEAPON);
	REQFUNC("GetPlayerTeamID", MF_GetPlayerTeamID, PFN_GET_PLAYER_TEAMID);
	REQFUNC("GetPlayerTeam", MF_GetPlayerTeam, PFN_GET_PLAYER_TEAM);
	REQFUNC("GetPlayerDeaths", MF_GetPlayerDeaths, PFN_GET_PLAYER_DEATHS);
	REQFUNC("GetPlayerMenu", MF_GetPlayerMenu, PFN_GET_PLAYER_MENU);
	REQFUNC("GetPlayerKeys", MF_GetPlayerKeys, PFN_GET_PLAYER_KEYS);
	REQFUNC("IsPlayerAlive", MF_IsPlayerAlive, PFN_IS_PLAYER_ALIVE);
	REQFUNC("GetPlayerFrags", MF_GetPlayerFrags, PFN_GET_PLAYER_FRAGS);
	REQFUNC("IsPlayerConnecting", MF_IsPlayerConnecting, PFN_IS_PLAYER_CONNECTING);
	REQFUNC("IsPlayerHLTV", MF_IsPlayerHLTV, PFN_IS_PLAYER_HLTV);
	REQFUNC("GetPlayerArmor", MF_GetPlayerArmor, PFN_GET_PLAYER_ARMOR);
	REQFUNC("GetPlayerHealth", MF_GetPlayerHealth, PFN_GET_PLAYER_HEALTH);
	REQFUNC("GetPlayerFlags", MF_GetPlayerFlags, PFN_GETPLAYERFLAGS);
	REQFUNC("GetPlayerEdict", MF_GetPlayerEdict, PFN_GET_PLAYER_EDICT);
	REQFUNC("amx_Push", MF_AmxPush, PFN_AMX_PUSH);
	REQFUNC("SetPlayerTeamInfo", MF_SetPlayerTeamInfo, PFN_SET_TEAM_INFO);
	REQFUNC("PlayerPropAddr", MF_PlayerPropAddr, PFN_PLAYER_PROP_ADDR);
	REQFUNC("RegAuthFunc", MF_RegAuthFunc, PFN_REG_AUTH_FUNC);
	REQFUNC("UnregAuthFunc", MF_UnregAuthFunc, PFN_UNREG_AUTH_FUNC);

	//Added in 1.75 
	REQFUNC("FindLibrary", MF_FindLibrary, PFN_FINDLIBRARY);
	REQFUNC("AddLibraries", MF_AddLibraries, PFN_ADDLIBRARIES);
	REQFUNC("RemoveLibraries", MF_RemoveLibraries, PFN_REMOVELIBRARIES);
	REQFUNC("OverrideNatives", MF_OverrideNatives, PFN_OVERRIDENATIVES);
	REQFUNC("GetLocalInfo", MF_GetLocalInfo, PFN_GETLOCALINFO);
	REQFUNC("AmxReregister", MF_AmxReRegister, PFN_AMX_REREGISTER);

	REQFUNC("MessageBlock", MF_MessageBlock, PFN_MESSAGE_BLOCK);

	REQFUNC("CellToReal", MF_CellToReal, PFN_CELL_TO_REAL);
	REQFUNC("RealToCell", MF_RealToCell, PFN_REAL_TO_CELL);

	//DeployNatives();
	return AMXX_OK;
}

int AMXX_Detach() noexcept
{
	return AMXX_OK;
}

int AMXX_PluginsLoaded() noexcept
{
	//DeployForwards();
	return AMXX_OK;
}

void AMXX_PluginsUnloaded() noexcept
{
}

void AMXX_PluginsUnloading() noexcept
{
}
