#include "quickloot.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>

#include <components/settings/settings.hpp>
#include <components/widgets/box.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actor.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/character.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/disease.hpp"
#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "../mwinput/sdlmappings.hpp"

#include "../mwmp/PlayerList.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/actionopen.hpp"
#include "../mwworld/actiontake.hpp"
#include "../mwworld/actiontrap.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/interactionanimation.hpp"
#include "../mwworld/player.hpp"

#include "containeritemmodel.hpp"
#include "inventoryitemmodel.hpp"
#include "inventorywindow.hpp"
#include "itemmodel.hpp"
#include "itemwidget.hpp"
#include "pickpocketitemmodel.hpp"


namespace
{
    std::string arenaText(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    std::string getQuickLootMode()
    {
        const auto modeKey = std::make_pair(std::string("GUI"), std::string("quick loot mode"));
        const auto legacyKey = std::make_pair(std::string("GUI"), std::string("quick loot"));

        const auto modeIt = Settings::Manager::mUserSettings.find(modeKey);
        if (modeIt == Settings::Manager::mUserSettings.end())
        {
            const auto legacyIt = Settings::Manager::mUserSettings.find(legacyKey);
            if (legacyIt != Settings::Manager::mUserSettings.end()
                && (legacyIt->second == "false" || legacyIt->second == "0"))
                return "disabled";
        }

        const std::string mode = Settings::Manager::getString("quick loot mode", "GUI");
        if (mode == "disabled" || mode == "container" || mode == "item")
            return mode;
        return Settings::Manager::getBool("quick loot", "GUI") ? "item" : "disabled";
    }

    bool isGraphicHerbalismContainer(const MWWorld::Ptr& ptr)
    {
        return !ptr.isEmpty()
            && ptr.getTypeName() == typeid(ESM::Container).name()
            && ptr.getClass().canBeHarvested(ptr);
    }
}

namespace MWGui
{
    QuickLoot::QuickLoot()
        : Layout("openmw_quickloot.layout")
        , mQuickLoot(nullptr)
        , mModel(nullptr)
        , mRows{}
        , mSortModel(nullptr)
        , mOpened(false)
        , mShouldOpen(false)
        , mHidden(true)
        , mPlaying(false)
        , mDismissed(false)
        , mFocusToolTipX(0.f)
        , mFocusToolTipY(0.f)
        , mDelay(0.f)
        , mRemainingDelay(0.f)
        , mLastMouseX(0)
        , mLastMouseY(0)
        , mEnabled(true)
        , mFrameDuration(0.f)
        , mStationaryTime(0.f)
        , mStationaryDelay(0.65f)
        , mReadyToShow(false)
        , mHasLastPlayerPosition(false)
        , mLastPlayerX(0.f)
        , mLastPlayerY(0.f)
        , mLastPlayerZ(0.f)
        , mLastIndex(0)
        , mVisibleStart(0)
    {
        getWidget(mQuickLoot, "QuickLoot");
        for (int i = 0; i < sVisibleRows; ++i)
        {
            RowWidgets& row = mRows[static_cast<std::size_t>(i)];
            const std::string prefix = "Row" + std::to_string(i);
            getWidget(row.mRoot, prefix);
            getWidget(row.mMarker, prefix + "Marker");
            getWidget(row.mIcon, prefix + "Icon");
            getWidget(row.mCount, prefix + "Count");
            getWidget(row.mWeight, prefix + "Weight");
            getWidget(row.mValue, prefix + "Value");
            getWidget(row.mName, prefix + "Name");
        }

        setVisibleAll(false);

        // QuickLoot is a HUD overlay, not a GUI mode. Never take keyboard focus:
        // doing so makes MyGUI consume key releases and disables W/S movement.
        mQuickLoot->eventKeyButtonPressed += MyGUI::newDelegate(this, &QuickLoot::onKeyButtonPressed);
        mQuickLoot->setNeedKeyFocus(false);
        mQuickLoot->setNeedMouseFocus(false);
        mMainWidget->setNeedMouseFocus(false);
        mMainWidget->setNeedKeyFocus(false);

        // Use the same appearance delay as ordinary tooltips.
        mDelay = Settings::Manager::getFloat("tooltip delay", "GUI");
        mRemainingDelay = mDelay;
        mStationaryDelay = std::clamp(
            Settings::Manager::getFloat("quick loot stationary delay", "GUI"), 0.1f, 2.f);
    }

