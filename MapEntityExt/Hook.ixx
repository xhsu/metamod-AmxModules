export module Hook;

import std;
import hlsdk;

import Plugin;
import Uranus;

import UtlHook;
import UtlString;

export using amxfw_t = decltype(std::invoke(MF_RegisterSPForwardByName, nullptr, nullptr));
export inline std::map<std::string_view, std::vector<amxfw_t>, sv_less_t> gRegisterEntitySpawn{};
export inline std::map<std::string_view, std::vector<amxfw_t>, sv_less_t> gRegisterEntityKvd{};

extern "C++" PFN_ENTITYINIT __cdecl OrpheuF_GetDispatch(char const* pszClassName) noexcept;

export namespace HookInfo
{
	inline FunctionHook GetDispatch{ &OrpheuF_GetDispatch };
}
