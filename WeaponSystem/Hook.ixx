export module Hook;

import std;
import hlsdk;

import UtlHook;

import Uranus;

extern "C++" PFN_ENTITYINIT __cdecl OrpheuF_GetDispatch(char const* pszClassName) noexcept;

export namespace HookInfo
{
	inline FunctionHook GetDispatch{ &OrpheuF_GetDispatch };
}
