export module Plugin;

export import eiface;
export import progdefs;

// ***** AMXX stuff *****

// module interface version was 1
// 2 - added logtag to struct (amxx1.1-rc1)
// 3 - added new tagAMX structure (amxx1.5)
// 4 - added new 'library' setting for direct loading
export inline constexpr auto AMXX_INTERFACE_VERSION = 4;

// amxx module info
export struct amxx_module_info_s
{
	const char *name;
	const char *author;
	const char *version;
	int reload;				// reload on mapchange when nonzero
	const char *logtag;		// added in version 2
	const char *library;	// added in version 4
	const char *libclass;	// added in version 4
};

// return values from functions called by amxx
export inline constexpr auto AMXX_OK = 0;				/* no error */
export inline constexpr auto AMXX_IFVERS = 1;			/* interface version */
export inline constexpr auto AMXX_PARAM = 2;			/* Invalid parameter */
export inline constexpr auto AMXX_FUNC_NOT_PRESENT = 3;	/* Function not present */

export inline constexpr auto AMXX_GAME_OK = 0;			/* This module can load on the current game mod. */
export inline constexpr auto AMXX_GAME_BAD = 1;			/* This module can not load on the current game mod. */

// *** Small stuff ***
// The next section is copied from the amx.h file
// Copyright (c) ITB CompuPhase, 1997-2005

export import <cstdint>;

export inline constexpr auto PAWN_CELL_SIZE = 32;

export using cell = std::int32_t;
export using ucell = std::uint32_t;
export using REAL = float;

export inline constexpr auto UNPACKEDMAX = ((1 << (sizeof(cell) - 1) * 8) - 1);
export inline constexpr auto UNLIMITED = (~1u >> 1);

struct tagAMX;
export using AMX_NATIVE = cell(*)(struct tagAMX *amx, cell *params);
export using AMX_CALLBACK = int (*)(struct tagAMX *amx, cell index, cell *result, cell *params);
export using AMX_DEBUG = int (*)(struct tagAMX *amx);

export struct AMX_NATIVE_INFO
{
	const char *name;
	AMX_NATIVE func;
};

export inline constexpr auto AMX_USERNUM = 4;

/* The AMX structure is the internal structure for many functions. Not all
 * fields are valid at all times; many fields are cached in local variables.
 */
export struct tagAMX
{
	unsigned char *base;	/* points to the AMX header plus the code, optionally also the data */
	unsigned char *data;	/* points to separate data+stack+heap, may be NULL */
	AMX_CALLBACK callback;
	AMX_DEBUG debug;		/* debug callback */

	/* for external functions a few registers must be accessible from the outside */
	cell cip;	/* instruction pointer: relative to base + amxhdr->cod */
	cell frm;	/* stack frame base: relative to base + amxhdr->dat */
	cell hea;	/* top of the heap: relative to base + amxhdr->dat */
	cell hlw;	/* bottom of the heap: relative to base + amxhdr->dat */
	cell stk;	/* stack pointer: relative to base + amxhdr->dat */
	cell stp;	/* top of the stack: relative to base + amxhdr->dat */
	int flags;	/* current status, see amx_Flags() */

	/* user data */
	long usertags[AMX_USERNUM];

	//okay userdata[3] in AMX Mod X is for the CPlugin * pointer
	//we're also gonna set userdata[2] to a special debug structure
	void *userdata[AMX_USERNUM];

	/* native functions can raise an error */
	int error;

	/* passing parameters requires a "count" field */
	int paramcount;

	/* the sleep opcode needs to store the full AMX status */
	cell pri;
	cell alt;
	cell reset_stk;
	cell reset_hea;
	cell sysreq_d;	/* relocated address/value for the SYSREQ.D opcode */

	/* support variables for the JIT */
	int reloc_size; /* required temporary buffer for relocations */
	long code_size; /* estimated memory footprint of the native code */
};

export using AMX = tagAMX;

