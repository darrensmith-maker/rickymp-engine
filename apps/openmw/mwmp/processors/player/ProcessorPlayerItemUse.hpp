#ifndef OPENMW_PROCESSORPLAYERITEMUSE_HPP
#define OPENMW_PROCESSORPLAYERITEMUSE_HPP

#include <components/esm/loadbook.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwgui/inventorywindow.hpp"
#include "apps/openmw/mwgui/windowmanagerimp.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"

#include "apps/openmw/mwmp/MechanicsHelper.hpp"

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerItemUse final: public PlayerProcessor
    {
    public:
        ProcessorPlayerItemUse()
        {
            BPP_INIT(ID_PLAYER_ITEM_USE)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (!isLocal()) return;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_ITEM_USE about LocalPlayer from server");

            if (!isRequest())
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- refId: %s, count: %i, charge: %i, enchantmentCharge: %f, soul: %s",
                    player->usedItem.refId.c_str(), player->usedItem.count, player->usedItem.charge,
                    player->usedItem.enchantmentCharge, player->usedItem.soul.c_str());

                MWWorld::Ptr playerPtr = MWBase::Environment::get().getWorld()->getPlayerPtr();
                MWWorld::InventoryStore &inventoryStore = playerPtr.getClass().getInventoryStore(playerPtr);

                MWWorld::Ptr itemPtr = MechanicsHelper::getItemPtrFromStore(player->usedItem, inventoryStore);

                if (itemPtr)
                {
                    // A magic scroll is selected for casting, not used as a
                    // regular book. Calling useItem() here would execute
                    // ActionRead and open GM_Scroll before the enchantment is
                    // selected.
                    bool isEnchantedScroll = false;
                    if (player->usingItemMagic
                        && itemPtr.getTypeName() == typeid(ESM::Book).name())
                    {
                        const MWWorld::LiveCellRef<ESM::Book>* book = itemPtr.get<ESM::Book>();
                        isEnchantedScroll = book->mBase->mData.mIsScroll
                            && !book->mBase->mEnchant.empty();
                    }

                    if (!isEnchantedScroll)
                        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->useItem(itemPtr);

                    if (player->usingItemMagic)
                    {
                        MWWorld::ContainerStoreIterator storeIterator = inventoryStore.begin();
                        for (; storeIterator != inventoryStore.end(); ++storeIterator)
                        {
                            if (*storeIterator == itemPtr)
                                break;
                        }

                        if (storeIterator != inventoryStore.end())
                        {
                            inventoryStore.setSelectedEnchantItem(storeIterator);
                            // Update the HUD and clear a previously selected
                            // regular spell immediately, in the same frame.
                            MWBase::Environment::get().getWindowManager()
                                ->setSelectedEnchantItem(*storeIterator);
                        }
                    }

                    if (player->itemUseDrawState != MWMechanics::DrawState_Nothing)
                        playerPtr.getClass().getNpcStats(playerPtr).setDrawState(static_cast<MWMechanics::DrawState_>(player->itemUseDrawState));
                }
                else
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Cannot use non-existent item %s", player->usedItem.refId.c_str());
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERITEMUSE_HPP