    QuickLoot::~QuickLoot()
    {
        clearModels();
    }

    void QuickLoot::clearModels()
    {
        if (mSortModel)
            delete mSortModel; // SortFilterItemModel owns mModel.
        else
            delete mModel;

        mSortModel = nullptr;
        mModel = nullptr;
    }

    int QuickLoot::getEntryCount() const
    {
        const std::size_t itemCount = mSortModel ? mSortModel->getItemCount() : 0;
        const std::size_t capped = std::min<std::size_t>(
            itemCount, static_cast<std::size_t>(std::numeric_limits<int>::max() - 1));
        return static_cast<int>(capped) + 1;
    }

    void QuickLoot::refreshRows()
    {
        const int total = getEntryCount();
        const int itemCount = std::max(0, total - 1);
        const int visibleItemRows = sVisibleRows - 1;

        if (itemCount <= 0)
        {
            for (RowWidgets& row : mRows)
            {
                row.mIcon->setItem(MWWorld::Ptr());
                row.mRoot->setVisible(false);
            }
            return;
        }

        mLastIndex = std::max(0, std::min(mLastIndex, total - 1));
        if (mLastIndex == 0)
            mVisibleStart = 0;
        else
        {
            const int selectedItem = mLastIndex - 1;
            if (selectedItem < mVisibleStart)
                mVisibleStart = selectedItem;
            else if (selectedItem >= mVisibleStart + visibleItemRows)
                mVisibleStart = selectedItem - visibleItemRows + 1;
        }
        mVisibleStart = std::max(0, std::min(mVisibleStart, std::max(0, itemCount - visibleItemRows)));

        for (int rowIndex = 0; rowIndex < sVisibleRows; ++rowIndex)
        {
            RowWidgets& row = mRows[static_cast<std::size_t>(rowIndex)];
            const int entryIndex = rowIndex == 0 ? 0 : mVisibleStart + rowIndex;
            if (entryIndex > itemCount)
            {
                row.mIcon->setItem(MWWorld::Ptr());
                row.mRoot->setVisible(false);
                continue;
            }

            const bool selected = entryIndex == mLastIndex;
            const std::string textSkin = selected ? "SandBrightText" : "SandText";
            const float textAlpha = selected ? 1.f : 0.72f;

            row.mMarker->changeWidgetSkin(textSkin);
            row.mCount->changeWidgetSkin(textSkin);
            row.mName->changeWidgetSkin(textSkin);
            row.mMarker->setAlpha(textAlpha);
            row.mCount->setAlpha(textAlpha);
            row.mName->setAlpha(textAlpha);

            // Never fade an item icon. Semi-transparent icons blend with the red/brown
            // HUD box and look as if the texture itself has been recoloured.
            row.mIcon->setAlpha(1.f);

            if (entryIndex == 0)
            {
                row.mMarker->setCaption("^");
                row.mIcon->setItem(MWWorld::Ptr());
                row.mIcon->setVisible(false);
                row.mCount->setVisible(false);
                row.mWeight->setVisible(false);
                row.mValue->setVisible(false);
                row.mName->setVisible(true);
                row.mName->setCaption(mContainerName.empty() ? arenaText("quickloot.container") : mContainerName);
            }
            else
            {
                const ItemStack item = mSortModel->getItem(entryIndex - 1);
                std::string name = item.mBase.getClass().getName(item.mBase);
                if (name.empty())
                    name = arenaText("quickloot.item");

                // Compact order: selection marker, icon, item name, stack count.
                row.mMarker->setCaption(selected ? ">" : "");

                // Rebind the texture every time a row is populated. ItemWidget caches
                // the icon path, while MyGUI may release or postpone the underlying
                // texture when the overlay was hidden. Clearing first prevents the
                // barely-visible/stale icon state seen on the first QuickLoot frame.
                row.mIcon->setVisible(false);
                row.mIcon->setItem(MWWorld::Ptr());
                row.mIcon->setItem(item.mBase);
                row.mIcon->setCount(1);
                row.mIcon->setAlpha(1.f);
                row.mIcon->setVisible(true);
                row.mName->setVisible(true);
                row.mName->setCaption(name);
                row.mCount->setVisible(true);
                row.mCount->setCaption("x" + std::to_string(item.mCount));
                row.mWeight->setVisible(false);
                row.mValue->setVisible(false);
            }

            row.mRoot->setVisible(true);
        }
    }