export enum EAmxErrorCodes
{
	AMX_ERR_NONE,
	/* reserve the first 15 error codes for exit codes of the abstract machine */
	AMX_ERR_EXIT,         /* forced exit */
	AMX_ERR_ASSERT,       /* assertion failed */
	AMX_ERR_STACKERR,     /* stack/heap collision */
	AMX_ERR_BOUNDS,       /* index out of bounds */
	AMX_ERR_MEMACCESS,    /* invalid memory access */
	AMX_ERR_INVINSTR,     /* invalid instruction */
	AMX_ERR_STACKLOW,     /* stack underflow */
	AMX_ERR_HEAPLOW,      /* heap underflow */
	AMX_ERR_CALLBACK,     /* no callback, or invalid callback */
	AMX_ERR_NATIVE,       /* native function failed */
	AMX_ERR_DIVIDE,       /* divide by zero */
	AMX_ERR_SLEEP,        /* go into sleepmode - code can be restarted */
	AMX_ERR_INVSTATE,     /* invalid state for this access */

	AMX_ERR_MEMORY = 16,  /* out of memory */
	AMX_ERR_FORMAT,       /* invalid file format */
	AMX_ERR_VERSION,      /* file is for a newer version of the AMX */
	AMX_ERR_NOTFOUND,     /* function not found */
	AMX_ERR_INDEX,        /* invalid index parameter (bad entry point) */
	AMX_ERR_DEBUG,        /* debugger cannot run */
	AMX_ERR_INIT,         /* AMX not initialized (or doubly initialized) */
	AMX_ERR_USERDATA,     /* unable to set user data field (table full) */
	AMX_ERR_INIT_JIT,     /* cannot initialize the JIT */
	AMX_ERR_PARAMS,       /* parameter error */
	AMX_ERR_DOMAIN,       /* domain error, expression result does not fit in range */
};

// *** Types ***
export using PFN_REQ_FNPTR = void *(*)(const char * /*name*/);

// ***** Module funcs stuff *****
export enum ForwardExecType
{
	ET_IGNORE = 0,					// Ignore return vaue
	ET_STOP,						// Stop on PLUGIN_HANDLED
	ET_STOP2,						// Stop on PLUGIN_HANDLED, continue on other values, return biggest return value
	ET_CONTINUE,					// Continue; return biggest return value
};

export enum ForwardParam
{
	FP_DONE = -1,					// specify this as the last argument
	// only tells the function that there are no more arguments
	FP_CELL,						// normal cell
	FP_FLOAT,						// float; used as normal cell though
	FP_STRING,						// string
	FP_STRINGEX,					// string; will be updated to the last function's value
	FP_ARRAY,						// array; use the return value of prepareArray.
};

export enum PlayerProp
{
	Player_Name,		//String
	Player_Ip,			//String
	Player_Team,		//String
	Player_Ingame,		//bool
	Player_Authorized,	//bool
	Player_Vgui,		//bool
	Player_Time,		//float
	Player_Playtime,	//float
	Player_MenuExpire,	//float
	Player_Weapons,		//struct{int,int}[32]
	Player_CurrentWeapon,	//int
	Player_TeamID,			//int
	Player_Deaths,			//int
	Player_Aiming,			//int
	Player_Menu,			//int
	Player_Keys,			//int
	Player_Flags,			//int[32]
	Player_Newmenu,			//int
	Player_NewmenuPage,		//int
};

export enum LibType
{
	LibType_Library,
	LibType_Class
};

export inline constexpr auto MSGBLOCK_SET = 0;
export inline constexpr auto MSGBLOCK_GET = 1;

export inline constexpr auto BLOCK_NOT = 0;
export inline constexpr auto BLOCK_ONCE = 1;
export inline constexpr auto BLOCK_SET = 2;

export import edict;

export using AUTHORIZEFUNC = void (*)(int player, const char *authstring);

