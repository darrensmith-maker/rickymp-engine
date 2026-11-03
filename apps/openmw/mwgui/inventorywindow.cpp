#include "inventorywindow.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <MyGUI_Window.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Gui.h>

#include <osg/Texture2D>

#include <components/misc/stringops.hpp>
#include <components/esm/loadmisc.hpp>

#include <components/myguiplatform/myguitexture.hpp>

#include <components/settings/settings.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwworld/cellstore.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/world.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/inventorystore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/actionequip.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/animationenhancements.hpp"
#include "../mwmechanics/creaturestats.hpp"

#include "itemview.hpp"
#include "inventoryitemmodel.hpp"
#include "sortfilteritemmodel.hpp"
#include "tradeitemmodel.hpp"
#include "countdialog.hpp"
#include "tradewindow.hpp"
#include "draganddrop.hpp"
#include "widgets.hpp"
#include "tooltips.hpp"

namespace
{

    std::string arenaText(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    bool isRightHandWeapon(const MWWorld::Ptr& item)
    {
        if (item.getClass().getTypeName() != typeid(ESM::Weapon).name())
            return false;
        std::vector<int> equipmentSlots = item.getClass().getEquipmentSlots(item).first;
        return (!equipmentSlots.empty() && equipmentSlots.front() == MWWorld::InventoryStore::Slot_CarriedRight);
    }

    bool isVirtualKeyRing(const MWGui::ItemStack& item)
    {
        return item.mCreator == nullptr && !item.mBase.isEmpty()
            && item.mBase.getClass().isKey(item.mBase);
    }

}

namespace MWGui
{

    InventoryWindow::InventoryWindow(DragAndDrop* dragAndDrop, osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : WindowPinnableBase("openmw_inventory_window.layout")
        , mDragAndDrop(dragAndDrop)
        , mSelectedItem(-1)
        , mSortModel(nullptr)
        , mTradeModel(nullptr)
        , mCategories(nullptr)
        , mBottomBar(nullptr)
        , mPaperDollButton(nullptr)
        , mPaperDollIcon(nullptr)
        , mViewModeButton(nullptr)
        , mWriterButton(nullptr)
        , mWriterIcon(nullptr)
        , mViewModeIcon(nullptr)
        , mPaperDollVisible(false)
        , mFilterKeys(nullptr)
        , mKeyRingPanel(nullptr)
        , mKeyRingTitle(nullptr)
        , mKeyRingWeight(nullptr)
        , mKeyRingList(nullptr)
        , mKeyRingOpen(false)
        , mKeyRingUpdateTimer(0.f)
        , mGuiMode(GM_Inventory)
        , mLastXSize(0)
        , mLastYSize(0)
        , mPreview(new MWRender::InventoryPreview(parent, resourceSystem, MWMechanics::getPlayer()))
        , mTrading(false)
        , mUpdateTimer(0.f)
    {
        mPreviewTexture.reset(new osgMyGUI::OSGTexture(mPreview->getTexture()));
        mPreview->rebuild();

        mMainWidget->castType<MyGUI::Window>()->eventWindowChangeCoord += MyGUI::newDelegate(this, &InventoryWindow::onWindowResize);

        getWidget(mAvatar, "Avatar");
        getWidget(mAvatarImage, "AvatarImage");
        getWidget(mEncumbranceBar, "EncumbranceBar");
        getWidget(mFilterAll, "AllButton");
        getWidget(mFilterWeapon, "WeaponButton");
        getWidget(mFilterApparel, "ApparelButton");
        getWidget(mFilterMagic, "MagicButton");
        getWidget(mFilterMisc, "MiscButton");
        getWidget(mFilterKeys, "KeysButton");
        getWidget(mLeftPane, "LeftPane");
        getWidget(mRightPane, "RightPane");
        getWidget(mCategories, "Categories");
        getWidget(mBottomBar, "BottomBar");
        getWidget(mArmorRating, "ArmorRating");
        getWidget(mFilterEdit, "FilterEdit");

        // The OpenMW 0.51 Inventory Extender has no vanilla character preview,
        // so its table can use the complete window width.  ArenaMW can render
        // the paper doll natively; expose it as an optional layout pane instead
        // of permanently sacrificing half of the table.  Hidden is the default
        // to match the original mod, and the choice persists in settings.
        mPaperDollVisible = Settings::Manager::getBool("inventory paper doll", "GUI");
        mPaperDollButton = mBottomBar->createWidget<MyGUI::Button>("MW_Button",
            MyGUI::IntCoord(0, 2, 28, 26), MyGUI::Align::Left | MyGUI::Align::VCenter, "ArenaPaperDollToggle");
        mPaperDollButton->setCaption("");
        mPaperDollButton->setNeedKeyFocus(false);
        mPaperDollButton->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onPaperDollClicked);
        mPaperDollIcon = mPaperDollButton->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(3, 1, 24, 24), MyGUI::Align::Center, "ArenaPaperDollToggleIcon");
        mPaperDollIcon->setNeedMouseFocus(false);
        mPaperDollButton->setStateSelected(mPaperDollVisible);
        refreshPaperDollToggleVisual();

        // ArenaMW virtual key ring. The ring is injected into ItemView by
        // SortFilterItemModel as a synthetic inventory row (first real key icon
        // + total key count). The real keys remain in InventoryStore, so doors,
        // scripts and encumbrance retain vanilla semantics. This floating panel
        // is only the expanded contents view opened from that virtual row.
        // It is intentionally parented to the inventory window itself so it
        // appears as an overlay above the item list rather than consuming any
        // permanent space inside the pane.
        mKeyRingPanel = mMainWidget->createWidget<MyGUI::Widget>("HUD_Box",
            MyGUI::IntCoord(0, 0, 320, 210), MyGUI::Align::Default, "ArenaKeyRingPanel");
        mKeyRingTitle = mKeyRingPanel->createWidget<MyGUI::TextBox>("SandBrightText",
            MyGUI::IntCoord(12, 8, 185, 22), MyGUI::Align::Left | MyGUI::Align::Top, "ArenaKeyRingTitle");
        mKeyRingTitle->setCaption(arenaText("keyring.title"));
        mKeyRingTitle->setNeedMouseFocus(false);
        mKeyRingWeight = mKeyRingPanel->createWidget<MyGUI::TextBox>("SandTextRight",
            MyGUI::IntCoord(210, 8, 98, 22), MyGUI::Align::Right | MyGUI::Align::Top, "ArenaKeyRingWeight");
        mKeyRingWeight->setNeedMouseFocus(false);
        mKeyRingList = mKeyRingPanel->createWidget<MyGUI::ScrollView>("MW_ScrollView",
            MyGUI::IntCoord(10, 34, 300, 166), MyGUI::Align::Stretch, "ArenaKeyRingList");
        mKeyRingList->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        mKeyRingList->setVisibleHScroll(false);
        mKeyRingList->setNeedMouseFocus(false);
        mKeyRingPanel->setVisible(false);