    bool QuickLoot::handleMouseWheel(int rel)
    {
        if (!isVisible() || rel == 0)
            return false;

        const int total = getEntryCount();
        if (total <= 0)
            return false;

        // One SDL wheel event always advances exactly one row, even on high-resolution wheels.
        if (rel < 0)
            mLastIndex = (mLastIndex + 1) % total;
        else
            mLastIndex = (mLastIndex + total - 1) % total;

        refreshRows();
        resize();
        return true;
    }

    bool QuickLoot::checkOwned()
    {
        if (mFocusObject.isEmpty())
            return false;

        MWWorld::Ptr ptr = MWMechanics::getPlayer();
        MWWorld::Ptr victim;

        MWBase::MechanicsManager* mm = MWBase::Environment::get().getMechanicsManager();
        return !mm->isAllowedToUse(ptr, mFocusObject, victim);
    }

    void QuickLoot::openStandardContainer()
    {
        if (mFocusObject.isEmpty())
            return;

        mOpened = true;
        setVisibleAll(false);
        MWBase::Environment::get().getWorld()->getPlayer().activate();
    }

    bool QuickLoot::activateSelected()
    {
        if (!isVisible() || !mModel || !mSortModel)
            return false;

        if (mLastIndex == 0)
        {
            openStandardContainer();
            return true;
        }

        const int itemIndex = mLastIndex - 1;
        if (itemIndex < 0 || itemIndex >= static_cast<int>(mSortModel->getItemCount()))
            return false;

        onItemSelected(itemIndex);
        return true;
    }

    void QuickLoot::onItemSelected(int index)
    {
        if (!mModel || !mSortModel || index < 0
            || index >= static_cast<int>(mSortModel->getItemCount()))
            return;

        if (!MWBase::Environment::get().getWindowManager()->isAllowed(MWGui::GW_Inventory))
            return;

        const ItemModel::ModelIndex sourceIndex = mSortModel->mapToSource(index);
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(mModel->getItemCount()))
            return;

        mOpened = true;
        ensureTrapTriggered();
        MWMechanics::diseaseContact(MWMechanics::getPlayer(), mFocusObject);

        // Keep a copy: moving the item may invalidate references owned by the model.
        const ItemStack item = mModel->getItem(sourceIndex);

        // Activate takes the complete stack (gold, potions, ingredients, arrows, etc.).
        const int count = static_cast<int>(std::min<std::size_t>(
            item.mCount, static_cast<std::size_t>(std::numeric_limits<int>::max())));

        if (!mModel->onTakeItem(item.mBase, count))
            return;

        ItemModel* playerModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getModel();
        mModel->update();
        MWWorld::Ptr movedItem = mModel->moveItem(item, count, playerModel);
        MWWorld::InteractionAnimation::playQuickLoot(mFocusObject, false);
        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();

        if (MyGUI::InputManager::getInstance().isControlPressed())
            MWBase::Environment::get().getWindowManager()->getInventoryWindow()->useItem(movedItem);
        else
        {
            const std::string sound = item.mBase.getClass().getUpSoundId(item.mBase);
            MWBase::Environment::get().getWindowManager()->playSound(sound);
        }

        mModel->update();
        mSortModel->update();
        const int remainingItems = getEntryCount() - 1;
        if (remainingItems <= 0)
        {
            setVisibleAll(false);
            return;
        }

        mLastIndex = std::min(mLastIndex, remainingItems);
        refreshRows();
        resize();
    }

