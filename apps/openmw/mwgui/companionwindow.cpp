#include "companionwindow.hpp"

#include <algorithm>
#include <cmath>

#include <MyGUI_InputManager.h>


#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/LocalActor.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"

#include "messagebox.hpp"
#include "inventorywindow.hpp"
#include "itemview.hpp"
#include "sortfilteritemmodel.hpp"
#include "companionitemmodel.hpp"
#include "draganddrop.hpp"
#include "countdialog.hpp"
#include "widgets.hpp"
#include "tooltips.hpp"

namespace
{

    int getProfit(const MWWorld::Ptr& actor)
    {
        std::string script = actor.getClass().getScript(actor);
        if (!script.empty())
        {
            return actor.getRefData().getLocals().getIntVar(script, "minimumprofit");
        }
        return 0;
    }

}

namespace MWGui
{

CompanionWindow::CompanionWindow(DragAndDrop *dragAndDrop, MessageBoxManager* manager)
    : WindowBase("openmw_companion_window.layout")
    , mSortModel(nullptr)
    , mModel(nullptr)
    , mSelectedItem(-1)
    , mDragAndDrop(dragAndDrop)
    , mMessageBoxManager(manager)
{
    getWidget(mCloseButton, "CloseButton");
    getWidget(mFilterAll, "AllButton");
    getWidget(mFilterWeapon, "WeaponButton");
    getWidget(mFilterApparel, "ApparelButton");
    getWidget(mFilterMagic, "MagicButton");
    getWidget(mFilterMisc, "MiscButton");
    getWidget(mProfitLabel, "ProfitLabel");
    getWidget(mEncumbranceBar, "EncumbranceBar");
    getWidget(mFilterEdit, "FilterEdit");
    getWidget(mItemView, "ItemView");
    mItemView->setExtendedMode(true);
    mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &CompanionWindow::onBackgroundSelected);
    mItemView->eventItemClicked += MyGUI::newDelegate(this, &CompanionWindow::onItemSelected);
    mItemView->eventItemDragStarted += MyGUI::newDelegate(this, &CompanionWindow::onItemDragStarted);
    mItemView->eventItemDoubleClicked += MyGUI::newDelegate(this, &CompanionWindow::onItemDoubleClicked);
    mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &CompanionWindow::onNameFilterChanged);
    mFilterAll->setStateSelected(true);
    mFilterAll->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);
    mFilterWeapon->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);
    mFilterApparel->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);
    mFilterMagic->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);
    mFilterMisc->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);

    mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onCloseButtonClicked);

    setCoord(160, 20, 680, 380);
}

void CompanionWindow::onItemSelected(int index)
{
    if (mDragAndDrop->mIsOnDragAndDrop)
    {
        dropItem();
        return;
    }

    const ItemStack& item = mSortModel->getItem(index);

    // We can't take conjured items from a companion NPC
    if (item.mFlags & ItemStack::Flag_Bound)
    {
        MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
        return;
    }

    MWWorld::Ptr object = item.mBase;
    int count = item.mCount;
    bool shift = MyGUI::InputManager::getInstance().isShiftPressed();
    if (MyGUI::InputManager::getInstance().isControlPressed())
        count = 1;

    mSelectedItem = mSortModel->mapToSource(index);

    if (count > 1 && !shift)
    {
        CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
        std::string name = object.getClass().getName(object) + MWGui::ToolTips::getSoulString(object.getCellRef());
        dialog->openCountDialog(name, "#{sTake}", count);
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &CompanionWindow::dragItem);
    }
    else
        dragItem (nullptr, count);
}

void CompanionWindow::onNameFilterChanged(MyGUI::EditBox* _sender)
    {
        mSortModel->setNameFilter(_sender->getCaption());
        mItemView->update();
    }

void CompanionWindow::onFilterChanged(MyGUI::Widget* sender)
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

    mFilterAll->setStateSelected(sender == mFilterAll);
    mFilterWeapon->setStateSelected(sender == mFilterWeapon);
    mFilterApparel->setStateSelected(sender == mFilterApparel);
    mFilterMagic->setStateSelected(sender == mFilterMagic);
    mFilterMisc->setStateSelected(sender == mFilterMisc);
    mItemView->update();
    mItemView->resetScrollBars();
}

void CompanionWindow::onItemDragStarted(int index)
{
    if (!mSortModel || !mModel || mDragAndDrop->mIsOnDragAndDrop)
        return;

    const ItemStack& item = mSortModel->getItem(index);
    if (item.mFlags & ItemStack::Flag_Bound)
    {
        MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
        return;
    }

    mSelectedItem = mSortModel->mapToSource(index);
    const int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;

    // Companion inventories are cell containers in TES3MP. Wait for the
    // server's ID_CONTAINER/DRAG echo before creating the floating item.
    requestDrag(count, nullptr);
}

void CompanionWindow::onItemDoubleClicked(int index)
{
    if (!mSortModel || !mModel || mDragAndDrop->mIsOnDragAndDrop)
        return;

    const ItemStack& item = mSortModel->getItem(index);
    if (item.mFlags & ItemStack::Flag_Bound)
    {
        MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
        return;
    }

    mSelectedItem = mSortModel->mapToSource(index);
    const int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;

    // Double click is a server-approved quick transfer to player inventory.
    requestDrag(count, MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getItemView());
}

