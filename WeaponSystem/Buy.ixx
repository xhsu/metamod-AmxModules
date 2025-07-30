export module Buy;

import CBase;
import PlayerItem;

export template <typename T>
CPrefabWeapon* BuyWeaponByCppClass(CBasePlayer* pPlayer) noexcept
{
	return T::BuyWeapon(pPlayer);
}