    void QuickLoot::ensureTrapTriggered()
    {
        if (mFocusObject.isEmpty() || mFocusObject.getTypeName() != typeid(ESM::Container).name())
            return;

        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        MWWorld::InventoryStore& invStore = player.getClass().getInventoryStore(player);

        const bool isTrapped = !mFocusObject.getCellRef().getTrap().empty();
        bool hasKey = false;
        std::string keyName;

        static const std::string trapActivationSound = "Disarm Trap Fail";

        // Necessary since having the key will always deactivate the trap.
        const std::string keyId = mFocusObject.getCellRef().getKey();
        if (!keyId.empty())
        {
            MWWorld::Ptr keyPtr = invStore.search(keyId);
            if (!keyPtr.isEmpty())
            {
                hasKey = true;
                keyName = keyPtr.getClass().getName(keyPtr);
            }
        }

        if (isTrapped && hasKey)
        {
            MWBase::Environment::get().getWindowManager()->messageBox(keyName + " #{sKeyUsed}");
            mFocusObject.getCellRef().setTrap("");
            MWBase::Environment::get().getSoundManager()->playSound3D(
                mFocusObject, "Disarm Trap", 1.f, 1.f);
        }
        else if (isTrapped)
        {
            std::shared_ptr<MWWorld::Action> action(
                new MWWorld::ActionTrap(mFocusObject.getCellRef().getTrap(), mFocusObject));
            action->setSound(trapActivationSound);
            action->execute(player);
        }
    }

    bool QuickLoot::handleKeyPress(MyGUI::KeyCode key)
    {
        if (!isVisible())
            return false;

        const SDL_Keycode takeAllKey = SDL_GetKeyFromName(
            Settings::Manager::getString("key quickloot takeall", "MorroUI").c_str());
        const MyGUI::KeyCode takeAll = MWInput::sdlKeyToMyGUI(takeAllKey);

        if (key == MyGUI::KeyCode::Q)
        {
            mDismissed = true;
            clearModels();
            setVisibleAll(false);
            return true;
        }
        if (key == MyGUI::KeyCode::F)
        {
            openStandardContainer();
            return true;
        }

        if (key == MyGUI::KeyCode::ArrowUp || key == MyGUI::KeyCode::ArrowLeft
            || key == MyGUI::KeyCode::W || key == MyGUI::KeyCode::A)
        {
            handleMouseWheel(1);
            return true;
        }
        if (key == MyGUI::KeyCode::ArrowDown || key == MyGUI::KeyCode::ArrowRight
            || key == MyGUI::KeyCode::S || key == MyGUI::KeyCode::D)
        {
            handleMouseWheel(-1);
            return true;
        }
        if (key == MyGUI::KeyCode::Return || key == MyGUI::KeyCode::NumpadEnter)
        {
            activateSelected();
            return true;
        }

        // All four movement directions are usable for the vertical QuickLoot list.
        if (static_cast<int>(key.getValue()) != static_cast<int>(takeAll.getValue()))
            return false;

        if (!mModel || !mSortModel)
            return true;

        mOpened = true;
        ensureTrapTriggered();
        MWMechanics::diseaseContact(MWMechanics::getPlayer(), mFocusObject);

        ItemModel* playerModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getModel();
        mModel->update();

        // Unequip all source items first to avoid unequipping/reequipping while transferring.
        if (mFocusObject.getClass().hasInventoryStore(mFocusObject))
        {
            MWWorld::InventoryStore& invStore = mFocusObject.getClass().getInventoryStore(mFocusObject);
            for (std::size_t i = 0; i < mModel->getItemCount(); ++i)
            {
                const ItemStack item = mModel->getItem(static_cast<int>(i));
                if (invStore.isEquipped(item.mBase))
                    invStore.unequipItem(item.mBase, mFocusObject);
            }
        }

        mModel->update();
        const std::size_t itemCount = mModel->getItemCount();
        bool tookAny = false;
        for (std::size_t i = 0; i < itemCount; ++i)
        {
            const ItemStack item = mModel->getItem(static_cast<int>(i));
            if (i == 0)
            {
                const std::string sound = item.mBase.getClass().getUpSoundId(item.mBase);
                MWBase::Environment::get().getWindowManager()->playSound(sound);
            }

            const int count = static_cast<int>(std::min<std::size_t>(
                item.mCount, static_cast<std::size_t>(std::numeric_limits<int>::max())));
            if (!mModel->onTakeItem(item.mBase, count))
                break;
            mModel->moveItem(item, count, playerModel);
            tookAny = true;
        }

        if (tookAny)
            MWWorld::InteractionAnimation::playQuickLoot(mFocusObject, true);
        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
        mModel->update();
        mSortModel->update();
        if (getEntryCount() <= 1)
        {
            setVisibleAll(false);
            return true;
        }

        mLastIndex = 1;
        mVisibleStart = 0;
        refreshRows();
        resize();
        return true;
    }

