#include "amxxmodule.h"
#include "engine.h"
#include "hook.h"

DWORD g_dwEngineBase = 0;
DWORD g_dwEngineSize = 0;
DWORD g_dwEngineBuildnum = 0;

hook_funcs_t gHookFuncs;

model_t **sv_worldmodel;

void Sys_Error(const char *fmt, ...)
{
	va_list argptr;
	char msg[1024];

	va_start(argptr, fmt);
	_vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	MessageBox(NULL, msg, "Error", MB_ICONERROR);
	exit(0);
}

#define SIG_NOT_FOUND(name) Sys_Error("Could not find: %s\nEngine buildnum£º%d", name, g_dwEngineBuildnum);

void Engine_InstallHook(void)
{
	DWORD addr;
	HMODULE hEngine;
	int swds = 1;
	
	hEngine = GetModuleHandle("swds.dll");
	if(!hEngine || hEngine == INVALID_HANDLE_VALUE)
	{
		swds = 0;
		hEngine = GetModuleHandle("hw.dll");
	}
	if(!hEngine || hEngine == INVALID_HANDLE_VALUE)
	{
		g_dwEngineBase = 0x1D01000;
		g_dwEngineSize = 0x1000000;
	}
	else
	{
		g_dwEngineBase = MH_GetModuleBase(hEngine);
		g_dwEngineSize = MH_GetModuleSize(hEngine);
	}

#define BUILD_NUMBER_SIG "\xA1\x2A\x2A\x2A\x2A\x83\xEC\x08\x2A\x33\x2A\x85\xC0"
#define BUILD_NUMBER_SIG_NEW "\x55\x8B\xEC\x83\xEC\x08\xA1\x2A\x2A\x2A\x2A\x56\x33\xF6\x85\xC0\x0F\x85\x2A\x2A\x2A\x2A\x53\x33\xDB\x8B\x04\x9D"

	gHookFuncs.buildnum = (int (*)(void))MH_SIGFind(g_dwEngineBase, g_dwEngineSize, BUILD_NUMBER_SIG, sizeof(BUILD_NUMBER_SIG)-1);
	if(!gHookFuncs.buildnum)
		gHookFuncs.buildnum = (int (*)(void))MH_SIGFind(g_dwEngineBase, g_dwEngineSize, BUILD_NUMBER_SIG_NEW, sizeof(BUILD_NUMBER_SIG_NEW)-1);
	if(!gHookFuncs.buildnum)
		SIG_NOT_FOUND("buildnum");

	g_dwEngineBuildnum = gHookFuncs.buildnum();

#define ED_PARSEEDICT_SIG "\xA1\x2A\x2A\x2A\x2A\x81\xEC\x24\x03\x00\x00\x53\x55\x8B\xAC\x24\x34\x03\x00\x00\x33\xDB\x56\x3B\xE8\x57"
#define ED_PARSEEDICT_SIG_NEW "\x55\x8B\xEC\x81\xEC\x10\x01\x00\x00\xA1\x2A\x2A\x2A\x2A\x56\x8B\x75\x0C\x57\x3B\xF0\x74\x16"

// LUNA: \x55\x8B\xEC\x81\xEC\x2A\x2A\x2A\x2A\xA1\x2A\x2A\x2A\x2A\x33\xC5\x89\x45\xFC\x53\x8B\x5D\x08\x56 ANNIV

	gHookFuncs.ED_ParseEdict = (char *(*)(char *, edict_t *))MH_SIGFind(g_dwEngineBase, g_dwEngineSize, ED_PARSEEDICT_SIG, sizeof(ED_PARSEEDICT_SIG)-1);
	if(!gHookFuncs.ED_ParseEdict)
		gHookFuncs.ED_ParseEdict = (char *(*)(char *, edict_t *))MH_SIGFind(g_dwEngineBase, g_dwEngineSize, ED_PARSEEDICT_SIG_NEW, sizeof(ED_PARSEEDICT_SIG_NEW)-1);
	if(!gHookFuncs.ED_ParseEdict)
		SIG_NOT_FOUND("ED_ParseEdict");

//E8 2A 2A 2A 2A                                      call    GetEntityInit
//83 C4 04                                            add     esp, 4
//85 C0                                               test    eax, eax
#define GETENTITYINIT_SIG "\xE8\x2A\x2A\x2A\x2A\x83\xC4\x04\x85\xC0"

//E8 D9 E7 03 00                                      call    GetEntityInit
//83 C4 10                                            add     esp, 10h
//3B C3                                               cmp     eax, ebx
#define GETENTITYINIT_SIG2 "\xE8\x2A\x2A\x2A\x2A\x83\xC4\x10\x3B\xC3"

	addr = MH_SIGFind((DWORD)gHookFuncs.ED_ParseEdict, 0x100, GETENTITYINIT_SIG, sizeof(GETENTITYINIT_SIG)-1);
	if(!addr)
		addr = MH_SIGFind((DWORD)gHookFuncs.ED_ParseEdict, 0x100, GETENTITYINIT_SIG2, sizeof(GETENTITYINIT_SIG2)-1);
	if(!addr)
		SIG_NOT_FOUND("GetEntityInit");
	gHookFuncs.GetEntityInit = (ENTITYINIT (*)(char *))GetCallAddress(addr);

	//SV_AddToFatPVS
#define SV_ADDTOFATPVS_SIG "\xE8\x2A\x2A\x2A\x2A\x83\xC4\x14"
	DWORD dwSV_AddToFatPVS = MH_SIGFind((DWORD)g_engfuncs.pfnSetFatPVS, 0x50, SV_ADDTOFATPVS_SIG, sizeof(SV_ADDTOFATPVS_SIG)-1);
	if(!dwSV_AddToFatPVS)
	{
		SIG_NOT_FOUND("SV_AddToFatPVS");
		return;
	}
	gHookFuncs.SV_AddToFatPVS = (void (*)(float *, mnode_t *))GetCallAddress(dwSV_AddToFatPVS);

	//sv_worldmodel
#define SV_WORLDMODEL_SIG "\xE8\x2A\x2A\x2A\x2A\x8B\x0D"
	DWORD dwsv_worldmodel = MH_SIGFind((DWORD)g_engfuncs.pfnSetFatPVS, 0x50, SV_WORLDMODEL_SIG, sizeof(SV_WORLDMODEL_SIG)-1);
	if(!dwsv_worldmodel)
		SIG_NOT_FOUND("sv_worldmodel");

	sv_worldmodel = *(model_t ***)(dwsv_worldmodel + 7);

	//SV_AddToFatPAS
	DWORD dwSV_AddToFatPAS = MH_SIGFind((DWORD)g_engfuncs.pfnSetFatPAS, 0x50, SV_ADDTOFATPVS_SIG, sizeof(SV_ADDTOFATPVS_SIG)-1);
	if(!dwSV_AddToFatPAS)
		SIG_NOT_FOUND("SV_AddToFatPAS");

	gHookFuncs.SV_AddToFatPAS = (void (*)(float *, mnode_t *))GetCallAddress(dwSV_AddToFatPAS);

	MH_InlineHook((void *)gHookFuncs.GetEntityInit, GetEntityInit, (void *&)gHookFuncs.GetEntityInit);

	SERVER_PRINT("[Meta Renderer] Engine hook installed.\n");
}