export using PFN_ADD_NATIVES				= int			(*)(const AMX_NATIVE_INFO * /*list*/);
export using PFN_ADD_NEW_NATIVES			= int			(*)(const AMX_NATIVE_INFO * /*list*/);
export using PFN_BUILD_PATHNAME				= char			*(*)(const char * /*format*/, ...);
export using PFN_BUILD_PATHNAME_R			= char			*(*)(char * /*buffer*/, size_t /* maxlen */, const char * /* format */, ...);
export using PFN_GET_AMXADDR				= cell			*(*)(AMX * /*amx*/, cell /*offset*/);
export using PFN_PRINT_SRVCONSOLE			= void			(*)(const char * /*format*/, ...);
export using PFN_GET_MODNAME				= const char	*(*)(void);
export using PFN_GET_AMXSCRIPTNAME			= const char	*(*)(int /*id*/);
export using PFN_GET_AMXSCRIPT				= AMX			*(*)(int /*id*/);
export using PFN_FIND_AMXSCRIPT_BYAMX		= int			(*)(const AMX * /*amx*/);
export using PFN_FIND_AMXSCRIPT_BYNAME		= int			(*)(const char * /*name*/);
export using PFN_SET_AMXSTRING				= int			(*)(AMX * /*amx*/, cell /*amx_addr*/, const char * /* source */, int /* max */);
export using PFN_GET_AMXSTRING				= char			*(*)(AMX * /*amx*/, cell /*amx_addr*/, int /*bufferId*/, int * /*pLen*/);
export using PFN_GET_AMXSTRINGLEN			= int			(*)(const cell *ptr);
export using PFN_FORMAT_AMXSTRING			= char			*(*)(AMX * /*amx*/, cell * /*params*/, int /*startParam*/, int * /*pLen*/);
export using PFN_COPY_AMXMEMORY				= void			(*)(cell * /*dest*/, const cell * /*src*/, int /*len*/);
export using PFN_LOG						= void			(*)(const char * /*fmt*/, ...);
export using PFN_LOG_ERROR					= void			(*)(AMX * /*amx*/, int /*err*/, const char * /*fmt*/, ...);
export using PFN_RAISE_AMXERROR				= int			(*)(AMX * /*amx*/, int /*error*/);
export using PFN_REGISTER_FORWARD			= int			(*)(const char * /*funcname*/, ForwardExecType /*exectype*/, ... /*paramtypes terminated by PF_DONE*/);
export using PFN_EXECUTE_FORWARD			= int			(*)(int /*id*/, ... /*params*/);
export using PFN_PREPARE_CELLARRAY			= cell			(*)(cell * /*ptr*/, unsigned int /*size*/);
export using PFN_PREPARE_CHARARRAY			= cell			(*)(char * /*ptr*/, unsigned int /*size*/);
export using PFN_PREPARE_CELLARRAY_A		= cell			(*)(cell * /*ptr*/, unsigned int /*size*/, bool /*copyBack*/);
export using PFN_PREPARE_CHARARRAY_A		= cell			(*)(char * /*ptr*/, unsigned int /*size*/, bool /*copyBack*/);
export using PFN_IS_PLAYER_VALID			= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_NAME			= const char	*(*)(int /*id*/);
export using PFN_GET_PLAYER_IP				= const char	*(*)(int /*id*/);
export using PFN_IS_PLAYER_INGAME			= int			(*)(int /*id*/);
export using PFN_IS_PLAYER_BOT				= int			(*)(int /*id*/);
export using PFN_IS_PLAYER_AUTHORIZED		= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_TIME			= float			(*)(int /*id*/);
export using PFN_GET_PLAYER_PLAYTIME		= float			(*)(int /*id*/);
export using PFN_GETPLAYERFLAGS				= int			(*)(int /* id*/);
export using PFN_GET_PLAYER_CURWEAPON		= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_TEAM			= const char	*(*)(int /*id*/);
export using PFN_GET_PLAYER_TEAMID			= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_DEATHS			= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_MENU			= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_KEYS			= int			(*)(int /*id*/);
export using PFN_IS_PLAYER_ALIVE			= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_FRAGS			= int			(*)(int /*id*/);
export using PFN_IS_PLAYER_CONNECTING		= int			(*)(int /*id*/);
export using PFN_IS_PLAYER_HLTV				= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_ARMOR			= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_HEALTH			= int			(*)(int /*id*/);
export using PFN_GET_PLAYER_EDICT			= edict_t		*(*)(int /*id*/);
export using PFN_PLAYER_PROP_ADDR			= void			*(*)(int /*id*/, int /*prop*/);
export using PFN_AMX_EXEC					= int			(*)(AMX * /*amx*/, cell * /*return val*/, int /*index*/);
export using PFN_AMX_EXECV					= int			(*)(AMX * /*amx*/, cell * /*return val*/, int /*index*/, int /*numparams*/, cell[] /*params*/);
export using PFN_AMX_ALLOT					= int			(*)(AMX * /*amx*/, int /*length*/, cell * /*amx_addr*/, cell ** /*phys_addr*/);
export using PFN_AMX_FINDPUBLIC				= int			(*)(AMX * /*amx*/, const char * /*func name*/, int * /*index*/);
export using PFN_AMX_FINDNATIVE				= int			(*)(AMX * /*amx*/, const char * /*func name*/, int * /*index*/);
export using PFN_LOAD_AMXSCRIPT				= int			(*)(AMX * /*amx*/, void ** /*code*/, const char * /*path*/, char[64] /*error info*/, int /* debug */);
export using PFN_UNLOAD_AMXSCRIPT			= int			(*)(AMX * /*amx*/, void ** /*code*/);
export using PFN_REAL_TO_CELL				= cell			(*)(REAL /*x*/);
export using PFN_CELL_TO_REAL				= REAL			(*)(cell /*x*/);
export using PFN_REGISTER_SPFORWARD			= int			(*)(AMX * /*amx*/, int /*func*/, ... /*params*/);
export using PFN_REGISTER_SPFORWARD_BYNAME	= int			(*)(AMX * /*amx*/, const char * /*funcName*/, ... /*params*/);
export using PFN_UNREGISTER_SPFORWARD		= void			(*)(int /*id*/);
export using PFN_MERGEDEFINITION_FILE		= void			(*)(const char * /*filename*/);
export using PFN_FORMAT						= const char	*(*)(const char * /*fmt*/, ... /*params*/);
export using PFN_REGISTERFUNCTION			= void			(*)(void * /*pfn*/, const char * /*desc*/);
export using PFN_AMX_PUSH					= int			(*)(AMX * /*amx*/, cell /*value*/);
export using PFN_SET_TEAM_INFO				= int			(*)(int /*player */, int /*teamid */, const char * /*name */);
export using PFN_REG_AUTH_FUNC				= void			(*)(AUTHORIZEFUNC);
export using PFN_UNREG_AUTH_FUNC			= void			(*)(AUTHORIZEFUNC);
export using PFN_FINDLIBRARY				= int			(*)(const char * /*name*/, LibType /*type*/);
export using PFN_ADDLIBRARIES				= size_t		(*)(const char * /*name*/, LibType /*type*/, void * /*parent*/);
export using PFN_REMOVELIBRARIES			= size_t		(*)(void * /*parent*/);
export using PFN_OVERRIDENATIVES			= void			(*)(AMX_NATIVE_INFO * /*natives*/, const char * /*myname*/);
export using PFN_GETLOCALINFO				= const char	*(*)(const char * /*name*/, const char * /*def*/);
export using PFN_AMX_REREGISTER				= int			(*)(AMX * /*amx*/, AMX_NATIVE_INFO * /*list*/, int /*list*/);
export using PFN_REGISTERFUNCTIONEX			= void			*(*)(void * /*pfn*/, const char * /*desc*/);
export using PFN_MESSAGE_BLOCK				= void			(*)(int /* mode */, int /* message */, int * /* opt */);