    void QuickLoot::onKeyButtonPressed(MyGUI::Widget*, MyGUI::KeyCode key, MyGUI::Char)
    {
        handleKeyPress(key);
    }

    void QuickLoot::setEnabled(bool enabled)
    {
        mEnabled = enabled;
        if (mEnabled)
            mDismissed = false;
        else
        {
            mStationaryTime = 0.f;
            mReadyToShow = false;
            mHasLastPlayerPosition = false;
            clearModels();
            setVisibleAll(false);
        }
    }

    void QuickLoot::onFrame(float frameDuration)
    {
        mFrameDuration = frameDuration;

        if (!mEnabled || mFocusObject.isEmpty())
        {
            mStationaryTime = 0.f;
            mReadyToShow = false;
            mHasLastPlayerPosition = false;
            return;
        }

        const MWWorld::Ptr playerPtr = MWMechanics::getPlayer();
        MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
        const MWMechanics::Movement& movement = playerPtr.getClass().getMovementSettings(playerPtr);
        const ESM::Position& playerPosition = playerPtr.getRefData().getPosition();

        const bool movementRequested = player.getAutoMove()
            || std::abs(movement.mPosition[0]) > 0.01f
            || std::abs(movement.mPosition[1]) > 0.01f
            || std::abs(movement.mPosition[2]) > 0.01f;

        bool positionChanged = false;
        if (mHasLastPlayerPosition)
        {
            const float dx = playerPosition.pos[0] - mLastPlayerX;
            const float dy = playerPosition.pos[1] - mLastPlayerY;
            const float dz = playerPosition.pos[2] - mLastPlayerZ;
            // One world unit is below visible movement and filters tiny physics jitter.
            positionChanged = dx * dx + dy * dy + dz * dz > 1.f;
        }

        mLastPlayerX = playerPosition.pos[0];
        mLastPlayerY = playerPosition.pos[1];
        mLastPlayerZ = playerPosition.pos[2];
        mHasLastPlayerPosition = true;

        const bool suppressOverlay = movementRequested || positionChanged
            || player.isInCombat()
            || MWBase::Environment::get().getWindowManager()->isGuiMode();

        if (suppressOverlay)
        {
            mStationaryTime = 0.f;
            mReadyToShow = false;
            if (isVisible())
            {
                clearModels();
                setVisibleAll(false);
            }
            return;
        }

        mStationaryTime = std::min(mStationaryDelay, mStationaryTime + frameDuration);
        mReadyToShow = mStationaryTime >= mStationaryDelay;
    }

    void QuickLoot::setVisibleAll(bool visible)
    {
        mHidden = !visible;
        if (visible && mOpened && mShouldOpen)
        {
            playOpenAnimation();
            mShouldOpen = false;
        }

        if (visible)
            resize();

        mMainWidget->setVisible(visible);
        for (int i = 0; i < mMainWidget->getChildCount(); ++i)
            mMainWidget->getChildAt(i)->setVisible(visible);

    }

