
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>


import UtlHook;

import WinAPI;

// This won't work unless place in a CPP file.
// https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-entry-point-function
BOOL WINAPI DllMain(
	HINSTANCE hinstDLL,	// handle to DLL module
	DWORD fdwReason,	// reason for calling function
	LPVOID lpReserved)	// reserved
{
	// Perform actions based on the reason for calling.
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		// Initialize once for each new process.
		// Return FALSE to fail DLL load.

		gSelfModuleBase = MH_GetModuleBase(hinstDLL);
		gSelfModuleSize = MH_GetModuleSize(hinstDLL);

		gSelfModuleHandle = hinstDLL;

		break;

	case DLL_THREAD_ATTACH:
		// Do thread-specific initialization.
		break;

	case DLL_THREAD_DETACH:
		// Do thread-specific cleanup.
		break;

	case DLL_PROCESS_DETACH:
		// Perform any necessary cleanup.
		break;
	}

	return TRUE;  // Successful DLL_PROCESS_ATTACH.
}