export inline PFN_ADD_NATIVES				MF_AddNatives = nullptr;
export inline PFN_ADD_NEW_NATIVES			MF_AddNewNatives = nullptr;
export inline PFN_BUILD_PATHNAME			MF_BuildPathname = nullptr;
export inline PFN_BUILD_PATHNAME_R			MF_BuildPathnameR = nullptr;
export inline PFN_GET_AMXADDR				MF_GetAmxAddr = nullptr;
export inline PFN_PRINT_SRVCONSOLE			MF_PrintSrvConsole = nullptr;
export inline PFN_GET_MODNAME				MF_GetModname = nullptr;
export inline PFN_GET_AMXSCRIPTNAME			MF_GetScriptName = nullptr;
export inline PFN_GET_AMXSCRIPT				MF_GetScriptAmx = nullptr;
export inline PFN_FIND_AMXSCRIPT_BYAMX		MF_FindScriptByAmx = nullptr;
export inline PFN_FIND_AMXSCRIPT_BYNAME		MF_FindScriptByName = nullptr;
export inline PFN_SET_AMXSTRING				MF_SetAmxString = nullptr;
export inline PFN_GET_AMXSTRING				MF_GetAmxString = nullptr;
export inline PFN_GET_AMXSTRINGLEN			MF_GetAmxStringLen = nullptr;
export inline PFN_FORMAT_AMXSTRING			MF_FormatAmxString = nullptr;
export inline PFN_COPY_AMXMEMORY			MF_CopyAmxMemory = nullptr;
export inline PFN_LOG						MF_Log = nullptr;
export inline PFN_LOG_ERROR					MF_LogError = nullptr;
export inline PFN_RAISE_AMXERROR			MF_RaiseAmxError = nullptr;
export inline PFN_REGISTER_FORWARD			MF_RegisterForward = nullptr;
export inline PFN_EXECUTE_FORWARD			MF_ExecuteForward = nullptr;
export inline PFN_PREPARE_CELLARRAY			MF_PrepareCellArray = nullptr;
export inline PFN_PREPARE_CHARARRAY			MF_PrepareCharArray = nullptr;
export inline PFN_PREPARE_CELLARRAY_A		MF_PrepareCellArrayA = nullptr;
export inline PFN_PREPARE_CHARARRAY_A		MF_PrepareCharArrayA = nullptr;
export inline PFN_IS_PLAYER_VALID			MF_IsPlayerValid = nullptr;
export inline PFN_GET_PLAYER_NAME			MF_GetPlayerName = nullptr;
export inline PFN_GET_PLAYER_IP				MF_GetPlayerIP = nullptr;
export inline PFN_IS_PLAYER_INGAME			MF_IsPlayerIngame = nullptr;
export inline PFN_IS_PLAYER_BOT				MF_IsPlayerBot = nullptr;
export inline PFN_IS_PLAYER_AUTHORIZED		MF_IsPlayerAuthorized = nullptr;
export inline PFN_GET_PLAYER_TIME			MF_GetPlayerTime = nullptr;
export inline PFN_GET_PLAYER_PLAYTIME		MF_GetPlayerPlayTime = nullptr;
export inline PFN_GET_PLAYER_CURWEAPON		MF_GetPlayerCurweapon = nullptr;
export inline PFN_GET_PLAYER_TEAMID			MF_GetPlayerTeamID = nullptr;
export inline PFN_GET_PLAYER_DEATHS			MF_GetPlayerDeaths = nullptr;
export inline PFN_GET_PLAYER_MENU			MF_GetPlayerMenu = nullptr;
export inline PFN_GET_PLAYER_KEYS			MF_GetPlayerKeys = nullptr;
export inline PFN_IS_PLAYER_ALIVE			MF_IsPlayerAlive = nullptr;
export inline PFN_GET_PLAYER_FRAGS			MF_GetPlayerFrags = nullptr;
export inline PFN_IS_PLAYER_CONNECTING		MF_IsPlayerConnecting = nullptr;
export inline PFN_IS_PLAYER_HLTV			MF_IsPlayerHLTV = nullptr;
export inline PFN_GET_PLAYER_ARMOR			MF_GetPlayerArmor = nullptr;
export inline PFN_GET_PLAYER_HEALTH			MF_GetPlayerHealth = nullptr;
export inline PFN_AMX_EXEC					MF_AmxExec = nullptr;
export inline PFN_AMX_EXECV					MF_AmxExecv = nullptr;
export inline PFN_AMX_ALLOT					MF_AmxAllot = nullptr;
export inline PFN_AMX_FINDPUBLIC			MF_AmxFindPublic = nullptr;
export inline PFN_LOAD_AMXSCRIPT			MF_LoadAmxScript = nullptr;
export inline PFN_UNLOAD_AMXSCRIPT			MF_UnloadAmxScript = nullptr;
export inline PFN_REAL_TO_CELL				MF_RealToCell = nullptr;
export inline PFN_CELL_TO_REAL				MF_CellToReal = nullptr;
export inline PFN_REGISTER_SPFORWARD		MF_RegisterSPForward = nullptr;
export inline PFN_REGISTER_SPFORWARD_BYNAME	MF_RegisterSPForwardByName = nullptr;
export inline PFN_UNREGISTER_SPFORWARD		MF_UnregisterSPForward = nullptr;
export inline PFN_MERGEDEFINITION_FILE		MF_MergeDefinitionFile = nullptr;
export inline PFN_AMX_FINDNATIVE			MF_AmxFindNative = nullptr;
export inline PFN_GETPLAYERFLAGS			MF_GetPlayerFlags = nullptr;
export inline PFN_GET_PLAYER_EDICT			MF_GetPlayerEdict = nullptr;
export inline PFN_FORMAT					MF_Format = nullptr;
export inline PFN_GET_PLAYER_TEAM			MF_GetPlayerTeam = nullptr;
export inline PFN_REGISTERFUNCTION			MF_RegisterFunction = nullptr;
export inline PFN_REQ_FNPTR					MF_RequestFunction = nullptr;
export inline PFN_AMX_PUSH					MF_AmxPush = nullptr;
export inline PFN_SET_TEAM_INFO				MF_SetPlayerTeamInfo = nullptr;
export inline PFN_PLAYER_PROP_ADDR			MF_PlayerPropAddr = nullptr;
export inline PFN_REG_AUTH_FUNC				MF_RegAuthFunc = nullptr;
export inline PFN_UNREG_AUTH_FUNC			MF_UnregAuthFunc = nullptr;
export inline PFN_FINDLIBRARY				MF_FindLibrary = nullptr;
export inline PFN_ADDLIBRARIES				MF_AddLibraries = nullptr;
export inline PFN_REMOVELIBRARIES			MF_RemoveLibraries = nullptr;
export inline PFN_OVERRIDENATIVES			MF_OverrideNatives = nullptr;
export inline PFN_GETLOCALINFO				MF_GetLocalInfo = nullptr;
export inline PFN_AMX_REREGISTER			MF_AmxReRegister = nullptr;
export inline PFN_REGISTERFUNCTIONEX		MF_RegisterFunctionEx = nullptr;
export inline PFN_MESSAGE_BLOCK				MF_MessageBlock = nullptr;