    void QuickLoot::update(float)
    {
        if (!mEnabled)
            return;

        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();
        const bool guiMode = winMgr->isGuiMode();
        const bool inCombat = MWBase::Environment::get().getWorld()->getPlayer().isInCombat();

        if (guiMode || mFocusObject.isEmpty() || mwmp::PlayerList::isDedicatedPlayer(mFocusObject)
            || isGraphicHerbalismContainer(mFocusObject)
            || mFocusObject.getCellRef().getLockLevel() > 0)
        {
            clearModels();
            setVisibleAll(false);
            return;
        }

        clearModels();

        const bool loot = mFocusObject.getClass().isActor()
            && mFocusObject.getClass().getCreatureStats(mFocusObject).isDead();
        const bool sneaking = MWBase::Environment::get().getMechanicsManager()->isSneaking(
            MWMechanics::getPlayer());

        mQuickLoot->getParent()->changeWidgetSkin(checkOwned() ? "HUD_Box_Owned" : "HUD_Box");

        if (mFocusObject.getClass().hasInventoryStore(mFocusObject))
        {
            if (mFocusObject.getClass().isNpc() && !loot && sneaking && !inCombat)
            {
                mModel = new PickpocketItemModel(mFocusObject, new InventoryItemModel(mFocusObject),
                    !mFocusObject.getClass().getCreatureStats(mFocusObject).getKnockedDown());
            }
            else if (loot)
                mModel = new InventoryItemModel(mFocusObject);
        }
        else
            mModel = new ContainerItemModel(mFocusObject);

        if (!mModel)
        {
            setVisibleAll(false);
            return;
        }

        mSortModel = new SortFilterItemModel(mModel);
        mSortModel->setCategory(SortFilterItemModel::Category_Simple);
        mSortModel->update();

        // Do not show QuickLoot for empty containers or corpses.
        if (getEntryCount() <= 1)
        {
            setVisibleAll(false);
            return;
        }

        // Entry 0 is the container header; entry 1 is the first item.
        // The GUI selector controls which row is initially selected.
        const std::string quickLootMode = getQuickLootMode();
        mLastIndex = quickLootMode == "container" ? 0 : 1;
        mVisibleStart = 0;
        mContainerName = mFocusObject.getClass().getName(mFocusObject);
        refreshRows();
        setVisibleAll(true);
    }

    void QuickLoot::position(MyGUI::IntPoint& position, MyGUI::IntSize size, MyGUI::IntSize viewportSize)
    {
        position += MyGUI::IntPoint(0, 32)
            - MyGUI::IntPoint(static_cast<int>(
                MyGUI::InputManager::getInstance().getMousePosition().left
                / static_cast<float>(viewportSize.width) * size.width), 0);

        if (position.left + size.width > viewportSize.width)
            position.left = viewportSize.width - size.width;
        if (position.top + size.height > viewportSize.height)
            position.top = MyGUI::InputManager::getInstance().getMousePosition().top - size.height - 8;
    }

    void QuickLoot::playOpenAnimation()
    {
        if (mFocusObject.isEmpty() || mFocusObject.getTypeName() != typeid(ESM::Container).name())
            return;
        MWRender::Animation* anim = MWBase::Environment::get().getWorld()->getAnimation(mFocusObject);

        if (!anim || !anim->hasAnimation("containeropen") || anim->isPlaying("containeropen")
            || anim->isPlaying("containerclose"))
            return;

        mPlaying = true;
        anim->play("containeropen", MWMechanics::Priority_Persistent, MWRender::Animation::BlendMask_All,
            false, 1.f, "start", "stop", 0.f, 0);
    }

    void QuickLoot::playCloseAnimation() const
    {
        if (mFocusObject.isEmpty() || mFocusObject.getTypeName() != typeid(ESM::Container).name())
            return;

        MWRender::Animation* anim = MWBase::Environment::get().getWorld()->getAnimation(mFocusObject);
        if (!anim || !anim->hasAnimation("containerclose"))
            return;

        float complete = 0.f;
        float startPoint = 0.f;
        if (anim->getInfo("containeropen", &complete))
            startPoint = 1.f - complete;

        anim->play("containerclose", MWMechanics::Priority_Persistent, MWRender::Animation::BlendMask_All,
            false, 1.f, "start", "stop", startPoint, 0);
    }