        mViewModeButton = mBottomBar->createWidget<MyGUI::Button>("MW_Button",
            MyGUI::IntCoord(0, 2, 28, 26), MyGUI::Align::Left | MyGUI::Align::VCenter, "ArenaInventoryViewToggle");
        mViewModeButton->setCaption("");
        mViewModeButton->setNeedKeyFocus(false);
        mViewModeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onViewModeClicked);
        mViewModeIcon = mViewModeButton->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(5, 4, 18, 18), MyGUI::Align::Center, "ArenaInventoryViewToggleIcon");
        mViewModeIcon->setNeedMouseFocus(false);
        mViewModeIcon->setColour(MyGUI::Colour(0.93f, 0.82f, 0.58f));

        // Native writing button. Keep the click target borderless and move the
        // quill one full icon slot to the right of the view toggle in
        // updateBottomControls(), matching the requested layout.
        mWriterButton = mBottomBar->createWidget<MyGUI::Widget>("",
            MyGUI::IntCoord(0, 2, 30, 26), MyGUI::Align::Left | MyGUI::Align::VCenter, "ArenaBookWriterButton");
        mWriterButton->setNeedKeyFocus(false);
        mWriterButton->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onWriterClicked);
        mWriterIcon = mWriterButton->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(2, 0, 26, 26), MyGUI::Align::Center, "ArenaBookWriterIcon");
        mWriterIcon->setNeedMouseFocus(false);
        mWriterIcon->setImageTexture("textures/ui/arenamw/quill_white.png");
        refreshWriterButtonVisual();

        // Barter gold uses the same visual language as an inventory stack:
        // the amount is drawn directly over the coin icon instead of taking a
        // separate text column to its right.
        mGoldIcon = mBottomBar->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(0, 1, 30, 30), MyGUI::Align::Left | MyGUI::Align::VCenter, "ArenaInventoryGoldIcon");
        mGoldIcon->setNeedMouseFocus(false);
        mGoldLabel = mBottomBar->createWidget<MyGUI::TextBox>("CountText",
            MyGUI::IntCoord(0, 1, 30, 30), MyGUI::Align::Left | MyGUI::Align::VCenter, "ArenaInventoryGoldLabel");
        mGoldLabel->setNeedMouseFocus(false);
        mGoldLabel->setTextAlign(MyGUI::Align::Right | MyGUI::Align::Bottom);
        mGoldLabel->setTextShadow(true);
        mGoldLabel->setTextShadowColour(MyGUI::Colour::Black);

        mAvatarImage->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onAvatarClicked);
        mAvatarImage->setRenderItemTexture(mPreviewTexture.get());
        mAvatarImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));

        getWidget(mItemView, "ItemView");
        mItemView->setExtendedMode(true);
        mItemView->setInternalViewModeButtonVisible(false);
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &InventoryWindow::onItemSelected);
        mItemView->eventItemDragStarted += MyGUI::newDelegate(this, &InventoryWindow::onItemDragStarted);
        mItemView->eventItemDoubleClicked += MyGUI::newDelegate(this, &InventoryWindow::onItemDoubleClicked);
        mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &InventoryWindow::onBackgroundSelected);

        mFilterAll->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterWeapon->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterApparel->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterMagic->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterMisc->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterKeys->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryWindow::onFilterChanged);
        mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &InventoryWindow::onNameFilterChanged);

        mFilterAll->setStateSelected(true);

        setGuiMode(mGuiMode);

        adjustPanes();
    }

    void InventoryWindow::adjustPanes()
    {
        if (!mLeftPane || !mRightPane)
            return;

        const int mainWidth = std::max(1, mMainWidget->getWidth());
        const int mainHeight = std::max(1, mMainWidget->getHeight());
        const int paneHeight = std::max(40, mainHeight - 44);
        const int rightMargin = 23;
        const int paneGap = 4;
        int rightLeft = 4;

        const bool paperDollAvailable = (mGuiMode == GM_Inventory);
        const bool showPaperDoll = paperDollAvailable && mPaperDollVisible;

        if (showPaperDoll)
        {
            const float aspect = 0.5f;
            int leftPaneWidth = static_cast<int>((paneHeight - mArmorRating->getHeight()) * aspect);

            const int minimumTableWidth = 300;
            const int maximumPreviewWidth = std::max(80, mainWidth - minimumTableWidth - rightMargin - paneGap);
            leftPaneWidth = std::max(80, std::min(leftPaneWidth, maximumPreviewWidth));

            mLeftPane->setVisible(true);
            mLeftPane->setSize(leftPaneWidth, paneHeight);
            rightLeft = mLeftPane->getLeft() + leftPaneWidth + paneGap;
        }
        else
        {
            mLeftPane->setVisible(false);
        }

        const int rightWidth = std::max(120, mainWidth - rightLeft - rightMargin);
        mRightPane->setCoord(rightLeft, mRightPane->getTop(), rightWidth, paneHeight);

        // The top strip is intentionally reserved for categories and table
        // sorting only. Search, used weight and view/paper-doll controls live
        // in the bottom strip.
        if (mCategories)
            mCategories->setCoord(0, 6, rightWidth, 28);

        if (mPaperDollButton)
        {
            mPaperDollButton->setVisible(paperDollAvailable);
            mPaperDollButton->setEnabled(paperDollAvailable);
            mPaperDollButton->setStateSelected(showPaperDoll);
        }

        updateBottomControls();
        adjustKeyRingLayout();

        if (mItemView)
            mItemView->relayout();
    }

    void InventoryWindow::updatePlayer()
    {
        mPtr = MWBase::Environment::get().getWorld ()->getPlayerPtr();

        // The window caption belongs to the inventory owner, not to the currently
        // selected weapon. Weapon/spell selection is already represented by the HUD.
        // Keeping the player name here also makes the left barter pane unambiguous.
        const std::string playerName = mPtr.getClass().getName(mPtr);
        if (!playerName.empty())
            setTitle(playerName);

        mTradeModel = new TradeItemModel(new InventoryItemModel(mPtr), MWWorld::Ptr());

        if (mSortModel) // reuse existing SortModel when possible to keep previous category/filter settings
            mSortModel->setSourceModel(mTradeModel);
        else
            mSortModel = new SortFilterItemModel(mTradeModel);

        mSortModel->setHideKeys(true);
        mSortModel->setNameFilter(mFilterEdit->getCaption());

        mItemView->setModel(mSortModel);

        mFilterAll->setStateSelected(true);
        mFilterWeapon->setStateSelected(false);
        mFilterApparel->setStateSelected(false);
        mFilterMagic->setStateSelected(false);
        mFilterMisc->setStateSelected(false);
        mFilterKeys->setStateSelected(false);

        mPreview->updatePtr(mPtr);
        mPreview->rebuild();
        mPreview->update();

        dirtyPreview();

        updatePreviewSize();

        updateEncumbranceBar();
        mItemView->update();
        updateKeyRing();
        notifyContentChanged();
    }

    void InventoryWindow::clear()
    {
        mPtr = MWWorld::Ptr();
        mTradeModel = nullptr;
        mSortModel = nullptr;
        mItemView->setModel(nullptr);
        if (mKeyRingList)
            refreshKeyRingPopupRows();
        if (mKeyRingPanel)
            mKeyRingPanel->setVisible(false);
        mKeyRingOpen = false;
        mKeyRingUpdateTimer = 0.f;
    }

    void InventoryWindow::toggleMaximized()
    {
        std::string setting = getModeSetting();

        bool maximized = !Settings::Manager::getBool(setting + " maximized", "Windows");
        if (maximized)
            setting += " maximized";

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        float x = Settings::Manager::getFloat(setting + " x", "Windows") * float(viewSize.width);
        float y = Settings::Manager::getFloat(setting + " y", "Windows") * float(viewSize.height);
        float w = Settings::Manager::getFloat(setting + " w", "Windows") * float(viewSize.width);
        float h = Settings::Manager::getFloat(setting + " h", "Windows") * float(viewSize.height);
        MyGUI::Window* window = mMainWidget->castType<MyGUI::Window>();
        window->setCoord(x, y, w, h);

        if (maximized)
            Settings::Manager::setBool(setting, "Windows", maximized);
        else
            Settings::Manager::setBool(setting + " maximized", "Windows", maximized);

        adjustPanes();
        updatePreviewSize();
    }

    void InventoryWindow::setGuiMode(GuiMode mode)
    {
        mGuiMode = mode;
        if (mItemView)
            mItemView->setSingleClickActionEnabled(mode == GM_Barter || mode == GM_Container);
        std::string setting = getModeSetting();
        setPinButtonVisible(mode == GM_Inventory);

        // Barter panes have the same minimum size but independent geometry.
        // Normal inventory/container modes keep their smaller minimum.
        MyGUI::Window* inventoryWindow = mMainWidget->castType<MyGUI::Window>();
        if (mode == GM_Barter)
            inventoryWindow->setMinSize(500, 460);
        else
            inventoryWindow->setMinSize(360, 220);

        if (!mPtr.isEmpty())
        {
            const std::string playerName = mPtr.getClass().getName(mPtr);
            if (!playerName.empty())
                setTitle(playerName);
        }

        if (Settings::Manager::getBool(setting + " maximized", "Windows"))
            setting += " maximized";

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        MyGUI::IntPoint pos(static_cast<int>(Settings::Manager::getFloat(setting + " x", "Windows") * viewSize.width),
                            static_cast<int>(Settings::Manager::getFloat(setting + " y", "Windows") * viewSize.height));
        MyGUI::IntSize size(static_cast<int>(Settings::Manager::getFloat(setting + " w", "Windows") * viewSize.width),
                            static_cast<int>(Settings::Manager::getFloat(setting + " h", "Windows") * viewSize.height));

        if (mode == GM_Barter)
        {
            size.width = std::max(size.width, 500);
            size.height = std::max(size.height, 460);
        }

        bool needUpdate = (size.width != mMainWidget->getWidth() || size.height != mMainWidget->getHeight());

        mMainWidget->setPosition(pos);
        mMainWidget->setSize(size);

        adjustPanes();

        if (needUpdate)
            updatePreviewSize();
    }

    SortFilterItemModel* InventoryWindow::getSortFilterModel()
    {
        return mSortModel;
    }

    TradeItemModel* InventoryWindow::getTradeModel()
    {
        return mTradeModel;
    }

    ItemModel* InventoryWindow::getModel()
    {
        return mTradeModel;
    }

    void InventoryWindow::onBackgroundSelected()
    {
        if (!mDragAndDrop->mIsOnDragAndDrop)
        {
            mDragAndDrop->queuePendingServerDropTarget(mItemView);
            return;
        }

        if (mDragAndDrop->isBarterDrag())
        {
            // Releasing back onto the source side cancels the preview drag.
            if (mDragAndDrop->mSourceView == mItemView)
            {
                mDragAndDrop->finish();
                return;
            }

            MWBase::Environment::get().getWindowManager()->getTradeWindow()->completeBarterDragToPlayer(
                mDragAndDrop->mSourceIndex, mDragAndDrop->mDraggedCount);
            mDragAndDrop->finish();
            return;
        }

        mDragAndDrop->drop(mTradeModel, mItemView);
    }

    void InventoryWindow::onItemSelected (int index)
    {
        if (!mSortModel || index < 0 || index >= static_cast<int>(mSortModel->getItemCount()))
            return;

        // In grid mode the virtual key ring behaves like a normal inventory
        // item when clicked, but opens its contents instead of entering drag.
        if (isVirtualKeyRing(mSortModel->getItem(index)))
        {
            onFilterChanged(mFilterKeys);
            return;
        }

        onItemSelectedFromSourceModel(mSortModel->mapToSource(index));
    }

    void InventoryWindow::onItemDragStarted(int index)
    {
        if (!mSortModel || !mTradeModel || mDragAndDrop->mIsOnDragAndDrop)
            return;
        if (index < 0 || index >= static_cast<int>(mSortModel->getItemCount()))
            return;

        // The ring is a presentation-only grouping. Its real key records stay
        // in InventoryStore and cannot be dragged/sold as one synthetic stack.
        if (isVirtualKeyRing(mSortModel->getItem(index)))
            return;

        const int sourceIndex = mSortModel->mapToSource(index);
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(mTradeModel->getItemCount()))
            return;

        const ItemStack& item = mTradeModel->getItem(sourceIndex);
        int count = item.mCount;
        if (MyGUI::InputManager::getInstance().isControlPressed())
            count = 1;

        mSelectedItem = sourceIndex;

        if (mTrading)
        {
            if (item.mFlags & ItemStack::Flag_Bound)
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog9}");
                return;
            }
            const int services = MWBase::Environment::get().getWindowManager()->getTradeWindow()->getMerchantServices();
            if (!item.mBase.getClass().canSell(item.mBase, services))
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog4}");
                return;
            }
            mDragAndDrop->startBarterDrag(mSelectedItem, mSortModel, mTradeModel, mItemView, count);
            return;
        }

        ensureSelectedItemUnequipped(count);
        mDragAndDrop->startDrag(mSelectedItem, mSortModel, mTradeModel, mItemView, count);
        notifyContentChanged();
    }

    void InventoryWindow::onItemDoubleClicked(int index)
    {
        if (!mSortModel || !mTradeModel)
            return;

        // Barter/container rows transfer on the first click. Do not let the
        // subsequent MyGUI double-click target whichever item moved into that row.
        if (mTrading || mGuiMode == GM_Container)
            return;

        if (index < 0 || index >= static_cast<int>(mSortModel->getItemCount()))
            return;

        if (isVirtualKeyRing(mSortModel->getItem(index)))
        {
            onFilterChanged(mFilterKeys);
            return;
        }

        const int sourceIndex = mSortModel->mapToSource(index);
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(mTradeModel->getItemCount()))
            return;

        const ItemStack quickItem = mTradeModel->getItem(sourceIndex);
        int quickCount = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : quickItem.mCount;

        // In barter/container/companion modes a double click means "send to the
        // other side". This mirrors modern two-pane inventories and compensates
        // for the paper doll being hidden by default.
        if (mTrading)
        {
            mSelectedItem = sourceIndex;
            if (quickItem.mFlags & ItemStack::Flag_Bound)
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog9}");
                return;
            }
            const int services = MWBase::Environment::get().getWindowManager()->getTradeWindow()->getMerchantServices();
            if (!quickItem.mBase.getClass().canSell(quickItem.mBase, services))
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog4}");
                return;
            }
            sellItem(nullptr, quickCount);
            return;
        }

        if ((mGuiMode == GM_Container || mGuiMode == GM_Companion) && mDragAndDrop->getTransferTargetView())
        {
            mSelectedItem = sourceIndex;
            ensureSelectedItemUnequipped(quickCount);
            mDragAndDrop->startDrag(mSelectedItem, mSortModel, mTradeModel, mItemView, quickCount);
            mDragAndDrop->getTransferTargetView()->eventBackgroundClicked();
            notifyContentChanged();
            return;
        }

        // A list-mode double click is the paper-doll-independent quick action:
        // equipped gear is removed, everything else goes through the same
        // Class::use() path as dropping it on the vanilla character preview.
        const ItemStack item = mTradeModel->getItem(sourceIndex);
        MWWorld::Ptr object = item.mBase;

        if (item.mType == ItemStack::Type_Equipped)
        {
            // Match the existing inventory safeguard for removing a weapon in
            // the middle of an attack/spell action.
            if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(mPtr)
                && object.getTypeName() == typeid(ESM::Weapon).name())
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sCantEquipWeapWarning}");
                return;
            }

            const std::string sound = object.getClass().getDownSoundId(object);
            MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
            invStore.unequipItemQuantity(object, mPtr, 1);
            // Equipment is client-authoritative in this TES3MP branch, but the
            // resulting slot delta must be broadcast immediately so every
            // loaded client sees the double-click unequip.
            mwmp::Main::get().getLocalPlayer()->updateEquipment(false);
            MWBase::Environment::get().getWindowManager()->playSound(sound);
            updateItemView();
            notifyContentChanged();
            return;
        }

        // Keep the normal ArenaMP server-approved item-use path. The server
        // echoes PlayerItemUse back before useItem() is actually executed.
        mwmp::Main::get().getLocalPlayer()->sendItemUse(object);
    }

    void InventoryWindow::onItemSelectedFromSourceModel (int index)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            mDragAndDrop->drop(mTradeModel, mItemView);
            return;
        }

        const ItemStack& item = mTradeModel->getItem(index);
        std::string sound = item.mBase.getClass().getDownSoundId(item.mBase);

        MWWorld::Ptr object = item.mBase;
        int count = item.mCount;
        bool shift = MyGUI::InputManager::getInstance().isShiftPressed();

        if (MyGUI::InputManager::getInstance().isControlPressed())
            count = 1;

        if (mTrading)
        {
            // Can't give conjured items to a merchant
            if (item.mFlags & ItemStack::Flag_Bound)
            {
                MWBase::Environment::get().getWindowManager()->playSound(sound);
                MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog9}");
                return;
            }

            // check if merchant accepts item
            int services = MWBase::Environment::get().getWindowManager()->getTradeWindow()->getMerchantServices();
            if (!object.getClass().canSell(object, services))
            {
                MWBase::Environment::get().getWindowManager()->playSound(sound);
                MWBase::Environment::get().getWindowManager()->
                        messageBox("#{sBarterDialog4}");
                return;
            }

            // ArenaMW two-pane barter uses single-click transfer by default.
            // Ctrl+click still moves exactly one item.
            mSelectedItem = index;
            sellItem(nullptr, count);
            return;
        }

        // Two-pane container mode uses the same click-vs-drag split as barter:
        // a clean click transfers immediately, while moving the mouse past the
        // drag threshold keeps the regular drag-and-drop path available.
        if (mGuiMode == GM_Container && mDragAndDrop->getTransferTargetView())
        {
            mSelectedItem = index;
            ensureSelectedItemUnequipped(count);
            mDragAndDrop->startDrag(mSelectedItem, mSortModel, mTradeModel, mItemView, count);
            mDragAndDrop->getTransferTargetView()->eventBackgroundClicked();
            notifyContentChanged();
            return;
        }

        // If we unequip weapon during attack, it can lead to unexpected behaviour
        if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(mPtr))
        {
            bool isWeapon = item.mBase.getTypeName() == typeid(ESM::Weapon).name();
            MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);

            if (isWeapon && invStore.isEquipped(item.mBase))
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sCantEquipWeapWarning}");
                return;
            }
        }

        if (count > 1 && !shift)
        {
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            std::string message = mTrading ? "#{sQuanityMenuMessage01}" : "#{sTake}";
            std::string name = object.getClass().getName(object) + MWGui::ToolTips::getSoulString(object.getCellRef());
            dialog->openCountDialog(name, message, count);
            dialog->eventOkClicked.clear();
            if (mTrading)
                dialog->eventOkClicked += MyGUI::newDelegate(this, &InventoryWindow::sellItem);
            else
                dialog->eventOkClicked += MyGUI::newDelegate(this, &InventoryWindow::dragItem);
            mSelectedItem = index;
        }
        else
        {
            mSelectedItem = index;
            if (mTrading)
                sellItem (nullptr, count);
            else
                dragItem (nullptr, count);
        }
    }

    void InventoryWindow::ensureSelectedItemUnequipped(int count)
    {
        const ItemStack& item = mTradeModel->getItem(mSelectedItem);
        if (item.mType == ItemStack::Type_Equipped)
        {
            MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
            MWWorld::Ptr newStack = *invStore.unequipItemQuantity(item.mBase, mPtr, count);

            // The unequipped item was re-stacked. We have to update the index
            // since the item pointed does not exist anymore.
            if (item.mBase != newStack)
            {
                updateItemView();  // Unequipping can produce a new stack, not yet in the window...

                // newIndex will store the index of the ItemStack the item was stacked on
                int newIndex = -1;
                for (size_t i=0; i < mTradeModel->getItemCount(); ++i)
                {
                    if (mTradeModel->getItem(i).mBase == newStack)
                    {
                        newIndex = i;
                        break;
                    }
                }

                if (newIndex == -1)
                    throw std::runtime_error("Can't find restacked item");

                mSelectedItem = newIndex;
            }
        }
    }

    void InventoryWindow::dragItem(MyGUI::Widget* sender, int count)
    {
        ensureSelectedItemUnequipped(count);
        mDragAndDrop->startDrag(mSelectedItem, mSortModel, mTradeModel, mItemView, count);
        notifyContentChanged();
    }

    void InventoryWindow::sellItem(MyGUI::Widget* sender, int count)
    {
        ensureSelectedItemUnequipped(count);
        const ItemStack& item = mTradeModel->getItem(mSelectedItem);
        std::string sound = item.mBase.getClass().getUpSoundId(item.mBase);
        MWBase::Environment::get().getWindowManager()->playSound(sound);

        if (item.mType == ItemStack::Type_Barter)
        {
            // this was an item borrowed to us by the merchant
            mTradeModel->returnItemBorrowedToUs(mSelectedItem, count);
            MWBase::Environment::get().getWindowManager()->getTradeWindow()->returnItem(mSelectedItem, count);
        }
        else
        {
            // borrow item to the merchant
            mTradeModel->borrowItemFromUs(mSelectedItem, count);
            MWBase::Environment::get().getWindowManager()->getTradeWindow()->borrowItem(mSelectedItem, count);
        }

        mItemView->update();
        notifyContentChanged();
    }

    void InventoryWindow::completeBarterDragToMerchant(int sourceIndex, int count)
    {
        if (!mTrading || !mTradeModel || sourceIndex < 0
            || sourceIndex >= static_cast<int>(mTradeModel->getItemCount()))
            return;

        mSelectedItem = sourceIndex;
        const ItemStack& item = mTradeModel->getItem(sourceIndex);
        if (item.mFlags & ItemStack::Flag_Bound)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog9}");
            return;
        }
        const int services = MWBase::Environment::get().getWindowManager()->getTradeWindow()->getMerchantServices();
        if (!item.mBase.getClass().canSell(item.mBase, services))
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog4}");
            return;
        }
        count = std::max(1, std::min(count, static_cast<int>(item.mCount)));
        sellItem(nullptr, count);
    }

    void InventoryWindow::updateItemView()
    {
        MWBase::Environment::get().getWindowManager()->updateSpellWindow();

        mItemView->update();

        dirtyPreview();
    }

    void InventoryWindow::onOpen()
    {
        // Reset the filter focus when opening the window
        MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        if (focus == mFilterEdit)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);

        if (mItemView)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mItemView);

        refreshPaperDollToggleVisual();
        refreshWriterButtonVisual();

        // Restore/finalize pane geometry first. Extended ItemView rows must be
        // created against the final RightPane width, not the 350px template size.
        adjustPanes();
        mItemView->relayout();

        if (!mPtr.isEmpty())
        {
            updateEncumbranceBar();
            mItemView->update();
            notifyContentChanged();
        }
    }

    std::string InventoryWindow::getModeSetting() const
    {
        std::string setting = "inventory";
        switch(mGuiMode)
        {
            case GM_Container:
                setting += " container";
                break;
            case GM_Companion:
                setting += " companion";
                break;
            case GM_Barter:
                setting += " barter";
                break;
            default:
                break;
        }

        return setting;
    }

    void InventoryWindow::onWindowResize(MyGUI::Window* _sender)
    {
        adjustPanes();
        // MyGUI parent alignment may bypass ItemView::setSize(), so explicitly
        // reflow table columns while dragging/restoring the inventory window.
        mItemView->relayout();
        std::string setting = getModeSetting();

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        float x = _sender->getPosition().left / float(viewSize.width);
        float y = _sender->getPosition().top / float(viewSize.height);
        float w = _sender->getSize().width / float(viewSize.width);
        float h = _sender->getSize().height / float(viewSize.height);
        Settings::Manager::setFloat(setting + " x", "Windows", x);
        Settings::Manager::setFloat(setting + " y", "Windows", y);
        Settings::Manager::setFloat(setting + " w", "Windows", w);
        Settings::Manager::setFloat(setting + " h", "Windows", h);
        bool maximized = Settings::Manager::getBool(setting + " maximized", "Windows");
        if (maximized)
            Settings::Manager::setBool(setting + " maximized", "Windows", false);

        if (mMainWidget->getSize().width != mLastXSize || mMainWidget->getSize().height != mLastYSize)
        {
            mLastXSize = mMainWidget->getSize().width;
            mLastYSize = mMainWidget->getSize().height;

            updatePreviewSize();
            updateArmorRating();
        }
    }

    void InventoryWindow::updateArmorRating()
    {
        mArmorRating->setCaptionWithReplacing ("#{sArmor}: "
            + MyGUI::utility::toString(static_cast<int>(mPtr.getClass().getArmorRating(mPtr))));
        if (mArmorRating->getTextSize().width > mArmorRating->getSize().width)
            mArmorRating->setCaptionWithReplacing (MyGUI::utility::toString(static_cast<int>(mPtr.getClass().getArmorRating(mPtr))));
    }

    void InventoryWindow::updatePreviewSize()
    {
        if (!mPaperDollVisible || !mLeftPane->getVisible())
            return;

        MyGUI::IntSize size = mAvatarImage->getSize();
        int width = std::min(static_cast<int>(mPreview->getTextureWidth()), size.width);
        int height = std::min(static_cast<int>(mPreview->getTextureHeight()), size.height);
        float scalingFactor = MWBase::Environment::get().getWindowManager()->getScalingFactor();
        mPreview->setViewport(int(width*scalingFactor), int(height*scalingFactor));

        mAvatarImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f,
                                                                     width*scalingFactor/float(mPreview->getTextureWidth()), height*scalingFactor/float(mPreview->getTextureHeight())));
    }

    void InventoryWindow::onNameFilterChanged(MyGUI::EditBox* _sender)
    {
        mSortModel->setNameFilter(_sender->getCaption());
        mItemView->update();
    }

    void InventoryWindow::onFilterChanged(MyGUI::Widget* _sender)
    {
        if (_sender == mFilterAll)
            mSortModel->setCategory(SortFilterItemModel::Category_All);
        else if (_sender == mFilterWeapon)
            mSortModel->setCategory(SortFilterItemModel::Category_Weapon);
        else if (_sender == mFilterApparel)
            mSortModel->setCategory(SortFilterItemModel::Category_Apparel);
        else if (_sender == mFilterMagic)
            mSortModel->setCategory(SortFilterItemModel::Category_Magic);
        else if (_sender == mFilterMisc)
            mSortModel->setCategory(SortFilterItemModel::Category_Misc);
        else if (_sender == mFilterKeys)
            mSortModel->setCategory(SortFilterItemModel::Category_Keys);
        mFilterAll->setStateSelected(false);
        mFilterWeapon->setStateSelected(false);
        mFilterApparel->setStateSelected(false);
        mFilterMagic->setStateSelected(false);
        mFilterMisc->setStateSelected(false);
        mFilterKeys->setStateSelected(false);

        mItemView->update();

        _sender->castType<MyGUI::Button>()->setStateSelected(true);
    }

    void InventoryWindow::refreshPaperDollToggleVisual()
    {
        if (!mPaperDollButton || !mPaperDollIcon)
            return;

        mPaperDollIcon->setImageTexture(mPaperDollVisible
            ? "textures/ui/arenamw/paper_doll_visible.png"
            : "textures/ui/arenamw/paper_doll_hidden.png");
        mPaperDollIcon->setColour(MyGUI::Colour::White);
        mPaperDollButton->setUserString("ToolTipType", "Layout");
        mPaperDollButton->setUserString("ToolTipLayout", "TextToolTipOneLine");
        mPaperDollButton->setUserString("Caption_TextOneLine",
            arenaText(mPaperDollVisible ? "inventory.paper_doll_hide" : "inventory.paper_doll_show"));
    }

    void InventoryWindow::refreshWriterButtonVisual()
    {
        if (!mWriterButton)
            return;
        mWriterButton->setUserString("ToolTipType", "Layout");
        mWriterButton->setUserString("ToolTipLayout", "TextToolTipOneLine");
        mWriterButton->setUserString("Caption_TextOneLine", arenaText("writer.tooltip_open"));
    }

    void InventoryWindow::onPaperDollClicked(MyGUI::Widget*)
    {
        if (mGuiMode != GM_Inventory)
            return;

        mPaperDollVisible = !mPaperDollVisible;
        Settings::Manager::setBool("inventory paper doll", "GUI", mPaperDollVisible);
        refreshPaperDollToggleVisual();

        adjustPanes();
        if (mPaperDollVisible)
        {
            updatePreviewSize();
            dirtyPreview();
        }

        // Recreate table rows after the pane width changes. This mirrors the
        // original Inventory Extender's full-width/no-preview layout and also
        // fixes names that were clipped while the paper doll occupied space.
        if (mItemView)
        {
            mItemView->relayout();
            mItemView->update();
        }
    }

    void InventoryWindow::onViewModeClicked(MyGUI::Widget*)
    {
        if (!mItemView)
            return;

        const ItemView::ViewMode nextMode =
            mItemView->getViewMode() == ItemView::View_List ? ItemView::View_Grid : ItemView::View_List;
        mItemView->setViewMode(nextMode);
        updateBottomControls();
    }

    void InventoryWindow::onWriterClicked(MyGUI::Widget*)
    {
        MWBase::Environment::get().getWindowManager()->openBookWriter();
    }

    void InventoryWindow::onPinToggled()
    {
        Settings::Manager::setBool("inventory pin", "Windows", mPinned);

        MWBase::Environment::get().getWindowManager()->setWeaponVisibility(!mPinned);
    }

    void InventoryWindow::onTitleDoubleClicked()
    {
        if (MyGUI::InputManager::getInstance().isShiftPressed())
            toggleMaximized();
        else if (!mPinned)
            MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Inventory);
    }

    void InventoryWindow::useItem(const MWWorld::Ptr &ptr, bool force)
    {
        const std::string& script = ptr.getClass().getScript(ptr);
        if (!script.empty())
        {
            // Don't try to equip the item if PCSkipEquip is set to 1
            if (ptr.getRefData().getLocals().getIntVar(script, "pcskipequip") == 1)
            {
                ptr.getRefData().getLocals().setVarByInt(script, "onpcequip", 1);
                return;
            }
            ptr.getRefData().getLocals().setVarByInt(script, "onpcequip", 0);
        }

        MWWorld::Ptr player = MWMechanics::getPlayer();

        // early-out for items that need to be equipped, but can't be equipped: we don't want to set OnPcEquip in that case
        if (!ptr.getClass().getEquipmentSlots(ptr).first.empty())
        {
            if (ptr.getClass().hasItemHealth(ptr) && ptr.getCellRef().getCharge() == 0)
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sInventoryMessage1}");
                updateItemView();
                return;
            }

            if (!force)
            {
                std::pair<int, std::string> canEquip = ptr.getClass().canBeEquipped(ptr, player);

                if (canEquip.first == 0)
                {
                    MWBase::Environment::get().getWindowManager()->messageBox(canEquip.second);
                    updateItemView();
                    return;
                }
            }
        }

        // If the item has a script, set OnPCEquip or PCSkipEquip to 1
        if (!script.empty())
        {
            // Ingredients, books and repair hammers must not have OnPCEquip set to 1 here
            const std::string& type = ptr.getTypeName();
            bool isBook = type == typeid(ESM::Book).name();
            if (!isBook && type != typeid(ESM::Ingredient).name() && type != typeid(ESM::Repair).name())
                ptr.getRefData().getLocals().setVarByInt(script, "onpcequip", 1);
            // Books must have PCSkipEquip set to 1 instead
            else if (isBook)
                ptr.getRefData().getLocals().setVarByInt(script, "pcskipequip", 1);
        }

        // ArenaMW native Consuming Animated hook. Capture the item while it is
        // still present in the inventory; ActionApply/ActionEat may remove it.
        ArenaMW::notifyConsumableUsed(player, ptr);
        if (ArenaMW::isConsumingAnimationActive(player))
            mwmp::Main::get().getLocalPlayer()->sendConsumableAnimation(ptr.getCellRef().getRefId());

        std::shared_ptr<MWWorld::Action> action = ptr.getClass().use(ptr, force);
        action->execute(player);

        if (isVisible())
        {
            mItemView->update();

            notifyContentChanged();
        }
        // else: will be updated in open()
    }

    void InventoryWindow::onAvatarClicked(MyGUI::Widget* _sender)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            MWWorld::Ptr ptr = mDragAndDrop->mItem.mBase;

            mDragAndDrop->finish();

            if (mDragAndDrop->mSourceModel != mTradeModel)
            {
                // Move item to the player's inventory
                ptr = mDragAndDrop->mSourceModel->moveItem(mDragAndDrop->mItem, mDragAndDrop->mDraggedCount, mTradeModel);
            }

            /*
                Start of tes3mp change (major)

                Instead of unilaterally using an item, send an ID_PLAYER_ITEM_USE packet and let the server
                decide if the item actually gets used
            */
            //useItem(ptr);
            mwmp::Main::get().getLocalPlayer()->sendItemUse(ptr);
            /*
                End of tes3mp change (major)
            */

            // If item is ingredient or potion don't stop drag and drop to simplify action of taking more than one 1 item
            if ((ptr.getTypeName() == typeid(ESM::Potion).name() ||
                 ptr.getTypeName() == typeid(ESM::Ingredient).name())
                && mDragAndDrop->mDraggedCount > 1)
            {
                // Item can be provided from other window for example container.
                // But after DragAndDrop::startDrag item automaticly always gets to player inventory.
                mSelectedItem = getModel()->getIndex(mDragAndDrop->mItem);
                dragItem(nullptr, mDragAndDrop->mDraggedCount - 1);
            }
        }
        else
        {
            MyGUI::IntPoint mousePos = MyGUI::InputManager::getInstance ().getLastPressedPosition (MyGUI::MouseButton::Left);
            MyGUI::IntPoint relPos = mousePos - mAvatarImage->getAbsolutePosition ();

            MWWorld::Ptr itemSelected = getAvatarSelectedItem (relPos.left, relPos.top);
            if (itemSelected.isEmpty ())
                return;

            for (size_t i=0; i < mTradeModel->getItemCount (); ++i)
            {
                if (mTradeModel->getItem(i).mBase == itemSelected)
                {
                    onItemSelectedFromSourceModel(i);
                    return;
                }
            }
            throw std::runtime_error("Can't find clicked item");
        }
    }

    MWWorld::Ptr InventoryWindow::getAvatarSelectedItem(int x, int y)
    {
        // convert to OpenGL lower-left origin
        y = (mAvatarImage->getHeight()-1) - y;

        // Scale coordinates
        float scalingFactor = MWBase::Environment::get().getWindowManager()->getScalingFactor();
        x = static_cast<int>(x*scalingFactor);
        y = static_cast<int>(y*scalingFactor);

        int slot = mPreview->getSlotSelected (x, y);

        if (slot == -1)
            return MWWorld::Ptr();

        MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
        if(invStore.getSlot(slot) != invStore.end())
        {
            MWWorld::Ptr item = *invStore.getSlot(slot);
            if (!item.getClass().showsInInventory(item))
                return MWWorld::Ptr();
            return item;
        }

        return MWWorld::Ptr();
    }

    void InventoryWindow::updateBottomControls()
    {
        if (!mBottomBar || !mRightPane || !mItemView)
            return;

        const int width = std::max(1, mRightPane->getWidth());
        const int height = std::max(1, mRightPane->getHeight());
        const int bottomHeight = 32;
        const int gap = 4;
        const int bottomTop = std::max(38, height - bottomHeight);

        mBottomBar->setCoord(0, bottomTop, width, bottomHeight);

        int x = 0;

        // The paper-doll toggle deliberately occupies the old key-ring button
        // position at the lower-left edge of the inventory pane.
        const bool paperDollAvailable = (mGuiMode == GM_Inventory);
        const bool showPaperDoll = paperDollAvailable && mPaperDollVisible;
        if (mPaperDollButton)
        {
            mPaperDollButton->setVisible(paperDollAvailable);
            mPaperDollButton->setEnabled(paperDollAvailable);
            if (paperDollAvailable)
            {
                mPaperDollButton->setCoord(x, 2, 30, 26);
                mPaperDollButton->setStateSelected(showPaperDoll);
                x += 30 + gap;
            }
        }

        const bool showGold = (mGuiMode == GM_Barter);
        // One compact stack-style block: icon and count overlap, just like an
        // item icon in the inventory/grid view.
        // Reserve a little more room than the 30px coin itself so 5-7 digit
        // player balances are not clipped by the search field. The count still
        // overlays the coin, but its text box may extend left into this reserve.
        const int goldBlockWidth = showGold ? 64 : 0;

        const int weightWidth = std::max(72, std::min(110, width / 4));
        if (mEncumbranceBar)
        {
            mEncumbranceBar->setCoord(x, 2, weightWidth, 26);
            x += weightWidth + gap;
        }

        if (mViewModeButton)
        {
            mViewModeButton->setCoord(x, 2, 30, 26);
            x += 30 + gap;
        }

        const bool showWriter = (mGuiMode == GM_Inventory);
        if (mWriterButton)
        {
            mWriterButton->setVisible(showWriter);
            mWriterButton->setEnabled(showWriter);
            if (showWriter)
            {
                mWriterButton->setCoord(x, 2, 30, 26);
                x += 30 + gap;
            }
        }

        const int filterWidth = std::max(40, width - x - (showGold ? goldBlockWidth + gap : 0));
        if (mFilterEdit)
            mFilterEdit->setCoord(x, 2, filterWidth, 26);

        if (mGoldIcon && mGoldLabel)
        {
            mGoldIcon->setVisible(showGold);
            mGoldLabel->setVisible(showGold);
            if (showGold)
            {
                const int goldLeft = width - goldBlockWidth;
                mGoldIcon->setCoord(width - 30, 1, 30, 30);
                mGoldLabel->setCoord(goldLeft, 1, goldBlockWidth, 30);
                const std::string goldIcon = resolveGoldIcon();
                if (!goldIcon.empty())
                {
                    mGoldIcon->setVisible(true);
                    mGoldIcon->setImageTexture(goldIcon);
                }
                else
                    mGoldIcon->setVisible(false);
                mGoldLabel->setCaption(MyGUI::utility::toString(getPlayerGold()));
            }
        }

        if (mViewModeIcon)
        {
            static const std::string iconRoot = "icons/inventoryextender/Base/";
            mViewModeIcon->setImageTexture(iconRoot
                + (mItemView->getViewMode() == ItemView::View_List ? "view_grid.dds" : "view_table.dds"));
            mViewModeIcon->setColour(MyGUI::Colour(0.93f, 0.82f, 0.58f));
        }
        if (mViewModeButton)
            mViewModeButton->setStateSelected(mItemView->getViewMode() == ItemView::View_List);

        // ItemView gets every remaining pixel between the top filter row and
        // the bottom strip. Used weight/search controls never overlap the list.
        const int itemTop = mItemView->getTop();
        static_cast<MyGUI::Widget*>(mItemView)->setSize(
            MyGUI::IntSize(width, std::max(40, bottomTop - itemTop - gap)));
    }

    void InventoryWindow::adjustKeyRingLayout()
    {
        if (!mKeyRingPanel || !mKeyRingList)
            return;

        const int mainWidth = std::max(1, mMainWidget->getWidth());
        const int mainHeight = std::max(1, mMainWidget->getHeight());
        const int panelWidth = std::max(260, std::min(360, mainWidth - 40));
        const int panelHeight = std::max(150, std::min(250, mainHeight - 48));
        const int panelLeft = std::max(12, (mainWidth - panelWidth) / 2);
        const int panelTop = std::max(24, (mainHeight - panelHeight) / 2);

        mKeyRingPanel->setCoord(panelLeft, panelTop, panelWidth, panelHeight);
        mKeyRingTitle->setCoord(12, 8, std::max(90, panelWidth - 126), 22);
        mKeyRingWeight->setCoord(panelWidth - 106, 8, 94, 22);
        mKeyRingList->setCoord(10, 34, panelWidth - 20, std::max(60, panelHeight - 44));
    }

    void InventoryWindow::onKeyRingClicked(MyGUI::Widget*)
    {
        onFilterChanged(mFilterKeys);
        if (mFilterEdit)
            mFilterEdit->setOnlyText("");
        if (mSortModel)
            mSortModel->setNameFilter("");
        if (mItemView)
            mItemView->update();
    }

    void InventoryWindow::refreshKeyRingPopupRows()
    {
        if (!mKeyRingList)
            return;

        while (mKeyRingList->getChildCount())
            MyGUI::Gui::getInstance().destroyWidget(mKeyRingList->getChildAt(0));
    }

    void InventoryWindow::updateKeyRing()
    {
        if (!mKeyRingList || !mKeyRingPanel || mPtr.isEmpty())
            return;

        MWWorld::InventoryStore& store = mPtr.getClass().getInventoryStore(mPtr);
        struct KeyEntry
        {
            std::string mName;
            std::string mIcon;
            int mCount = 0;
            float mWeight = 0.f;
        };
        std::vector<KeyEntry> keys;
        int keyCount = 0;
        float totalWeight = 0.f;

        for (MWWorld::ContainerStoreIterator it = store.begin(MWWorld::ContainerStore::Type_Miscellaneous);
             it != store.end(); ++it)
        {
            MWWorld::Ptr key = *it;
            if (!key.getClass().isKey(key))
                continue;

            const int count = std::max(0, key.getRefData().getCount());
            if (count == 0)
                continue;

            KeyEntry entry;
            entry.mName = key.getClass().getName(key);
            entry.mIcon = key.getClass().getInventoryIcon(key);
            entry.mCount = count;
            entry.mWeight = std::max(0.f, key.getClass().getWeight(key)) * count;
            keyCount += count;
            totalWeight += entry.mWeight;
            keys.push_back(entry);
        }

        std::sort(keys.begin(), keys.end(), [](const KeyEntry& left, const KeyEntry& right)
        {
            return Misc::StringUtils::lowerCaseUtf8(left.mName) < Misc::StringUtils::lowerCaseUtf8(right.mName);
        });

        std::ostringstream weightCaption;
        weightCaption << std::fixed << std::setprecision(totalWeight < 10.f ? 2 : 1) << totalWeight;
        mKeyRingWeight->setCaption(arenaText("keyring.weight") + ": " + weightCaption.str());

        refreshKeyRingPopupRows();

        const int viewWidth = std::max(1, mKeyRingList->getWidth());
        const int rowHeight = 30;
        const int iconSize = 24;
        const int iconLeft = 6;
        const int textLeft = iconLeft + iconSize + 8;
        const int textWidth = std::max(40, viewWidth - textLeft - 10);
        const int canvasHeight = std::max(mKeyRingList->getHeight(), static_cast<int>(keys.size()) * rowHeight);

        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            const KeyEntry& key = keys[i];
            MyGUI::Widget* row = mKeyRingList->createWidget<MyGUI::Widget>("",
                MyGUI::IntCoord(0, static_cast<int>(i) * rowHeight, viewWidth, rowHeight),
                MyGUI::Align::Default, "ArenaKeyRow");
            MyGUI::ImageBox* icon = row->createWidget<MyGUI::ImageBox>("ImageBox",
                MyGUI::IntCoord(iconLeft, 3, iconSize, iconSize),
                MyGUI::Align::Left | MyGUI::Align::VCenter, "ArenaKeyRowIcon");
            icon->setNeedMouseFocus(false);
            if (!key.mIcon.empty())
                icon->setImageTexture(key.mIcon);

            MyGUI::TextBox* label = row->createWidget<MyGUI::TextBox>("SandText",
                MyGUI::IntCoord(textLeft, 0, textWidth, rowHeight),
                MyGUI::Align::Left | MyGUI::Align::VCenter | MyGUI::Align::HStretch, "ArenaKeyRowText");
            std::ostringstream rowCaption;
            rowCaption << key.mName;
            if (key.mCount > 1)
                rowCaption << "  x" << key.mCount;
            label->setCaption(rowCaption.str());
            label->setNeedMouseFocus(false);
            label->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
        }

        mKeyRingList->setCanvasSize(MyGUI::IntSize(viewWidth, canvasHeight));
        mKeyRingList->setVisibleVScroll(canvasHeight > mKeyRingList->getHeight());
        mKeyRingPanel->setUserString("ArenaKeyCount", MyGUI::utility::toString(keyCount));

        if (keyCount == 0)
        {
            mKeyRingOpen = false;
            mKeyRingPanel->setVisible(false);
        }
    }

    void InventoryWindow::updateEncumbranceBar()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();

        float capacity = player.getClass().getCapacity(player);
        float encumbrance = player.getClass().getEncumbrance(player);
        mTradeModel->adjustEncumbrance(encumbrance);
        mEncumbranceBar->setValue(std::ceil(encumbrance), static_cast<int>(capacity));
    }

    void InventoryWindow::onFrame(float dt)
    {
        updateEncumbranceBar();

        if (mPinned)
        {
            mUpdateTimer += dt;
            if (0.1f < mUpdateTimer)
            {
                mUpdateTimer = 0;

                // Update pinned inventory in-game
                if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
                {
                    mItemView->update();
                    notifyContentChanged();
                }
            }
        }
    }

    void InventoryWindow::setTrading(bool trading)
    {
        mTrading = trading;
    }

    void InventoryWindow::dirtyPreview()
    {
        if (!mPaperDollVisible)
            return;

        mPreview->update();
        updateArmorRating();
    }

    void InventoryWindow::notifyContentChanged()
    {
        updateKeyRing();
        // update the spell window just in case new enchanted items were added to inventory
        MWBase::Environment::get().getWindowManager()->updateSpellWindow();

        MWBase::Environment::get().getMechanicsManager()->updateMagicEffects(
                    MWMechanics::getPlayer());

        dirtyPreview();
    }

    void InventoryWindow::pickUpObject (MWWorld::Ptr object)
    {
        // If the inventory is not yet enabled, don't pick anything up
        if (!MWBase::Environment::get().getWindowManager()->isAllowed(GW_Inventory))
            return;
        // make sure the object is of a type that can be picked up
        std::string type = object.getTypeName();
        if ( (type != typeid(ESM::Apparatus).name())
            && (type != typeid(ESM::Armor).name())
            && (type != typeid(ESM::Book).name())
            && (type != typeid(ESM::Clothing).name())
            && (type != typeid(ESM::Ingredient).name())
            && (type != typeid(ESM::Light).name())
            && (type != typeid(ESM::Miscellaneous).name())
            && (type != typeid(ESM::Lockpick).name())
            && (type != typeid(ESM::Probe).name())
            && (type != typeid(ESM::Repair).name())
            && (type != typeid(ESM::Weapon).name())
            && (type != typeid(ESM::Potion).name()))
            return;

        // An object that can be picked up must have a tooltip.
        if (!object.getClass().hasToolTip(object))
            return;

        int count = object.getRefData().getCount();
        if (object.getClass().isGold(object))
            count *= object.getClass().getValue(object);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWBase::Environment::get().getWorld()->breakInvisibility(player);

        if (!object.getRefData().activate())
            return;

        MWBase::Environment::get().getMechanicsManager()->itemTaken(player, object, MWWorld::Ptr(), count);

        // add to player inventory
        // can't use ActionTake here because we need an MWWorld::Ptr to the newly inserted object
        MWWorld::Ptr newObject = *player.getClass().getContainerStore (player).add (object, object.getRefData().getCount(), player);

        /*
            Start of tes3mp addition

            Send an ID_OBJECT_DELETE packet every time an item from the world is picked up
            by the player through the inventory HUD
        */
        mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->addObjectGeneric(object);
        objectList->sendObjectDelete();
        /*
            End of tes3mp addition
        */

        // remove from world
        MWBase::Environment::get().getWorld()->deleteObject (object);

        // get ModelIndex to the item
        mTradeModel->update();
        size_t i=0;
        for (; i<mTradeModel->getItemCount(); ++i)
        {
            if (mTradeModel->getItem(i).mBase == newObject)
                break;
        }
        if (i == mTradeModel->getItemCount())
            throw std::runtime_error("Added item not found");
        mDragAndDrop->startDrag(i, mSortModel, mTradeModel, mItemView, count);

        MWBase::Environment::get().getWindowManager()->updateSpellWindow();
    }

    void InventoryWindow::cycle(bool next)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();

        if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(player))
            return;

        const MWMechanics::CreatureStats &stats = player.getClass().getCreatureStats(player);
        bool godmode = MWBase::Environment::get().getWorld()->getGodModeState();
        if ((!godmode && stats.isParalyzed()) || stats.getKnockedDown() || stats.isDead() || stats.getHitRecovery())
            return;

        ItemModel::ModelIndex selected = -1;
        // not using mSortFilterModel as we only need sorting, not filtering
        SortFilterItemModel model(new InventoryItemModel(player));
        model.setSortByType(false);
        model.update();
        if (model.getItemCount() == 0)
            return;

        for (ItemModel::ModelIndex i=0; i<int(model.getItemCount()); ++i)
        {
            MWWorld::Ptr item = model.getItem(i).mBase;
            if (model.getItem(i).mType & ItemStack::Type_Equipped && isRightHandWeapon(item))
                selected = i;
        }

        int incr = next ? 1 : -1;
        bool found = false;
        std::string lastId;
        if (selected != -1)
            lastId = model.getItem(selected).mBase.getCellRef().getRefId();
        ItemModel::ModelIndex cycled = selected;
        for (unsigned int i=0; i<model.getItemCount(); ++i)
        {
            cycled += incr;
            cycled = (cycled + model.getItemCount()) % model.getItemCount();

            MWWorld::Ptr item = model.getItem(cycled).mBase;

            // skip different stacks of the same item, or we will get stuck as stacking/unstacking them may change their relative ordering
            if (Misc::StringUtils::ciEqual(lastId, item.getCellRef().getRefId()))
                continue;

            lastId = item.getCellRef().getRefId();

            if (item.getClass().getTypeName() == typeid(ESM::Weapon).name() &&
                isRightHandWeapon(item) &&
                item.getClass().canBeEquipped(item, player).first)
            {
                found = true;
                break;
            }
        }

        if (!found || selected == cycled)
            return;

        /*
            Start of tes3mp change (major)

            Instead of unilaterally using an item, send an ID_PLAYER_ITEM_USE packet and let the server
            decide if the item actually gets used
        */
        //useItem(model.getItem(cycled).mBase);
        mwmp::Main::get().getLocalPlayer()->sendItemUse(model.getItem(cycled).mBase);
        /*
            End of tes3mp change (major)
        */
    }

    void InventoryWindow::rebuildAvatar()
    {
        mPreview->rebuild();
    }

    int InventoryWindow::getPlayerGold() const
    {
        if (mPtr.isEmpty())
            return 0;
        return mPtr.getClass().getContainerStore(mPtr).count(MWWorld::ContainerStore::sGoldId);
    }

    std::string InventoryWindow::resolveGoldIcon() const
    {
        const ESM::Miscellaneous* gold = MWBase::Environment::get().getWorld()->getStore()
            .get<ESM::Miscellaneous>().search(MWWorld::ContainerStore::sGoldId);
        if (!gold || gold->mIcon.empty())
            return std::string();

        // ESM icon names are not guaranteed to already be VFS-normalized.
        // ItemWidget corrects them before rendering; do the same for the
        // compact barter-gold icon to avoid the magenta missing-texture tile.
        return MWBase::Environment::get().getWindowManager()->correctIconPath(gold->mIcon);
    }
}