// Global variables from metamod.
// These variable names are referenced by various macros.
export import meta_api;
export import mutil;
export import plinfo;

export inline meta_globals_t *gpMetaGlobals = nullptr;	// metamod globals
export inline gamedll_funcs_t *gpGamedllFuncs = nullptr;	// gameDLL function tables
export inline mutil_funcs_t *gpMetaUtilFuncs = nullptr;	// metamod utility functions

export inline constexpr plugin_info_t gPluginInfo =
{
	.ifvers		= META_INTERFACE_VERSION,
	.name		= "Uranus",
	.version	= "1.0.0",
	.date		= __DATE__,
	.author		= "xhsu",
	.url		= "http://www.metamod.org/",
	.logtag		= "URN",
	.loadable	= PT_ANYTIME,
	.unloadable	= PT_ANYPAUSE,
};

export inline constexpr auto PLID = &gPluginInfo;

/************* AMXX Stuff *************/

// *** Globals ***
// Module info
export inline constexpr amxx_module_info_s g_ModuleInfo =
{
	.name		= gPluginInfo.name,
	.author		= gPluginInfo.author,
	.version	= gPluginInfo.version,
	.reload		= false,	// Should reload module on new map?
	.logtag		= gPluginInfo.logtag,
	.library	= "Uranus",
	.libclass	= "",	// LUNA: no idea what is this, but keep it empty.
};
