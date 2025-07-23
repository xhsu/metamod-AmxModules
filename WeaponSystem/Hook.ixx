export module Hook;

import std;
import hlsdk;

import UtlHook;

import Uranus;

extern "C++" PFN_ENTITYINIT __cdecl OrpheuF_GetDispatch(char const* pszClassName) noexcept;
extern "C++" void __fastcall OrpheuF_DropPlayerItem(class CBasePlayer* pPlayer, void*, char const* pszItemName) noexcept;
extern "C++" void __cdecl OrpheuF_packPlayerItem(class CBasePlayer* pPlayer, class CBasePlayerItem* pItem, bool packAmmo) noexcept;
extern "C++" qboolean __cdecl OrpheuF_HandleBuyAliasCommands(CBasePlayer* player, const char* pszCommand) noexcept;
extern "C++" bool __cdecl OrpheuF_BuyGunAmmo(class CBasePlayer* player, class CBasePlayerItem* weapon, bool bBlinkMoney) noexcept;

export namespace HookInfo
{
	inline FunctionHook GetDispatch{ &OrpheuF_GetDispatch };
	inline FunctionHook DropPlayerItem{ &OrpheuF_DropPlayerItem };
	inline FunctionHook packPlayerItem{ &OrpheuF_packPlayerItem };
	inline FunctionHook HandleBuyAliasCommands{ &OrpheuF_HandleBuyAliasCommands };
	inline FunctionHook BuyGunAmmo{ &OrpheuF_BuyGunAmmo };
}
