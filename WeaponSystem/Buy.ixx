export module Buy;

import CBase;
import PlayerItem;

export template <typename T>
CPrefabWeapon* BuyWeaponByWeaponClass(CBasePlayer* pPlayer) noexcept
{
	return T::BuyWeapon(pPlayer);
}