bool CompanionWindow::requestDrag(int count, ItemView* pendingTarget)
{
    if (!mModel || mPtr.isEmpty() || mSelectedItem < 0 || mSelectedItem >= static_cast<int>(mModel->getItemCount()))
        return false;
    if (mDragAndDrop->isServerDragPending())
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

void CompanionWindow::dragItem(MyGUI::Widget* sender, int count)
{
    (void)sender;
    requestDrag(count, nullptr);
}

void CompanionWindow::dropItem()
{
    if (!mModel || mPtr.isEmpty() || !mDragAndDrop->mIsOnDragAndDrop)
        return;

    // Mirror ContainerWindow's TES3MP flow: never mutate the companion locally
    // before the server has accepted the ADD. Remove the optimistic player copy
    // and let the echoed container packet rebuild the authoritative actor store.
    mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
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

    mDragAndDrop->finish(true);
    updateEncumbranceBar();
}

void CompanionWindow::onBackgroundSelected()
{
    if (!mDragAndDrop->mIsOnDragAndDrop)
    {
        // Preserve a release that happened while ID_CONTAINER/DRAG was in flight.
        mDragAndDrop->queuePendingServerDropTarget(mItemView);
        return;
    }

    dropItem();
}

bool CompanionWindow::isOnDragAndDrop() const
{
    return mDragAndDrop->mIsOnDragAndDrop;
}

bool CompanionWindow::dragItemByPtr(const MWWorld::Ptr& itemPtr, int dragCount)
{
    if (!mModel || !mSortModel)
        return false;

    ItemModel::ModelIndex newIndex = -1;
    for (unsigned int i = 0; i < mModel->getItemCount(); ++i)
    {
        if (mModel->getItem(i).mBase == itemPtr)
        {
            newIndex = i;
            break;
        }
    }

    if (newIndex == -1)
        return false;

    ItemView* pendingTarget = mDragAndDrop->takePendingServerDropTarget(mItemView);
    mDragAndDrop->startDrag(newIndex, mSortModel, mModel, mItemView, dragCount);
    syncCompanionEquipment();
    updateEncumbranceBar();

    if (pendingTarget)
        pendingTarget->eventBackgroundClicked();
    return true;
}

void CompanionWindow::setPtr(const MWWorld::Ptr& npc)
{
    // Reuse TES3MP's current-container identity for actor inventory packets.
    mwmp::Main::get().getLocalPlayer()->storeCurrentContainer(npc);
    mPtr = npc;
    updateEncumbranceBar();

    mModel = new CompanionItemModel(npc);
    mSortModel = new SortFilterItemModel(mModel);
    mFilterEdit->setCaption(std::string());
    mSortModel->setCategory(SortFilterItemModel::Category_All);
    mFilterAll->setStateSelected(true);
    mFilterWeapon->setStateSelected(false);
    mFilterApparel->setStateSelected(false);
    mFilterMagic->setStateSelected(false);
    mFilterMisc->setStateSelected(false);
    mItemView->setModel(mSortModel);
    mItemView->resetScrollBars();
    mDragAndDrop->setTransferTargetView(mItemView);

    setTitle(npc.getClass().getName(npc));
}

void CompanionWindow::onFrame(float dt)
{
    checkReferenceAvailable();
    updateEncumbranceBar();
}

void CompanionWindow::syncCompanionEquipment()
{
    if (mPtr.isEmpty())
        return;

    mwmp::CellController* controller = mwmp::Main::get().getCellController();
    if (!controller || !controller->isLocalActor(mPtr))
        return;

    if (mwmp::LocalActor* actor = controller->getLocalActor(mPtr))
        actor->updateEquipment(true, true);
}

void CompanionWindow::updateEncumbranceBar()
{
    if (mPtr.isEmpty())
        return;
    float capacity = mPtr.getClass().getCapacity(mPtr);
    float encumbrance = mPtr.getClass().getEncumbrance(mPtr);
    mEncumbranceBar->setValue(std::ceil(encumbrance), static_cast<int>(capacity));

    if (mModel && mModel->hasProfit(mPtr))
    {
        mProfitLabel->setCaptionWithReplacing("#{sProfitValue} " + MyGUI::utility::toString(getProfit(mPtr)));
    }
    else
        mProfitLabel->setCaption("");
}

void CompanionWindow::onCloseButtonClicked(MyGUI::Widget* _sender)
{
    if (exit())
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Companion);
}

bool CompanionWindow::exit()
{
    if (mModel && mModel->hasProfit(mPtr) && getProfit(mPtr) < 0)
    {
        std::vector<std::string> buttons;
        buttons.emplace_back("#{sCompanionWarningButtonOne}");
        buttons.emplace_back("#{sCompanionWarningButtonTwo}");
        mMessageBoxManager->createInteractiveMessageBox("#{sCompanionWarningMessage}", buttons);
        mMessageBoxManager->eventButtonPressed += MyGUI::newDelegate(this, &CompanionWindow::onMessageBoxButtonClicked);
        return false;
    }
    return true;
}

void CompanionWindow::onMessageBoxButtonClicked(int button)
{
    if (button == 0)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Companion);
        // Important for Calvus' contract script to work properly
        MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
    }
}

void CompanionWindow::onReferenceUnavailable()
{
    MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Companion);
}

void CompanionWindow::resetReference()
{
    mDragAndDrop->cancelPendingServerDrag(mItemView);
    mDragAndDrop->clearTransferTargetView(mItemView);
    ReferenceInterface::resetReference();
    mItemView->setModel(nullptr);
    mModel = nullptr;
    mSortModel = nullptr;
}


}