    void QuickLoot::resize()
    {
        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        const int visibleItems = std::min(sVisibleRows - 1, std::max(0, getEntryCount() - 1));
        if (visibleItems <= 0)
            return;

        constexpr int outerPadding = 5;
        constexpr int headerHeight = 24;
        constexpr int itemHeight = 26;
        constexpr int markerWidth = 16;
        constexpr int iconSize = 20;
        constexpr int iconLeft = markerWidth;
        constexpr int nameLeft = iconLeft + iconSize + 4;
        constexpr int headerNameLeft = markerWidth + 4;
        constexpr int nameCountGap = 6;
        constexpr int rightPadding = 7;

        // Fit the box to the longest currently visible row instead of reserving a
        // fixed 400-pixel column. A small minimum keeps short names readable, while
        // the maximum prevents unusually long modded names from covering the screen.
        int desiredInnerWidth = headerNameLeft + mRows[0].mName->getTextSize().width + rightPadding;
        for (int rowIndex = 1; rowIndex <= visibleItems; ++rowIndex)
        {
            const RowWidgets& row = mRows[static_cast<std::size_t>(rowIndex)];
            const int nameWidth = row.mName->getTextSize().width;
            const int countWidth = row.mCount->getTextSize().width;
            desiredInnerWidth = std::max(desiredInnerWidth,
                nameLeft + nameWidth + nameCountGap + countWidth + rightPadding);
        }

        // Start narrower for ordinary containers, but keep enough headroom for
        // long translated or modded item names instead of clipping them early.
        const int maxOuterWidth = std::max(1, std::min(480, viewSize.width - 16));
        const int minOuterWidth = std::min(200, maxOuterWidth);
        const int outerWidth = std::max(minOuterWidth,
            std::min(desiredInnerWidth + outerPadding * 2, maxOuterWidth));
        const int outerHeight = outerPadding * 2 + headerHeight + visibleItems * itemHeight;
        const int innerWidth = std::max(1, outerWidth - outerPadding * 2);
        const int innerHeight = std::max(1, outerHeight - outerPadding * 2);

        setCoord(viewSize.width * 7 / 10 - outerWidth / 2,
            viewSize.height * 6 / 10 - outerHeight / 2,
            outerWidth, outerHeight);

        // Explicit coordinates keep the layout compact with every GUI scaling factor.
        mQuickLoot->getParent()->setCoord(0, 0, outerWidth, outerHeight);
        mQuickLoot->setCoord(outerPadding, outerPadding, innerWidth, innerHeight);

        for (int rowIndex = 0; rowIndex < sVisibleRows; ++rowIndex)
        {
            RowWidgets& row = mRows[static_cast<std::size_t>(rowIndex)];
            const bool header = rowIndex == 0;
            const int rowHeight = header ? headerHeight : itemHeight;
            const int rowTop = header ? 0 : headerHeight + (rowIndex - 1) * itemHeight;
            row.mRoot->setCoord(0, rowTop, innerWidth, rowHeight);
            row.mMarker->setCoord(0, 0, markerWidth, rowHeight);

            if (header)
            {
                row.mIcon->setCoord(iconLeft, (rowHeight - iconSize) / 2, iconSize, iconSize);
                row.mName->setCoord(headerNameLeft, 0,
                    std::max(1, innerWidth - headerNameLeft - rightPadding), rowHeight);
                row.mCount->setCoord(0, 0, 0, 0);
            }
            else
            {
                row.mIcon->setCoord(iconLeft, (rowHeight - iconSize) / 2, iconSize, iconSize);

                const int countWidth = std::max(28,
                    std::min(56, row.mCount->getTextSize().width + 4));
                const int maximumNameWidth = std::max(1,
                    innerWidth - nameLeft - nameCountGap - countWidth - rightPadding);
                const int nameWidth = std::max(1,
                    std::min(maximumNameWidth, row.mName->getTextSize().width + 2));
                const int countLeft = nameLeft + nameWidth + nameCountGap;

                row.mName->setCoord(nameLeft, 0, nameWidth, rowHeight);
                row.mCount->setCoord(countLeft, 0, countWidth, rowHeight);
            }

            row.mWeight->setCoord(0, 0, 0, 0);
            row.mValue->setCoord(0, 0, 0, 0);
        }
    }

    void QuickLoot::clear()
    {
        mFocusObject = MWWorld::Ptr();
        mLastFocusObject = MWWorld::Ptr();
        mContainerName.clear();
        mDismissed = false;
        mStationaryTime = 0.f;
        mReadyToShow = false;
        mHasLastPlayerPosition = false;
        clearModels();
        setVisibleAll(false);
    }

