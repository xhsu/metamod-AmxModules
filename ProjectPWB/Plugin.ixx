export module Plugin;

export import hlsdk;
export import metamod_api;
export import amxxmodule_api;


// Global variables from metamod.
// These variable names are referenced by various macros.
export inline meta_globals_t *gpMetaGlobals = nullptr;	// metamod globals
export inline gamedll_funcs_t *gpGamedllFuncs = nullptr;	// gameDLL function tables
export inline mutil_funcs_t *gpMetaUtilFuncs = nullptr;	// metamod utility functions

export inline constexpr plugin_info_t gPluginInfo =
{
	.ifvers		= META_INTERFACE_VERSION,
	.name		= "P, W & B",
	.version	= "2.0.0",
	.date		= __DATE__,
	.author		= "xhsu",
	.url		= "http://www.metamod.org/",
	.logtag		= "PWB",
	.loadable	= PT_ANYTIME,
	.unloadable	= PT_ANYPAUSE,
};

export inline constexpr auto PLID = &gPluginInfo;

/************* AMXX Stuff *************/

// *** Globals ***
// Module info
export inline constexpr amxx_module_info_t g_ModuleInfo =
{
	.name		= gPluginInfo.name,
	.author		= gPluginInfo.author,
	.version	= gPluginInfo.version,
	.reload		= false,	// Should reload module on new map?
	.logtag		= gPluginInfo.logtag,
	.library	= "PWB",
	.libclass	= "",	// LUNA: no idea what is this, but keep it empty.
};