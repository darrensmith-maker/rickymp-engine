#include "actionequip.hpp"

#include <algorithm>
#include <limits>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/weapontype.hpp"

#include <components/compiler/locals.hpp>
#include <components/esm/loadweap.hpp>

#include "inventorystore.hpp"
#include "player.hpp"
#include "class.hpp"

namespace
{
    void autoEquipCompatibleAmmunition(MWWorld::InventoryStore& invStore, const MWWorld::Ptr& actor,
        const MWWorld::Ptr& weapon)
    {
        // NPC combat already selects ammunition using its own rating logic. Keep this helper local
        // to the player so it cannot interfere with actor authority or scripted equipment packets.
        if (actor != MWMechanics::getPlayer() || weapon.getTypeName() != typeid(ESM::Weapon).name())
            return;

        const ESM::Weapon* weaponRecord = weapon.get<ESM::Weapon>()->mBase;
        const int ammunitionType = MWMechanics::getWeaponType(weaponRecord->mData.mType)->mAmmoType;
        if (ammunitionType != ESM::Weapon::Arrow && ammunitionType != ESM::Weapon::Bolt)
            return;

        MWWorld::ContainerStoreIterator current
            = invStore.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
        if (current != invStore.end() && current.getType() == MWWorld::ContainerStore::Type_Weapon
            && current->getRefData().getCount() > 0
            && current->get<ESM::Weapon>()->mBase->mData.mType == ammunitionType)
        {
            // Respect the ammunition explicitly chosen by the player when it is compatible.
            return;
        }

        MWWorld::ContainerStoreIterator best = invStore.end();
        int bestDamage = std::numeric_limits<int>::min();
        for (MWWorld::ContainerStoreIterator it(invStore.begin(MWWorld::ContainerStore::Type_Weapon));
             it != invStore.end(); ++it)
        {
            if (it->getRefData().getCount() <= 0)
                continue;

            const ESM::Weapon* ammunition = it->get<ESM::Weapon>()->mBase;
            if (ammunition->mData.mType != ammunitionType)
                continue;

            const int damage = std::max({ static_cast<int>(ammunition->mData.mChop[1]),
                static_cast<int>(ammunition->mData.mSlash[1]),
                static_cast<int>(ammunition->mData.mThrust[1]) });
            if (best == invStore.end() || damage > bestDamage)
            {
                best = it;
                bestDamage = damage;
            }
        }

        if (best != invStore.end())
            invStore.equip(MWWorld::InventoryStore::Slot_Ammunition, best, actor);
    }
}

namespace MWWorld
{
    ActionEquip::ActionEquip (const MWWorld::Ptr& object, bool force)
    : Action (false, object)
    , mForce(force)
    {
    }

    void ActionEquip::executeImp (const Ptr& actor)
    {
        MWWorld::Ptr object = getTarget();
        MWWorld::InventoryStore& invStore = actor.getClass().getInventoryStore(actor);

        if (object.getClass().hasItemHealth(object) && object.getCellRef().getCharge() == 0)
        {
            if (actor == MWMechanics::getPlayer())
                MWBase::Environment::get().getWindowManager()->messageBox("#{sInventoryMessage1}");

            return;
        }

        if (!mForce)
        {
            std::pair <int, std::string> result = object.getClass().canBeEquipped (object, actor);

            // display error message if the player tried to equip something
            if (!result.second.empty() && actor == MWMechanics::getPlayer())
                MWBase::Environment::get().getWindowManager()->messageBox(result.second);

            switch(result.first)
            {
                case 0:
                    return;
                default:
                    break;
            }
        }

        // slots that this item can be equipped in
        std::pair<std::vector<int>, bool> slots_ = getTarget().getClass().getEquipmentSlots(getTarget());
        if (slots_.first.empty())
            return;

        // retrieve ContainerStoreIterator to the item
        MWWorld::ContainerStoreIterator it = invStore.begin();
        for (; it != invStore.end(); ++it)
        {
            if (*it == object)
            {
                break;
            }
        }

        if (it == invStore.end())
        {
            std::stringstream error;
            error << "ActionEquip can't find item " << object.getCellRef().getRefId();
            throw std::runtime_error(error.str());
        }

        // equip the item in the first free slot
        std::vector<int>::const_iterator slot=slots_.first.begin();
        for (;slot!=slots_.first.end(); ++slot)
        {
            // if the item is equipped already, nothing to do
            if (invStore.getSlot(*slot) == it)
            {
                autoEquipCompatibleAmmunition(invStore, actor, object);
                return;
            }

            if (invStore.getSlot(*slot) == invStore.end())
            {
                // slot is not occupied
                invStore.equip(*slot, it, actor);
                break;
            }
        }

        // all slots are occupied -> cycle
        // move all slots one towards begin(), then equip the item in the slot that is now free
        if (slot == slots_.first.end())
        {
            ContainerStoreIterator enchItem = invStore.getSelectedEnchantItem();
            bool reEquip = false;
            for (slot = slots_.first.begin(); slot != slots_.first.end(); ++slot)
            {
                invStore.unequipSlot(*slot, actor, false);
                if (slot + 1 != slots_.first.end())
                {
                    invStore.equip(*slot, invStore.getSlot(*(slot + 1)), actor);
                }
                else
                {
                    invStore.equip(*slot, it, actor);
                }

                //Fix for issue of selected enchated item getting remmoved on cycle
                if (invStore.getSlot(*slot) == enchItem)
                {
                    reEquip = true;
                }
            }
            if (reEquip)
            {
                invStore.setSelectedEnchantItem(enchItem);
            }
        }

        autoEquipCompatibleAmmunition(invStore, actor, object);
    }
}
