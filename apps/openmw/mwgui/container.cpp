#include "container.hpp"

#include <algorithm>
#include <MyGUI_InputManager.h>
#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>

#include <cmath>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/CellController.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/scriptmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"

#include "../mwmechanics/aipackage.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/summoning.hpp"

#include "../mwscript/interpretercontext.hpp"

#include "inventorywindow.hpp"

#include "itemview.hpp"
#include "inventoryitemmodel.hpp"
#include "containeritemmodel.hpp"
#include "sortfilteritemmodel.hpp"
#include "pickpocketitemmodel.hpp"
#include "draganddrop.hpp"
#include "widgets.hpp"

namespace MWGui
{

    ContainerWindow::ContainerWindow(DragAndDrop* dragAndDrop)
        : WindowBase("openmw_container_window.layout")
        , mDragAndDrop(dragAndDrop)
        , mSortModel(nullptr)
        , mModel(nullptr)
        , mSelectedItem(-1)
    {
        getWidget(mTakeButton, "TakeButton");
        getWidget(mCloseButton, "CloseButton");
        getWidget(mFilterAll, "AllButton");
        getWidget(mFilterWeapon, "WeaponButton");
        getWidget(mFilterApparel, "ApparelButton");
        getWidget(mFilterMagic, "MagicButton");
        getWidget(mFilterMisc, "MiscButton");
        getWidget(mFilterKeys, "KeysButton");
        getWidget(mFilterEdit, "FilterEdit");
        getWidget(mEncumbranceBar, "EncumbranceBar");
        getWidget(mBottomBar, "BottomBar");

        getWidget(mItemView, "ItemView");
        mItemView->setExtendedMode(true);
        mItemView->setSingleClickActionEnabled(true);
        mItemView->setInternalViewModeButtonVisible(false);
        mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &ContainerWindow::onBackgroundSelected);
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &ContainerWindow::onItemSelected);
        mItemView->eventItemDragStarted += MyGUI::newDelegate(this, &ContainerWindow::onItemDragStarted);
        mItemView->eventItemDoubleClicked += MyGUI::newDelegate(this, &ContainerWindow::onItemDoubleClicked);

        mViewModeButton = mBottomBar->createWidget<MyGUI::Button>("MW_Button",
            MyGUI::IntCoord(126, 2, 30, 24), MyGUI::Align::Left | MyGUI::Align::Top, "ContainerViewModeButton");
        mViewModeButton->setCaption("");
        mViewModeIcon = mViewModeButton->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(6, 3, 18, 18), MyGUI::Align::Center, "ContainerViewModeIcon");
        mViewModeIcon->setNeedMouseFocus(false);
        mViewModeIcon->setColour(MyGUI::Colour(0.93f, 0.82f, 0.58f));
        mViewModeIcon->setImageTexture("icons/inventoryextender/Base/view_grid.dds");

        mFilterAll->setStateSelected(true);
        mFilterAll->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterWeapon->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterApparel->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterMagic->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterMisc->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterKeys->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mViewModeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onViewModeClicked);
        mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onCloseButtonClicked);
        mTakeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onTakeAllButtonClicked);
        mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &ContainerWindow::onNameFilterChanged);

        updateBottomBarLayout();

        setCoord(160, 20, 680, 380);
    }


    void ContainerWindow::onItemSelected(int index)
    {
        if (!mSortModel || !mModel || index < 0 || index >= static_cast<int>(mSortModel->getItemCount()))
            return;

        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            dropItem();
            return;
        }

        const ItemStack& item = mSortModel->getItem(index);

        // We can't take a conjured item from a container (some NPC we're pickpocketing, a box, etc)
        if (item.mFlags & ItemStack::Flag_Bound)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sContentsMessage1}");
            return;
        }

        mSelectedItem = mSortModel->mapToSource(index);
        const int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;

        // A clean click is a quick transfer to the player. The multiplayer
        // container remains server-authoritative: request the same DRAG packet
        // as normal drag-and-drop and auto-release only after the server echo.
        requestDrag(count, MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getItemView());
    }

    void ContainerWindow::onItemDragStarted(int index)
    {
        if (!mSortModel || !mModel || mDragAndDrop->mIsOnDragAndDrop)
            return;

        const ItemStack& item = mSortModel->getItem(index);
        if (item.mFlags & ItemStack::Flag_Bound)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sContentsMessage1}");
            return;
        }

        mSelectedItem = mSortModel->mapToSource(index);
        const int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;

        // Do not start a local drag here. The container is server-authoritative:
        // the echoed ID_CONTAINER/DRAG will call dragItemByPtr().
        requestDrag(count, nullptr);
    }

    void ContainerWindow::onItemDoubleClicked(int index)
    {
        // One-click transfer already queued the server-authoritative DRAG.
        // Ignore MyGUI's follow-up double-click event to avoid a duplicate request.
        (void)index;
    }

    bool ContainerWindow::requestDrag(int count, ItemView* pendingTarget)
    {
        if (!mModel || mSelectedItem < 0 || mSelectedItem >= static_cast<int>(mModel->getItemCount()))
            return false;
        if (mDragAndDrop->isServerDragPending())
            return false;

        if (!onTakeItem(mModel->getItem(mSelectedItem), count))
            return false;

        mDragAndDrop->beginServerDrag(mItemView);
        if (pendingTarget)
            mDragAndDrop->queuePendingServerDropTarget(pendingTarget);

        mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->cell = *mPtr.getCell()->getCell();
        objectList->action = mwmp::BaseObjectList::REMOVE;
        objectList->containerSubAction = mwmp::BaseObjectList::DRAG;

        mwmp::BaseObject baseObject = objectList->getBaseObjectFromPtr(mPtr);
        MWWorld::Ptr itemPtr = mModel->getItem(mSelectedItem).mBase;
        objectList->addContainerItem(baseObject, itemPtr, itemPtr.getRefData().getCount(), count);
        objectList->addBaseObject(baseObject);
        objectList->sendContainer();
        return true;
    }

    void ContainerWindow::dragItem(MyGUI::Widget* sender, int count)
    {
        (void)sender;
        requestDrag(count, nullptr);
    }

    void ContainerWindow::dropItem()
    {
        if (!mModel)
            return;

        bool success = mModel->onDropItem(mDragAndDrop->mItem.mBase, mDragAndDrop->mDraggedCount);

        /*
            Start of tes3mp addition

            Send an ID_CONTAINER packet every time an item is dropped in a container
        */
        if (success)
        {
            mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->cell = *mPtr.getCell()->getCell();
            objectList->action = mwmp::BaseObjectList::ADD;
            objectList->containerSubAction = mwmp::BaseObjectList::DROP;

            mwmp::BaseObject baseObject = objectList->getBaseObjectFromPtr(mPtr);
            MWWorld::Ptr itemPtr = mDragAndDrop->mItem.mBase;
            objectList->addContainerItem(baseObject, itemPtr, mDragAndDrop->mDraggedCount, 0);
            objectList->addBaseObject(baseObject);
            objectList->sendContainer();
        }
        /*
            End of tes3mp addition
        */

        /*
            Start of tes3mp change (major)

            For valid drops, avoid running the original code for the item transfer, to prevent unilateral
            item duping or interaction on this client

            Instead, finish the drag in a way that removes the items in it, and let the server's reply handle
            the rest
        */
        if (success)
            // mDragAndDrop->drop(mModel, mItemView);
            mDragAndDrop->finish(true);
        /*
            End of tes3mp change (major)
        */
    }

    void ContainerWindow::onBackgroundSelected()
    {
        if (!mDragAndDrop->mIsOnDragAndDrop)
        {
            // A server-approved drag may still be in flight. Remember that the
            // user released over the source container so the eventual echo can
            // immediately send the item back instead of leaving a ghost drag.
            mDragAndDrop->queuePendingServerDropTarget(mItemView);
            return;
        }

        dropItem();
    }

    void ContainerWindow::setPtr(const MWWorld::Ptr& container)
    {
        /*
            Start of tes3mp addition

            Mark this container as open for multiplayer logic purposes
        */
        mwmp::Main::get().getLocalPlayer()->storeCurrentContainer(container);
        /*
            End of tes3mp addition
        */

        mPtr = container;

        bool loot = mPtr.getClass().isActor() && mPtr.getClass().getCreatureStats(mPtr).isDead();

        if (mPtr.getClass().hasInventoryStore(mPtr))
        {
            if (mPtr.getClass().isNpc() && !loot)
            {
                // we are stealing stuff
                mModel = new PickpocketItemModel(mPtr, new InventoryItemModel(container),
                                                 !mPtr.getClass().getCreatureStats(mPtr).getKnockedDown());
            }
            else
                mModel = new InventoryItemModel(container);
        }
        else
        {
            mModel = new ContainerItemModel(container);
        }

        mSortModel = new SortFilterItemModel(mModel);
        mFilterEdit->setCaption("");
        mSortModel->setNameFilter("");
        mSortModel->setCategory(SortFilterItemModel::Category_All);
        mFilterAll->setStateSelected(true);
        mFilterWeapon->setStateSelected(false);
        mFilterApparel->setStateSelected(false);
        mFilterMagic->setStateSelected(false);
        mFilterMisc->setStateSelected(false);
        mFilterKeys->setStateSelected(false);

        mItemView->setModel (mSortModel);
        mItemView->setViewMode(ItemView::View_List);
        if (mViewModeButton)
            mViewModeButton->setStateSelected(true);
        if (mViewModeIcon)
            mViewModeIcon->setImageTexture("icons/inventoryextender/Base/view_grid.dds");
        updateBottomBarLayout();
        mItemView->resetScrollBars();
        mDragAndDrop->setTransferTargetView(mItemView);

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mItemView);

        setTitle(container.getClass().getName(container));
        updateEncumbranceBar();
    }

    void ContainerWindow::resetReference()
    {
        mDragAndDrop->cancelPendingServerDrag(mItemView);
        mDragAndDrop->clearTransferTargetView(mItemView);
        ReferenceInterface::resetReference();
        mItemView->setModel(nullptr);
        mModel = nullptr;
        mSortModel = nullptr;
    }

    void ContainerWindow::onFrame(float dt)
    {
        (void)dt;
        checkReferenceAvailable();
        if (!mPtr.isEmpty())
            updateEncumbranceBar();
        updateBottomBarLayout();
    }

    void ContainerWindow::onClose()
    {
        /*
            Start of tes3mp addition

            Mark this container as closed for multiplayer logic purposes
        */
        mwmp::Main::get().getLocalPlayer()->clearCurrentContainer();
        /*
            End of tes3mp addition
        */

        WindowBase::onClose();

        // Make sure the window was actually closed and not temporarily hidden.
        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Container))
            return;

        if (mModel)
            mModel->onClose();

        if (!mPtr.isEmpty())
            MWBase::Environment::get().getMechanicsManager()->onClose(mPtr);
        resetReference();
    }

    void ContainerWindow::onCloseButtonClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::onTakeAllButtonClicked(MyGUI::Widget* _sender)
    {
        if(mDragAndDrop != nullptr && mDragAndDrop->mIsOnDragAndDrop)
            return;

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);

        /*
            Start of tes3mp addition

            Send an ID_CONTAINER packet every time the Take All button is used on
            a container
        */
        mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->cell = *mPtr.getCell()->getCell();
        objectList->action = mwmp::BaseObjectList::REMOVE;
        objectList->containerSubAction = mwmp::BaseObjectList::TAKE_ALL;
        mwmp::BaseObject baseObject = objectList->getBaseObjectFromPtr(mPtr);

        for (size_t i = 0; i < mModel->getItemCount(); ++i)
        {
            const ItemStack& item = mModel->getItem(i);

            // Trigger crimes related to the attempted taking of these items, if applicable
            if (!onTakeItem(item, item.mCount))
                break;

            objectList->addContainerItem(baseObject, item, item.mCount, item.mCount);
        }

        if (baseObject.containerItems.size() > 0)
        {
            objectList->addBaseObject(baseObject);
            objectList->sendContainer();
        }
        /*
            End of tes3mp addition
        */

        /*
            Start of tes3mp change (major)

            Avoid running any of the original code for taking all items, to prevent
            possibilities for item duping or interaction with restricted containers
        */
        return;
        /*
            End of tes3mp change (major)
        */

        // transfer everything into the player's inventory
        ItemModel* playerModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getModel();
        assert(mModel);
        mModel->update();

        // unequip all items to avoid unequipping/reequipping
        if (mPtr.getClass().hasInventoryStore(mPtr))
        {
            MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
            for (size_t i=0; i<mModel->getItemCount(); ++i)
            {
                const ItemStack& item = mModel->getItem(i);
                if (invStore.isEquipped(item.mBase) == false)
                    continue;

                invStore.unequipItem(item.mBase, mPtr);
            }
        }

        mModel->update();

        for (size_t i=0; i<mModel->getItemCount(); ++i)
        {
            if (i==0)
            {
                // play the sound of the first object
                MWWorld::Ptr item = mModel->getItem(i).mBase;
                std::string sound = item.getClass().getUpSoundId(item);
                MWBase::Environment::get().getWindowManager()->playSound(sound);
            }

            const ItemStack& item = mModel->getItem(i);

            if (!onTakeItem(item, item.mCount))
                break;

            mModel->moveItem(item, item.mCount, playerModel);
        }

        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::onDisposeCorpseButtonClicked(MyGUI::Widget *sender)
    {
        if(mDragAndDrop == nullptr || !mDragAndDrop->mIsOnDragAndDrop)
        {
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);

            // Copy mPtr because onTakeAllButtonClicked closes the window which resets the reference
            MWWorld::Ptr ptr = mPtr;
            onTakeAllButtonClicked(mTakeButton);
            
            if (ptr.getClass().isPersistent(ptr))
                MWBase::Environment::get().getWindowManager()->messageBox("#{sDisposeCorpseFail}");
            else
            {
                /*
                    Start of tes3mp change (major)

                    Instead of deleting the corpse on this client, increasing the death count and
                    running the dead actor's script, simply send an ID_OBJECT_DELETE packet to the server
                    as a request for the deletion
                */

                /*
                MWMechanics::CreatureStats& creatureStats = ptr.getClass().getCreatureStats(ptr);

                // If we dispose corpse before end of death animation, we should update death counter counter manually.
                // Also we should run actor's script - it may react on actor's death.
                if (creatureStats.isDead() && !creatureStats.isDeathAnimationFinished())
                {
                    creatureStats.setDeathAnimationFinished(true);
                    MWBase::Environment::get().getMechanicsManager()->notifyDied(ptr);

                    const std::string script = ptr.getClass().getScript(ptr);
                    if (!script.empty() && MWBase::Environment::get().getWorld()->getScriptsEnabled())
                    {
                        MWScript::InterpreterContext interpreterContext (&ptr.getRefData().getLocals(), ptr);
                        MWBase::Environment::get().getScriptManager()->run (script, interpreterContext);
                    }

                    // Clean up summoned creatures as well
                    std::map<ESM::SummonKey, int>& creatureMap = creatureStats.getSummonedCreatureMap();
                    for (const auto& creature : creatureMap)
                        MWBase::Environment::get().getMechanicsManager()->cleanupSummonedCreature(ptr, creature.second);
                    creatureMap.clear();

                    // Check if we are a summon and inform our master we've bit the dust
                    for(const auto& package : creatureStats.getAiSequence())
                    {
                        if(package->followTargetThroughDoors() && !package->getTarget().isEmpty())
                        {
                            const auto& summoner = package->getTarget();
                            auto& summons = summoner.getClass().getCreatureStats(summoner).getSummonedCreatureMap();
                            auto it = std::find_if(summons.begin(), summons.end(), [&] (const auto& entry) { return entry.second == creatureStats.getActorId(); });
                            if(it != summons.end())
                            {
                                MWMechanics::purgeSummonEffect(summoner, *it);
                                summons.erase(it);
                                break;
                            }
                        }
                    }
                }

                MWBase::Environment::get().getWorld()->deleteObject(ptr);
                */

                mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
                objectList->reset();
                objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                objectList->addObjectGeneric(ptr);
                objectList->sendObjectDelete();
                /*
                    End of tes3mp change (major)
                */
            }
        }
    }

    void ContainerWindow::onReferenceUnavailable()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::onNameFilterChanged(MyGUI::EditBox* sender)
    {
        if (!mSortModel)
            return;

        mSortModel->setNameFilter(sender->getCaption());
        mItemView->update();
        mItemView->resetScrollBars();
    }

    void ContainerWindow::onFilterChanged(MyGUI::Widget* sender)
    {
        if (!mSortModel)
            return;

        if (sender == mFilterAll)
            mSortModel->setCategory(SortFilterItemModel::Category_All);
        else if (sender == mFilterWeapon)
            mSortModel->setCategory(SortFilterItemModel::Category_Weapon);
        else if (sender == mFilterApparel)
            mSortModel->setCategory(SortFilterItemModel::Category_Apparel);
        else if (sender == mFilterMagic)
            mSortModel->setCategory(SortFilterItemModel::Category_Magic);
        else if (sender == mFilterMisc)
            mSortModel->setCategory(SortFilterItemModel::Category_Misc);
        else if (sender == mFilterKeys)
            mSortModel->setCategory(SortFilterItemModel::Category_Keys);

        mFilterAll->setStateSelected(sender == mFilterAll);
        mFilterWeapon->setStateSelected(sender == mFilterWeapon);
        mFilterApparel->setStateSelected(sender == mFilterApparel);
        mFilterMagic->setStateSelected(sender == mFilterMagic);
        mFilterMisc->setStateSelected(sender == mFilterMisc);
        mFilterKeys->setStateSelected(sender == mFilterKeys);
        mItemView->update();
        mItemView->resetScrollBars();
    }

    void ContainerWindow::onViewModeClicked(MyGUI::Widget* sender)
    {
        (void)sender;
        if (!mItemView)
            return;

        ItemView::ViewMode mode = mItemView->getViewMode();
        mItemView->setViewMode(mode == ItemView::View_List ? ItemView::View_Grid : ItemView::View_List);

        if (mViewModeButton)
            mViewModeButton->setStateSelected(mItemView->getViewMode() == ItemView::View_List);
        if (mViewModeIcon)
            mViewModeIcon->setImageTexture(std::string("icons/inventoryextender/Base/")
                + (mItemView->getViewMode() == ItemView::View_List ? "view_grid.dds" : "view_table.dds"));
    }

    void ContainerWindow::updateBottomBarLayout()
    {
        if (!mBottomBar || !mEncumbranceBar || !mFilterEdit || !mViewModeButton || !mTakeButton || !mCloseButton)
            return;

        const int width = mBottomBar->getWidth();
        const int gap = 6;
        const int encWidth = std::max(90, std::min(120, width / 5));
        const int buttonW = 30;
        const int closeW = std::max(48, mCloseButton->getWidth());
        const int takeW = std::max(86, mTakeButton->getWidth());

        int x = 0;
        mEncumbranceBar->setCoord(x, 2, encWidth, 24);
        x += encWidth + gap;

        mViewModeButton->setCoord(x, 2, buttonW, 24);
        x += buttonW + gap;

        const int rightButtonsWidth = takeW + gap + closeW;
        const int filterWidth = std::max(80, width - x - gap - rightButtonsWidth);
        mFilterEdit->setCoord(x, 2, filterWidth, 24);
        x += filterWidth + gap;

        mTakeButton->setCoord(x, 2, takeW, 24);
        x += takeW + gap;
        mCloseButton->setCoord(x, 2, closeW, 24);
    }

    void ContainerWindow::updateEncumbranceBar()
    {
        if (mPtr.isEmpty() || !mEncumbranceBar)
            return;

        float capacity = mPtr.getClass().getCapacity(mPtr);
        float encumbrance = mPtr.getClass().getEncumbrance(mPtr);
        mEncumbranceBar->setValue(std::ceil(encumbrance), static_cast<int>(capacity));
    }

    bool ContainerWindow::onTakeItem(const ItemStack &item, int count)
    {
        return mModel->onTakeItem(item.mBase, count);
    }

    /*
        Start of tes3mp addition

        Make it possible to check from elsewhere whether there is currently an
        item being dragged in the container window
    */
    bool ContainerWindow::isOnDragAndDrop()
    {
        return mDragAndDrop->mIsOnDragAndDrop;
    }
    /*
        End of tes3mp addition
    */

    /*
        Start of tes3mp addition

        Make it possible to drag a specific item Ptr instead of having to rely
        on an index that may have changed in the meantime, for drags that
        require approval from the server
    */
    bool ContainerWindow::dragItemByPtr(const MWWorld::Ptr& itemPtr, int dragCount)
    {
        ItemModel::ModelIndex newIndex = -1;
        for (unsigned int i = 0; i < mModel->getItemCount(); ++i)
        {
            if (mModel->getItem(i).mBase == itemPtr)
            {
                newIndex = i;
                break;
            }
        }

        if (newIndex != -1)
        {
            // If the mouse was released before the server echoed our DRAG, keep
            // that release target and complete it immediately after creating the
            // approved drag. If the echo arrived first, target is null and the
            // normal MouseButtonReleased path handles it.
            ItemView* pendingTarget = mDragAndDrop->takePendingServerDropTarget(mItemView);
            mDragAndDrop->startDrag(newIndex, mSortModel, mModel, mItemView, dragCount);
            if (pendingTarget)
                pendingTarget->eventBackgroundClicked();
            return true;
        }

        return false;
    }
    /*
        End of tes3mp addition
    */
}