    void QuickLoot::setFocusObject(const MWWorld::Ptr& focus)
    {
        // The three-state selector is the single source of truth for the overlay.
        const std::string quickLootMode = getQuickLootMode();
        const bool quickLootEnabled = quickLootMode != "disabled";

        if (focus != mFocusObject)
        {
            mDismissed = false;
            mStationaryTime = 0.f;
            mReadyToShow = false;
            mHasLastPlayerPosition = false;
        }

        if (!mEnabled || !quickLootEnabled)
        {
            mLastFocusObject = mFocusObject;
            mFocusObject = focus;
            mStationaryTime = 0.f;
            mReadyToShow = false;
            mHasLastPlayerPosition = false;
            clearModels();
            setVisibleAll(false);
            return;
        }

        const MWWorld::Ptr player = MWMechanics::getPlayer();
        const bool werewolf = player.getClass().getNpcStats(player).isWerewolf();
        const bool incapacitated = player.getClass().getCreatureStats(player).isParalyzed()
            || player.getClass().getCreatureStats(player).getKnockedDown();

        if (focus.isEmpty() || MWBase::Environment::get().getWindowManager()->isGuiMode() || werewolf
            || incapacitated || mwmp::PlayerList::isDedicatedPlayer(focus)
            || isGraphicHerbalismContainer(focus)
            || (focus.getTypeName() != typeid(ESM::Container).name()
                && !focus.getClass().hasInventoryStore(focus)))
        {
            mLastFocusObject = mFocusObject;
            mFocusObject = focus;
            mStationaryTime = 0.f;
            mReadyToShow = false;
            mHasLastPlayerPosition = false;
            clearModels();
            setVisibleAll(false);
            return;
        }

        if (focus != mFocusObject)
        {
            if (mOpened && !mShouldOpen)
                playCloseAnimation();
            mOpened = false;
            mShouldOpen = true;
        }

        mLastFocusObject = mFocusObject;
        mFocusObject = focus;

        if (mDismissed)
        {
            clearModels();
            setVisibleAll(false);
            return;
        }

        const bool combat = MWBase::Environment::get().getWorld()->getPlayer().isInCombat();
        if (combat || !mReadyToShow)
        {
            clearModels();
            setVisibleAll(false);
            return;
        }

        const bool loot = mFocusObject.getClass().isActor()
            && mFocusObject.getClass().getCreatureStats(mFocusObject).isDead()
            && mFocusObject.getClass().getCreatureStats(mFocusObject).isDeathAnimationFinished();
        const bool sneaking = MWBase::Environment::get().getMechanicsManager()->isSneaking(player);

        bool hide = false;
        if (mFocusObject.getClass().hasInventoryStore(mFocusObject) && mFocusObject.getClass().isNpc())
        {
            if ((!loot && !sneaking) || (!loot && sneaking && combat))
                hide = true;
        }

        if (mLastFocusObject == mFocusObject && !hide && mSortModel)
        {
            mSortModel->update();
            const int total = getEntryCount();
            if (total <= 1)
            {
                setVisibleAll(false);
                return;
            }

            mLastIndex = std::max(0, std::min(mLastIndex, total - 1));
            refreshRows();
            setVisibleAll(true);
            return;
        }

        setVisibleAll(false);
        clearModels();
        if (!hide)
            update(mFrameDuration);
    }

    void QuickLoot::setFocusObjectScreenCoords(float min_x, float min_y, float max_x, float max_y)
    {
        mFocusToolTipX = (min_x + max_x) / 2;
        mFocusToolTipY = min_y;
    }

    void QuickLoot::setDelay(float delay)
    {
        mDelay = delay;
        mRemainingDelay = mDelay;
    }

    void QuickLoot::setStationaryDelay(float delay)
    {
        mStationaryDelay = std::clamp(delay, 0.1f, 2.f);
        mStationaryTime = std::min(mStationaryTime, mStationaryDelay);
        mReadyToShow = mStationaryTime >= mStationaryDelay;
    }
}
