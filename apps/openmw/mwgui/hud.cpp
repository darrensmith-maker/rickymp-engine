#include "hud.hpp"

#include <MyGUI_RenderManager.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/PlayerList.hpp"
#include "../mwworld/cellstore.hpp"
/*
    End of tes3mp addition
*/

#include <components/settings/settings.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "inventorywindow.hpp"
#include "spellicons.hpp"
#include "itemmodel.hpp"
#include "draganddrop.hpp"

#include "itemwidget.hpp"

namespace
{
    // The location name temporarily replaces the clock below the compass.
    // Only the time animates: fade it out, show the location steadily, then fade time back in.
    constexpr float sCompassInfoFadeDuration = 0.35f;
    constexpr float sCompassLocationHoldDuration = 4.f;
    constexpr float sCompassInfoSequenceDuration
        = sCompassInfoFadeDuration * 2.f + sCompassLocationHoldDuration;

    std::string getWeaponSpellBoxMode()
    {
        const auto modeKey = std::make_pair(std::string("GUI"), std::string("weapon spell box mode"));
        const auto legacyKey = std::make_pair(std::string("GUI"), std::string("persistent weapon spell boxes"));
        if (Settings::Manager::mUserSettings.find(modeKey) == Settings::Manager::mUserSettings.end())
        {
            const auto legacyIt = Settings::Manager::mUserSettings.find(legacyKey);
            if (legacyIt != Settings::Manager::mUserSettings.end())
                return (legacyIt->second == "false" || legacyIt->second == "0") ? "hidden" : "transparent";
        }

        const std::string mode = Settings::Manager::getString("weapon spell box mode", "GUI");
        if (mode == "hidden" || mode == "transparent" || mode == "visible")
            return mode;
        return Settings::Manager::getBool("persistent weapon spell boxes", "GUI")
            ? "transparent" : "hidden";
    }

    std::string getResourceBarMode()
    {
        const std::string mode = Settings::Manager::getString("resource bars mode", "HUD");
        if (mode == "always" || mode == "automatic" || mode == "hidden")
            return mode;
        return Settings::Manager::getBool("auto hide resource bars", "GUI")
            ? "automatic" : "always";
    }

    std::string getNpcBarMode()
    {
        const std::string mode = Settings::Manager::getString("npc bar mode", "HUD");
        if (mode == "off" || mode == "combat" || mode == "hover" || mode == "both")
            return mode;
        return Settings::Manager::getBool("target info panel", "GUI") ? "both" : "combat";
    }

    bool npcBarShowsHover(const std::string& mode)
    {
        return mode == "hover" || mode == "both";
    }

    bool npcBarShowsCombat(const std::string& mode)
    {
        return mode == "combat" || mode == "both";
    }

    enum class HorizontalCompassMarkerKind
    {
        Enemy = 0,
        Player = 1,
        Ally = 2,
        Door = 3,
        DetectKey = 4,
        DetectEnchantment = 5,
        DetectCreature = 6
    };

    struct HorizontalCompassMarkerCandidate
    {
        MWWorld::Ptr mObject;
        std::string mIdentity;
        int mLeft = 0;
        float mDistanceSquared = 0.f;
        HorizontalCompassMarkerKind mKind = HorizontalCompassMarkerKind::Ally;
    };

    MyGUI::Colour getHorizontalCompassMarkerColour(HorizontalCompassMarkerKind kind)
    {
        switch (kind)
        {
            case HorizontalCompassMarkerKind::Enemy:
                return MyGUI::Colour(1.00f, 0.18f, 0.16f);
            case HorizontalCompassMarkerKind::Player:
                return MyGUI::Colour(0.18f, 0.42f, 0.95f);
            case HorizontalCompassMarkerKind::Ally:
                return MyGUI::Colour(0.14f, 0.74f, 0.22f);
            case HorizontalCompassMarkerKind::Door:
            case HorizontalCompassMarkerKind::DetectKey:
            case HorizontalCompassMarkerKind::DetectEnchantment:
            case HorizontalCompassMarkerKind::DetectCreature:
                // Use the same sand/gold colour as normal Morrowind location text
                // (for example the "Seyda Neen" HUD caption), not a hard-coded tint.
                return MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}"));
        }
        return MyGUI::Colour(1.f, 1.f, 1.f);
    }

    std::string getHorizontalCompassMarkerCaption(HorizontalCompassMarkerKind kind)
    {
        switch (kind)
        {
            case HorizontalCompassMarkerKind::Door:
                return "⌂";
            case HorizontalCompassMarkerKind::DetectKey:
                return "○─";
            case HorizontalCompassMarkerKind::DetectEnchantment:
                return "✦";
            case HorizontalCompassMarkerKind::DetectCreature:
                return "△";
            case HorizontalCompassMarkerKind::Enemy:
            case HorizontalCompassMarkerKind::Player:
            case HorizontalCompassMarkerKind::Ally:
                return "●";
        }
        return "●";
    }
}

namespace MWGui
{

    /**
     * Makes it possible to use ItemModel::moveItem to move an item from an inventory to the world.
     */
    class WorldItemModel : public ItemModel
    {
    public:
        WorldItemModel(float left, float top) : mLeft(left), mTop(top) {}
        virtual ~WorldItemModel() override {}
        MWWorld::Ptr copyItem (const ItemStack& item, size_t count, bool /*allowAutoEquip*/) override
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();

            MWWorld::Ptr dropped;
            if (world->canPlaceObject(mLeft, mTop))
                dropped = world->placeObject(item.mBase, mLeft, mTop, count);
            else
                dropped = world->dropObjectOnGround(world->getPlayerPtr(), item.mBase, count);
            dropped.getCellRef().setOwner("");

            /*
                Start of tes3mp addition

                Send an ID_OBJECT_PLACE packet every time an object is dropped into the world from
                the inventory screen
            */
            mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectPlace(dropped, true);
            objectList->sendObjectPlace();
            /*
                End of tes3mp addition
            */

            /*
                Start of tes3mp change (major)

                Instead of actually keeping this object as is, delete it after sending the packet
                and wait for the server to send it back with a unique mpNum of its own
            */
            MWBase::Environment::get().getWorld()->deleteObject(dropped);
            /*
                End of tes3mp change (major)
            */

            return dropped;
        }

        void removeItem (const ItemStack& item, size_t count) override { throw std::runtime_error("removeItem not implemented"); }
        ModelIndex getIndex (ItemStack item) override { throw std::runtime_error("getIndex not implemented"); }
        void update() override {}
        size_t getItemCount() override { return 0; }
        ItemStack getItem (ModelIndex index) override { throw std::runtime_error("getItem not implemented"); }

    private:
        // Where to drop the item
        float mLeft;
        float mTop;
    };


    HUD::HUD(CustomMarkerCollection &customMarkers, DragAndDrop* dragAndDrop, MWRender::LocalMap* localMapRender)
        : WindowBase("openmw_hud.layout")
        , LocalMapBase(customMarkers, localMapRender, Settings::Manager::getBool("local map hud fog of war", "Map"))
        , mGameplayHud(nullptr)
        , mHealth(nullptr)
        , mMagicka(nullptr)
        , mStamina(nullptr)
        , mDrowning(nullptr)
        , mHealthText(nullptr)
        , mMagickaText(nullptr)
        , mStaminaText(nullptr)
        , mFpsBox(nullptr)
        , mEnemyName(nullptr)
        , mEnemySummary(nullptr)
        , mWeapImage(nullptr)
        , mSpellImage(nullptr)
        , mWeapStatus(nullptr)
        , mSpellStatus(nullptr)
        , mEffectBox(nullptr)
        , mMinimap(nullptr)
        , mCrosshair(nullptr)
        , mCellNameClip(nullptr)
        , mCellNameBox(nullptr)
        , mGameTimeBox(nullptr)
        , mHorizontalCompass(nullptr)
        , mHorizontalCompassCenter(nullptr)
        , mDrowningFrame(nullptr)
        , mDrowningFlash(nullptr)
        , mHealthManaStaminaBaseLeft(0)
        , mWeapBoxBaseLeft(0)
        , mSpellBoxBaseLeft(0)
        , mMinimapBoxBaseRight(0)
        , mEffectBoxBaseRight(0)
        , mDragAndDrop(dragAndDrop)
        , mCellNameTimer(0.0f)
        , mCellNameScrollOffset(0.f)
        , mCellNameScrollPause(0.f)
        , mCellNameScrollDirection(-1)
        , mCellNameScrolling(false)
        , mWeaponSpellTimer(0.f)
        , mGameTimeUpdateTimer(0.f)
        , mGameTimeCaption("00:00")
        , mGameTimeShowingCellName(false)
        , mMapVisible(true)
        , mMinimapBaseVisible(true)
        , mEffectBaseVisible(true)
        , mCrosshairBaseVisible(true)
        , mWeaponVisible(true)
        , mSpellVisible(true)
        , mWorldMouseOver(false)
        , mHorizontalCompassAngle(0.f)
        , mHorizontalCompassDirty(true)
        , mHorizontalCompassMarkerTimer(0.f)
        , mEnemyActorId(-1)
        , mEnemyHealthTimer(-1)
        , mFocusActorScreenX(0.5f)
        , mFocusActorScreenY(0.f)
        , mFocusActorDistance(-1.f)
        , mFocusActorPanelAlpha(0.f)
        , mTargetPanelCenterX(0.f)
        , mFocusActorCurrentlyFaced(false)
        , mTargetPanelPositionInitialized(false)
        , mFpsUpdateTimer(0.f)
        , mFpsAccumulatedTime(0.f)
        , mFpsFrameCount(0)
        , mIsDrowning(false)
        , mDrowningFlashTheta(0.f)
        , mHmsBaseVisible(true)
    {
        mMainWidget->setSize(MyGUI::RenderManager::getInstance().getViewSize());
        getWidget(mGameplayHud, "GameplayHud");

        // Keep every gameplay HUD element hidden until authentication or new-character
        // registration has completely finished. The login/registration dialogs and chat
        // live outside this container and therefore remain usable.
        mGameplayHud->setVisible(false);

        // Energy bars
        getWidget(mHealthFrame, "HealthFrame");
        getWidget(mMagickaFrame, "MagickaFrame");
        getWidget(mFatigueFrame, "FatigueFrame");
        getWidget(mHealth, "Health");
        getWidget(mMagicka, "Magicka");
        getWidget(mStamina, "Stamina");
        getWidget(mEnemyHealth, "EnemyHealth");
        getWidget(mEnemyName, "EnemyName");
        getWidget(mEnemySummary, "EnemySummary");
        getWidget(mHealthText, "HealthText");
        getWidget(mMagickaText, "MagickaText");
        getWidget(mStaminaText, "StaminaText");
        getWidget(mFpsBox, "FpsText");
        mHealthManaStaminaBaseLeft = mHealthFrame->getLeft();

        mHealthFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mMagickaFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mFatigueFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);

        //Drowning bar
        getWidget(mDrowningFrame, "DrowningFrame");
        getWidget(mDrowning, "Drowning");
        getWidget(mDrowningFlash, "Flash");
        mDrowning->setProgressRange(200);

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();

        // Item and spell images and status bars
        getWidget(mWeapBox, "WeapBox");
        getWidget(mWeapImage, "WeapImage");
        getWidget(mWeapStatus, "WeapStatus");
        mWeapBoxBaseLeft = mWeapBox->getLeft();
        mWeapBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWeaponClicked);

        getWidget(mSpellBox, "SpellBox");
        getWidget(mSpellImage, "SpellImage");
        getWidget(mSpellStatus, "SpellStatus");
        mSpellBoxBaseLeft = mSpellBox->getLeft();
        mSpellBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMagicClicked);

        getWidget(mSneakBox, "SneakBox");
        mSneakBoxBaseLeft = mSneakBox->getLeft();

        getWidget(mEffectBox, "EffectBox");
        mEffectBoxBaseRight = viewSize.width - mEffectBox->getRight();

        getWidget(mMinimapBox, "MiniMapBox");
        mMinimapBoxBaseRight = viewSize.width - mMinimapBox->getRight();
        getWidget(mMinimap, "MiniMap");
        getWidget(mCompass, "Compass");
        getWidget(mMinimapButton, "MiniMapButton");
        mMinimapButton->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);

        getWidget(mCellNameClip, "CellNameClip");
        getWidget(mCellNameBox, "CellName");
        getWidget(mWeaponSpellBox, "WeaponSpellName");
        getWidget(mGameTimeBox, "GameTime");
        if (mGameTimeBox)
        {
            // The clock/location line is a single, larger caption below the compass.
            mGameTimeBox->setFontHeight(18);
            mGameTimeBox->setTextAlign(MyGUI::Align::Center);
            mGameTimeBox->setCaption(mGameTimeCaption);
        }
        // The old standalone cell-name line is intentionally retired. The location
        // now uses the same caption as the clock, so the two never overlap.
        if (mCellNameClip)
            mCellNameClip->setVisible(false);
        if (mCellNameBox)
            mCellNameBox->setVisible(false);

        getWidget(mHorizontalCompass, "HorizontalCompass");
        constexpr int horizontalCompassTickCount = 11;
        for (int i = 0; i < horizontalCompassTickCount; ++i)
        {
            MyGUI::TextBox* tick = mHorizontalCompass->createWidget<MyGUI::TextBox>("SandBrightText",
                MyGUI::IntCoord(0, 1, 48, 20), MyGUI::Align::Default);
            tick->setTextAlign(MyGUI::Align::Center);
            tick->setTextShadow(true);
            tick->setTextShadowColour(MyGUI::Colour::Black);
            tick->setNeedMouseFocus(false);
            mHorizontalCompassTicks.push_back(tick);
        }
        mHorizontalCompassCenter = mHorizontalCompass->createWidget<MyGUI::TextBox>("SandBrightText",
            MyGUI::IntCoord(mHorizontalCompass->getWidth() / 2 - 12, 14, 24, 10), MyGUI::Align::Default);
        mHorizontalCompassCenter->setCaption("^");
        mHorizontalCompassCenter->setTextAlign(MyGUI::Align::Center);
        mHorizontalCompassCenter->setTextShadow(true);
        mHorizontalCompassCenter->setTextShadowColour(MyGUI::Colour::Black);
        mHorizontalCompassCenter->setNeedMouseFocus(false);

        // Fixed widget pool avoids allocating GUI objects while actors move in and out of range.
        constexpr int horizontalCompassMarkerCount = 48;
        for (int i = 0; i < horizontalCompassMarkerCount; ++i)
        {
            MyGUI::TextBox* marker = mHorizontalCompass->createWidget<MyGUI::TextBox>("NormalText",
                MyGUI::IntCoord(0, 3, 32, 22), MyGUI::Align::Default);
            marker->setCaption("●");
            marker->setFontName("CompassMarkerFont");
            marker->setFontHeight(18);
            marker->setTextAlign(MyGUI::Align::Center);
            marker->setTextShadow(true);
            marker->setTextShadowColour(MyGUI::Colour::Black);
            marker->setNeedMouseFocus(false);
            marker->setAlpha(0.f);
            marker->setVisible(false);

            HorizontalCompassMarkerState state;
            state.mWidget = marker;
            mHorizontalCompassMarkers.push_back(state);
        }

        getWidget(mCrosshair, "Crosshair");

        LocalMapBase::init(mMinimap, mCompass);

        mMainWidget->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWorldClicked);
        mMainWidget->eventMouseMove += MyGUI::newDelegate(this, &HUD::onWorldMouseOver);
        mMainWidget->eventMouseLostFocus += MyGUI::newDelegate(this, &HUD::onWorldMouseLostFocus);

        updatePositions();
        mSpellIcons = new SpellIcons();
    }

    HUD::~HUD()
    {
        mMainWidget->eventMouseLostFocus.clear();
        mMainWidget->eventMouseMove.clear();
        mMainWidget->eventMouseButtonClick.clear();

        delete mSpellIcons;
    }

    void HUD::setValue(const std::string& id, const MWMechanics::DynamicStat<float>& value)
    {
        int current = static_cast<int>(value.getCurrent());
        int modified = static_cast<int>(value.getModified());

        // Fatigue can be negative
        if (id != "FBar")
            current = std::max(0, current);

        std::string valStr = MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
        if (id == "HBar")
        {
            mHealth->setProgressRange(std::max(0, modified));
            mHealth->setProgressPosition(std::max(0, current));
            if (mHealthText)
                mHealthText->setCaption(valStr);
            mHealthFrame->setUserString("Caption_HealthDescription", "#{sHealthDesc}\n" + valStr);
            registerBarChange(mHealthBarState, current, modified);
        }
        else if (id == "MBar")
        {
            mMagicka->setProgressRange(std::max(0, modified));
            mMagicka->setProgressPosition(std::max(0, current));
            if (mMagickaText)
                mMagickaText->setCaption(valStr);
            mMagickaFrame->setUserString("Caption_HealthDescription", "#{sMagDesc}\n" + valStr);
            registerBarChange(mMagickaBarState, current, modified);
        }
        else if (id == "FBar")
        {
            mStamina->setProgressRange(std::max(0, modified));
            mStamina->setProgressPosition(std::max(0, current));
            if (mStaminaText)
                mStaminaText->setCaption(valStr);
            mFatigueFrame->setUserString("Caption_HealthDescription", "#{sFatDesc}\n" + valStr);
            registerBarChange(mStaminaBarState, current, modified);
        }
    }

    void HUD::setDrowningTimeLeft(float time, float maxTime)
    {
        size_t progress = static_cast<size_t>(time / maxTime * 200);
        mDrowning->setProgressPosition(progress);

        bool isDrowning = (progress == 0);
        if (isDrowning && !mIsDrowning) // Just started drowning
            mDrowningFlashTheta = 0.0f; // Start out on bright red every time.

        mDrowningFlash->setVisible(isDrowning);
        mIsDrowning = isDrowning;
    }

    void HUD::setDrowningBarVisible(bool visible)
    {
        mDrowningFrame->setVisible(visible);
    }

    void HUD::onWorldClicked(MyGUI::Widget* _sender)
    {
        if (!MWBase::Environment::get().getWindowManager ()->isGuiMode ())
            return;

        MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            // drop item into the gameworld
            MWBase::Environment::get().getWorld()->breakInvisibility(
                        MWMechanics::getPlayer());

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            WorldItemModel drop (mouseX, mouseY);
            mDragAndDrop->drop(&drop, nullptr);

            winMgr->changePointer("arrow");
        }
        else
        {
            GuiMode mode = winMgr->getMode();

            if (!winMgr->isConsoleMode() && (mode != GM_Container) && (mode != GM_Inventory))
                return;

            MWWorld::Ptr object = MWBase::Environment::get().getWorld()->getFacedObject();

            if (winMgr->isConsoleMode())
                winMgr->setConsoleSelectedObject(object);
            else //if ((mode == GM_Container) || (mode == GM_Inventory))
            {
                // pick up object
                if (!object.isEmpty())
                /*
                    Start of tes3mp change (major)

                    Disable unilateral picking up of objects on this client

                    Instead, send an ID_OBJECT_ACTIVATE packet every time an item is made to pick up
                    an item here, and expect the server's reply to our packet to cause the actual
                    picking up of items
                */
                    //winMgr->getInventoryWindow()->pickUpObject(object);
                {
                    mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
                    objectList->reset();
                    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                    objectList->addObjectActivate(object, MWMechanics::getPlayer());
                    objectList->sendObjectActivate();
                }
                /*
                    End of tes3mp change (major)
                */
            }
        }
    }

    void HUD::onWorldMouseOver(MyGUI::Widget* _sender, int x, int y)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            mWorldMouseOver = false;

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            MWBase::World* world = MWBase::Environment::get().getWorld();

            // if we can't drop the object at the wanted position, show the "drop on ground" cursor.
            bool canDrop = world->canPlaceObject(mouseX, mouseY);

            if (!canDrop)
                MWBase::Environment::get().getWindowManager()->changePointer("drop_ground");
            else
                MWBase::Environment::get().getWindowManager()->changePointer("arrow");

        }
        else
        {
            MWBase::Environment::get().getWindowManager()->changePointer("arrow");
            mWorldMouseOver = true;
        }
    }

    void HUD::onWorldMouseLostFocus(MyGUI::Widget* _sender, MyGUI::Widget* _new)
    {
        MWBase::Environment::get().getWindowManager()->changePointer("arrow");
        mWorldMouseOver = false;
    }

    void HUD::onHMSClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Stats);
    }

    void HUD::onMapClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Map);
    }

    void HUD::onWeaponClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Inventory);
    }

    void HUD::onMagicClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Magic);
    }


    void HUD::onResChange(int width, int height)
    {
        mMainWidget->setSize(width, height);
        updatePositions();
    }

    void HUD::setCellName(const std::string& cellName)
    {
        if (mCellName != cellName)
        {
            mCellName = cellName;
            mCellNameTimer = Settings::Manager::getBool("show cell name", "HUD")
                ? sCompassInfoSequenceDuration : 0.f;

            // Cell names are now presented in the clock line under the compass.
            // Keep the legacy layout widgets hidden so there is only one HUD caption.
            if (mCellNameClip)
                mCellNameClip->setVisible(false);
            if (mCellNameBox)
                mCellNameBox->setVisible(false);
        }
    }

    void HUD::onFrame(float dt)
    {
        const bool loginFinished = mwmp::Main::isInitialized()
            && mwmp::Main::get().getLocalPlayer()
            && mwmp::Main::get().getLocalPlayer()->isLoggedIn();

        // LocalPlayer::isLoggedIn() becomes true only after an existing character has
        // been received or after the complete new-character registration sequence.
        // Toggling one parent preserves the individual visibility state of every HUD
        // child, so map/settings/autohide choices are restored correctly after login.
        if (mGameplayHud && mGameplayHud->getVisible() != loginFinished)
            mGameplayHud->setVisible(loginFinished);

        LocalMapBase::onFrame(dt);

        // Apply HUD preferences live so changes made in the dedicated HUD tab do
        // not require reopening the game or rebuilding the HUD layout.
        const bool showMinimap = mMinimapBaseVisible && Settings::Manager::getBool("show minimap", "HUD");
        if (mMinimapBox->getVisible() != showMinimap)
        {
            mMinimapBox->setVisible(showMinimap);
            updatePositions();
        }
        if (mCompass)
            mCompass->setVisible(showMinimap);
        // Clock/location visibility and timed replacement are handled together below.
        if (mEffectBox)
            mEffectBox->setVisible(mEffectBaseVisible && Settings::Manager::getBool("show status effects", "HUD"));
        if (mFpsBox)
            mFpsBox->setVisible(Settings::Manager::getBool("show fps", "HUD"));
        if (mCrosshair)
            mCrosshair->setVisible(mCrosshairBaseVisible && Settings::Manager::getBool("crosshair", "HUD"));

        updateHorizontalCompass();
        updateHorizontalCompassMarkers(dt);

        updateGameTimeAndCellName(dt);

        mWeaponSpellTimer -= dt;
        if (mWeaponSpellTimer < 0)
            mWeaponSpellBox->setVisible(false);

        mFpsAccumulatedTime += dt;
        ++mFpsFrameCount;
        mFpsUpdateTimer -= dt;
        if (mFpsBox && mFpsUpdateTimer <= 0.f)
        {
            const float safeTime = std::max(0.0001f, mFpsAccumulatedTime);
            const int fps = static_cast<int>(std::lround(static_cast<double>(mFpsFrameCount) / safeTime));
            mFpsBox->setCaption(MyGUI::utility::toString(fps));
            mFpsUpdateTimer = 0.25f;
            mFpsAccumulatedTime = 0.f;
            mFpsFrameCount = 0;
        }

        updateFocusedTargetPanel(dt);

        const std::string npcBarMode = getNpcBarMode();
        const bool showHoverNpcBar = npcBarShowsHover(npcBarMode);
        const bool showCombatNpcBar = npcBarShowsCombat(npcBarMode);
        const bool focusedTargetAlive = !mFocusActor.isEmpty()
            && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead();
        const bool dialogueOpen = MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        const bool focusedTargetPanel = showHoverNpcBar && focusedTargetAlive && !dialogueOpen
            && mFocusActorPanelAlpha > 0.01f;
        const bool combatTargetPanel = showCombatNpcBar && mEnemyActorId != -1;

        mEnemyHealthTimer -= dt;
        if (mEnemyHealth->getVisible() && mEnemyHealthTimer < 0 && !focusedTargetPanel)
        {
            mEnemyHealth->setVisible(false);
        }

        if (mIsDrowning)
            mDrowningFlashTheta += dt * osg::PI*2;

        mSpellIcons->updateWidgets(mEffectBox, true);
        if (mEffectBox)
            mEffectBox->setVisible(mEffectBaseVisible
                && Settings::Manager::getBool("show status effects", "HUD")
                && mEffectBox->getChildCount() > 0);

        if ((focusedTargetPanel || combatTargetPanel) && mEnemyHealth->getVisible())
        {
            updateEnemyHealthBar();
        }

        if (focusedTargetPanel)
        {
            mEnemyHealth->setVisible(true);
            if (mEnemyName)
                mEnemyName->setVisible(true);
            if (mEnemySummary)
                mEnemySummary->setVisible(true);
            updateEnemyHealthBar();
        }

        if (mIsDrowning)
        {
            float intensity = (cos(mDrowningFlashTheta) + 2.0f) / 3.0f;

            mDrowningFlash->setAlpha(intensity);
        }

        MWMechanics::DrawState_ drawState = MWMechanics::DrawState_Nothing;
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (!player.isEmpty())
            drawState = player.getClass().getCreatureStats(player).getDrawState();

        if ((!showHoverNpcBar && !showCombatNpcBar) || (dialogueOpen && mEnemyActorId == -1))
            mEnemyHealth->setVisible(false);

        const bool showFocusedTargetInfo = focusedTargetPanel && mEnemyHealth->getVisible();
        if (mEnemyName)
            mEnemyName->setVisible(showFocusedTargetInfo);
        if (mEnemySummary)
            mEnemySummary->setVisible(showFocusedTargetInfo);

        updateAutoHideBar(mHealthFrame, mHealthBarState, dt, false);
        updateAutoHideBar(mMagickaFrame, mMagickaBarState, dt,
            drawState == MWMechanics::DrawState_Spell, mSpellBox);
        updateAutoHideBar(mFatigueFrame, mStaminaBarState, dt,
            drawState == MWMechanics::DrawState_Weapon, mWeapBox);
    }

    void HUD::updateGameTimeAndCellName(float dt)
    {
        if (!mGameTimeBox)
            return;

        dt = std::max(0.f, dt);
        const bool showTime = Settings::Manager::getBool("show game time", "HUD");
        const bool showLocation = Settings::Manager::getBool("show cell name", "HUD");

        // Keep the underlying time string current even while the location name is shown,
        // so the clock is correct immediately when it fades back in.
        mGameTimeUpdateTimer -= dt;
        if (mGameTimeUpdateTimer <= 0.f)
        {
            const float gameHour = MWBase::Environment::get().getWorld()->getTimeStamp().getHour();
            int hours = static_cast<int>(std::floor(gameHour)) % 24;
            int minutes = static_cast<int>(std::floor((gameHour - std::floor(gameHour)) * 60.f + 0.5f));
            if (minutes >= 60)
            {
                minutes = 0;
                hours = (hours + 1) % 24;
            }

            std::ostringstream stream;
            stream << std::setfill('0') << std::setw(2) << hours << ':'
                   << std::setfill('0') << std::setw(2) << minutes;
            mGameTimeCaption = stream.str();
            mGameTimeUpdateTimer = 0.2f;
        }

        if (!showLocation)
            mCellNameTimer = 0.f;
        else if (mCellNameTimer > 0.f)
            mCellNameTimer = std::max(0.f, mCellNameTimer - dt);

        bool displayLocation = false;
        float alpha = 1.f;
        const bool locationSequence = showLocation && !mCellName.empty() && mCellNameTimer > 0.f;

        if (locationSequence)
        {
            // If the clock itself is disabled, the location can appear immediately; there
            // is no visible time caption to fade away first.
            if (!showTime)
            {
                displayLocation = true;
                alpha = 1.f;
            }
            else
            {
                const float elapsed = sCompassInfoSequenceDuration - mCellNameTimer;
                const float fade = sCompassInfoFadeDuration;
                const float locationEnd = fade + sCompassLocationHoldDuration;

                if (elapsed < fade)
                {
                    // Phase 1: only the clock fades out.
                    displayLocation = false;
                    alpha = 1.f - elapsed / fade;
                }
                else if (elapsed < locationEnd)
                {
                    // Location itself is steady and fully opaque.
                    displayLocation = true;
                    alpha = 1.f;
                }
                else
                {
                    // Switch back to the clock at zero alpha and fade only the time in.
                    displayLocation = false;
                    alpha = (elapsed - locationEnd) / fade;
                }
            }
        }

        alpha = std::max(0.f, std::min(1.f, alpha));
        const bool visible = displayLocation ? showLocation : showTime;
        mGameTimeBox->setVisible(visible);
        mGameTimeBox->setAlpha(visible ? alpha : 0.f);

        if (!visible)
            return;

        if (displayLocation)
        {
            if (!mGameTimeShowingCellName)
            {
                mGameTimeBox->setCaptionWithReplacing("#{sCell=" + mCellName + "}");
                mGameTimeShowingCellName = true;
            }
        }
        else
        {
            mGameTimeBox->setCaption(mGameTimeCaption);
            mGameTimeShowingCellName = false;
        }
    }

    void HUD::setPlayerDir(float x, float y)
    {
        LocalMapBase::setPlayerDir(x, y);
        const float angle = std::atan2(x, y);
        if (angle != mHorizontalCompassAngle)
        {
            mHorizontalCompassAngle = angle;
            mHorizontalCompassDirty = true;
        }
    }

    void HUD::updateHorizontalCompass()
    {
        if (!mHorizontalCompass)
            return;

        const bool enabled = Settings::Manager::getBool("horizontal compass", "HUD");
        if (mHorizontalCompass->getVisible() != enabled)
            mHorizontalCompass->setVisible(enabled);
        if (!enabled || !mHorizontalCompassDirty)
            return;

        static const char* directions[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        constexpr float stepDegrees = 15.f;
        constexpr float pixelsPerDegree = 4.f;
        constexpr int labelWidth = 48;

        float heading = osg::RadiansToDegrees(mHorizontalCompassAngle);
        while (heading < 0.f)
            heading += 360.f;
        while (heading >= 360.f)
            heading -= 360.f;

        const int nearestStep = static_cast<int>(std::floor(heading / stepDegrees + 0.5f));
        const int halfCount = static_cast<int>(mHorizontalCompassTicks.size()) / 2;
        for (size_t i = 0; i < mHorizontalCompassTicks.size(); ++i)
        {
            const int markerStep = nearestStep + static_cast<int>(i) - halfCount;
            const float markerDegrees = markerStep * stepDegrees;
            const float delta = markerDegrees - heading;
            const int left = static_cast<int>(std::lround(
                mHorizontalCompass->getWidth() * 0.5f + delta * pixelsPerDegree - labelWidth * 0.5f));
            MyGUI::TextBox* tick = mHorizontalCompassTicks[i];
            tick->setPosition(left, 1);

            int wrappedStep = markerStep % 24;
            if (wrappedStep < 0)
                wrappedStep += 24;
            if (wrappedStep % 3 == 0)
                tick->setCaption(directions[(wrappedStep / 3) % 8]);
            else
                tick->setCaption("|");
        }

        mHorizontalCompassDirty = false;
    }

    void HUD::updateHorizontalCompassMarkers(float dt)
    {
        if (!mHorizontalCompass)
            return;

        dt = std::max(0.f, dt);
        const bool enabled = Settings::Manager::getBool("horizontal compass", "HUD")
            && mGameplayHud && mGameplayHud->getVisible();
        if (!enabled)
        {
            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
            {
                state.mObject = MWWorld::Ptr();
                state.mIdentity.clear();
                state.mAlpha = 0.f;
                state.mTargetAlpha = 0.f;
                state.mSeen = false;
                state.mWidget->setAlpha(0.f);
                state.mWidget->setVisible(false);
            }
            mHorizontalCompassMarkerTimer = 0.f;
            return;
        }

        mHorizontalCompassMarkerTimer -= dt;
        if (mHorizontalCompassMarkerTimer <= 0.f)
        {
            mHorizontalCompassMarkerTimer = 0.075f;

            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                state.mSeen = false;

            MWBase::World* world = MWBase::Environment::get().getWorld();
            MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
            if (world && mechanics)
            {
                const MWWorld::Ptr player = world->getPlayerPtr();
                if (!player.isEmpty() && player.isInCell())
                {
                    // Lower enum value has visual priority when the same reference belongs
                    // to several groups (for example an enemy also found by Detect Animal).
                    std::map<MWWorld::Ptr, HorizontalCompassMarkerKind> objects;
                    const auto addObject = [&objects](const MWWorld::Ptr& object, HorizontalCompassMarkerKind kind)
                    {
                        if (object.isEmpty())
                            return;
                        const auto [it, inserted] = objects.emplace(object, kind);
                        if (!inserted && static_cast<int>(kind) < static_cast<int>(it->second))
                            it->second = kind;
                    };

                    for (const MWWorld::Ptr& enemy : mechanics->getActorsFighting(player))
                        addObject(enemy, HorizontalCompassMarkerKind::Enemy);

                    std::set<MWWorld::Ptr> allies;
                    mechanics->getActorsSidingWith(player, allies);
                    for (const MWWorld::Ptr& ally : allies)
                    {
                        // Network players have their own blue marker; followers and summons stay green.
                        if (!mwmp::PlayerList::isDedicatedPlayer(ally))
                            addObject(ally, HorizontalCompassMarkerKind::Ally);
                    }

                    if (mwmp::Main::isInitialized() && mwmp::Main::get().getLocalPlayer())
                    {
                        const std::vector<RakNet::RakNetGUID> playerGuids
                            = mwmp::PlayerList::getPlayersInCell(*player.getCell()->getCell());
                        for (const RakNet::RakNetGUID& guid : playerGuids)
                        {
                            mwmp::DedicatedPlayer* remotePlayer = mwmp::PlayerList::getPlayer(guid);
                            if (remotePlayer)
                                addObject(remotePlayer->getPtr(), HorizontalCompassMarkerKind::Player);
                        }
                    }

                    // Reuse the engine's native detection logic. The effective magnitude
                    // already combines spells, abilities, potions and enchanted items, so
                    // these compass markers obey Detect Key / Detect Enchantment / Detect Animal
                    // ranges exactly like the local-map magic markers do.
                    const auto addDetected = [world, &player, &addObject](MWBase::World::DetectionType type,
                        HorizontalCompassMarkerKind kind)
                    {
                        std::vector<MWWorld::Ptr> detected;
                        world->listDetectedReferences(player, detected, type);
                        for (const MWWorld::Ptr& object : detected)
                            addObject(object, kind);
                    };
                    addDetected(MWBase::World::Detect_Key, HorizontalCompassMarkerKind::DetectKey);
                    addDetected(MWBase::World::Detect_Enchantment, HorizontalCompassMarkerKind::DetectEnchantment);
                    addDetected(MWBase::World::Detect_Creature, HorizontalCompassMarkerKind::DetectCreature);

                    constexpr float markerRange = 8192.f;
                    constexpr float markerRangeSquared = markerRange * markerRange;
                    constexpr float pixelsPerDegree = 4.f;
                    constexpr int compassInnerPadding = 10;
                    const int markerWidth = mHorizontalCompassMarkers.empty()
                        ? 32
                        : mHorizontalCompassMarkers.front().mWidget->getWidth();
                    const osg::Vec3f playerPosition = player.getRefData().getPosition().asVec3();
                    const int compassWidth = mHorizontalCompass->getWidth();
                    const float compassCenter = compassWidth * 0.5f;
                    const float maximumMarkerOffset = std::max(1.f,
                        compassCenter - compassInnerPadding - markerWidth * 0.5f);
                    const float maximumVisibleDegrees = maximumMarkerOffset / pixelsPerDegree;

                    std::vector<HorizontalCompassMarkerCandidate> candidates;
                    candidates.reserve(objects.size() + 24);

                    const auto appendCandidate = [&](const MWWorld::Ptr& object, const std::string& identity,
                        HorizontalCompassMarkerKind kind, float worldX, float worldY, float distanceSquared)
                    {
                        if (distanceSquared > markerRangeSquared || distanceSquared < 1.f)
                            return;

                        const float bearing = std::atan2(worldX - playerPosition.x(), worldY - playerPosition.y());
                        float relativeAngle = bearing - mHorizontalCompassAngle;
                        while (relativeAngle > osg::PI)
                            relativeAngle -= osg::PI * 2.f;
                        while (relativeAngle < -osg::PI)
                            relativeAngle += osg::PI * 2.f;

                        // Skyrim-like edge behaviour: markers outside the visible compass arc
                        // stay pinned to the nearest edge rather than disappearing behind you.
                        const float relativeDegrees = osg::RadiansToDegrees(relativeAngle);
                        const float displayedDegrees = std::max(-maximumVisibleDegrees,
                            std::min(maximumVisibleDegrees, relativeDegrees));
                        const int left = static_cast<int>(std::lround(
                            compassCenter + displayedDegrees * pixelsPerDegree - markerWidth * 0.5f));

                        HorizontalCompassMarkerCandidate candidate;
                        candidate.mObject = object;
                        candidate.mIdentity = identity;
                        candidate.mLeft = left;
                        candidate.mDistanceSquared = distanceSquared;
                        candidate.mKind = kind;
                        candidates.push_back(candidate);
                    };

                    for (const auto& [object, kind] : objects)
                    {
                        if (object == player || !object.isInCell())
                            continue;
                        if (object.getRefData().getCount() <= 0 || !object.getRefData().isEnabled()
                            || object.getRefData().isDeleted())
                            continue;

                        const bool actorMarker = kind == HorizontalCompassMarkerKind::Enemy
                            || kind == HorizontalCompassMarkerKind::Player
                            || kind == HorizontalCompassMarkerKind::Ally
                            || kind == HorizontalCompassMarkerKind::DetectCreature;
                        if (actorMarker)
                        {
                            if (!object.getClass().isActor() || object.getClass().getCreatureStats(object).isDead())
                                continue;
                            // Combat/follower/network-player markers are cell-local. Detect Animal
                            // may originate in an adjacent active exterior and is allowed to remain.
                            if (kind != HorizontalCompassMarkerKind::DetectCreature
                                && object.getCell() != player.getCell())
                                continue;
                        }

                        const osg::Vec3f position = object.getRefData().getPosition().asVec3();
                        const float dx = position.x() - playerPosition.x();
                        const float dy = position.y() - playerPosition.y();
                        appendCandidate(object, std::string(), kind, position.x(), position.y(), dx * dx + dy * dy);
                    }

                    // Door markers are now an exterior-navigation aid. While outdoors,
                    // show nearby teleport doors that lead into interiors (buildings, caves, etc.).
                    // Interior-to-interior/exterior door clutter is intentionally omitted.
                    if (player.getCell()->getCell()->isExterior())
                    {
                        std::vector<MWBase::World::DoorMarker> doors;
                        world->getDoorMarkers(player.getCell(), doors);
                        for (const MWBase::World::DoorMarker& door : doors)
                        {
                            // mPaged == false identifies an interior destination.
                            if (door.dest.mPaged)
                                continue;

                            const float dx = door.x - playerPosition.x();
                            const float dy = door.y - playerPosition.y();
                            std::ostringstream identity;
                            identity << "door:" << door.name << ':'
                                << static_cast<int>(std::lround(door.x)) << ':'
                                << static_cast<int>(std::lround(door.y));
                            appendCandidate(MWWorld::Ptr(), identity.str(), HorizontalCompassMarkerKind::Door,
                                door.x, door.y, dx * dx + dy * dy);
                        }
                    }

                    std::sort(candidates.begin(), candidates.end(),
                        [](const HorizontalCompassMarkerCandidate& left,
                            const HorizontalCompassMarkerCandidate& right)
                        {
                            if (left.mDistanceSquared != right.mDistanceSquared)
                                return left.mDistanceSquared < right.mDistanceSquared;
                            return static_cast<int>(left.mKind) < static_cast<int>(right.mKind);
                        });

                    const size_t candidateCount = std::min(candidates.size(), mHorizontalCompassMarkers.size());
                    for (size_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
                    {
                        const HorizontalCompassMarkerCandidate& candidate = candidates[candidateIndex];
                        HorizontalCompassMarkerState* selectedState = nullptr;

                        // Keep the same widget attached to the same object/door so markers
                        // do not swap positions or blink between 75 ms scans.
                        for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                        {
                            const bool sameObject = !candidate.mObject.isEmpty()
                                && !state.mObject.isEmpty() && state.mObject == candidate.mObject;
                            const bool sameIdentity = candidate.mObject.isEmpty()
                                && state.mObject.isEmpty() && !candidate.mIdentity.empty()
                                && state.mIdentity == candidate.mIdentity;
                            if (sameObject || sameIdentity)
                            {
                                selectedState = &state;
                                break;
                            }
                        }

                        if (!selectedState)
                        {
                            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                            {
                                if (!state.mSeen && state.mObject.isEmpty() && state.mIdentity.empty())
                                {
                                    selectedState = &state;
                                    break;
                                }
                            }
                        }
                        if (!selectedState)
                        {
                            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                            {
                                if (state.mSeen)
                                    continue;
                                if (!selectedState || state.mAlpha < selectedState->mAlpha)
                                    selectedState = &state;
                            }
                        }
                        if (!selectedState)
                            continue;

                        const bool newMarker = (!candidate.mObject.isEmpty()
                                && (selectedState->mObject.isEmpty() || selectedState->mObject != candidate.mObject))
                            || (candidate.mObject.isEmpty()
                                && (selectedState->mIdentity != candidate.mIdentity || !selectedState->mObject.isEmpty()));
                        const float oldTargetLeft = selectedState->mTargetLeft;
                        selectedState->mObject = candidate.mObject;
                        selectedState->mIdentity = candidate.mIdentity;
                        selectedState->mSeen = true;
                        selectedState->mTargetLeft = static_cast<float>(candidate.mLeft);
                        selectedState->mTargetAlpha = 1.f;
                        selectedState->mWidget->setCaption(getHorizontalCompassMarkerCaption(candidate.mKind));
                        selectedState->mWidget->setTextColour(getHorizontalCompassMarkerColour(candidate.mKind));

                        if (newMarker)
                        {
                            selectedState->mCurrentLeft = selectedState->mTargetLeft;
                            selectedState->mAlpha = 0.f;
                            selectedState->mWidget->setAlpha(0.f);
                        }
                        else if (std::abs(selectedState->mTargetLeft - oldTargetLeft) > compassWidth * 0.65f)
                        {
                            // Crossing the exact rear direction changes the nearest edge. Snap only this
                            // wraparound transition so the marker does not incorrectly travel through centre.
                            selectedState->mCurrentLeft = selectedState->mTargetLeft;
                        }
                    }
                }
            }

            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
            {
                if (!state.mSeen)
                    state.mTargetAlpha = 0.f;
            }
        }

        // Position is exponentially smoothed each frame. Alpha uses separate fade-in/fade-out
        // speeds so a newly detected target appears quickly but disappears more softly.
        const float positionBlend = 1.f - std::exp(-14.f * dt);
        constexpr float fadeInSpeed = 7.5f;
        constexpr float fadeOutSpeed = 4.5f;
        for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
        {
            state.mCurrentLeft += (state.mTargetLeft - state.mCurrentLeft) * positionBlend;
            if (state.mTargetAlpha > state.mAlpha)
                state.mAlpha = std::min(state.mTargetAlpha, state.mAlpha + fadeInSpeed * dt);
            else
                state.mAlpha = std::max(state.mTargetAlpha, state.mAlpha - fadeOutSpeed * dt);

            const bool visible = state.mAlpha > 0.01f || state.mTargetAlpha > 0.f;
            state.mWidget->setVisible(visible);
            if (visible)
            {
                constexpr int compassInnerPadding = 10;
                const int markerWidth = state.mWidget->getWidth();
                const float minimumLeft = static_cast<float>(compassInnerPadding);
                const float maximumLeft = static_cast<float>(std::max(
                    compassInnerPadding, mHorizontalCompass->getWidth() - compassInnerPadding - markerWidth));
                state.mCurrentLeft = std::max(minimumLeft, std::min(maximumLeft, state.mCurrentLeft));
                state.mWidget->setPosition(static_cast<int>(std::lround(state.mCurrentLeft)), 3);
                state.mWidget->setAlpha(state.mAlpha);
            }
            else
            {
                state.mObject = MWWorld::Ptr();
                state.mIdentity.clear();
                state.mAlpha = 0.f;
                state.mWidget->setAlpha(0.f);
            }
        }
    }


    void HUD::registerBarChange(AutoHideBarState& state, int current, int modified)
    {
        const bool firstUpdate = !state.initialized;
        const bool maximumChanged = state.initialized && state.modified != modified;
        const bool valueDecreased = state.initialized && current < state.current;

        state.current = current;
        state.modified = modified;
        state.initialized = true;

        // Show a bar when the resource is actually spent/damaged or its maximum changes.
        // Passive regeneration must not continuously restart the auto-hide timer.
        if (firstUpdate || maximumChanged || valueDecreased)
        {
            state.idleTimer = 0.f;
            state.alpha = 1.f;
        }
    }

    void HUD::applyBarAlpha(MyGUI::Widget* widget, float alpha)
    {
        if (!widget)
            return;

        widget->setAlpha(std::max(0.f, std::min(1.f, alpha)));
    }

    void HUD::updateAutoHideBar(MyGUI::Widget* frame, AutoHideBarState& state, float dt,
        bool forceVisible, MyGUI::Widget* persistentIcon)
    {
        if (!frame || !state.initialized)
            return;

        const bool barEnabled = frame == mHealthFrame
            ? Settings::Manager::getBool("show health bar", "HUD")
            : (frame == mMagickaFrame
                ? Settings::Manager::getBool("show magicka bar", "HUD")
                : Settings::Manager::getBool("show stamina bar", "HUD"));

        if (!mHmsBaseVisible)
        {
            frame->setVisible(false);
            return;
        }

        const auto applyResourceState = [&](float alpha)
        {
            alpha = std::max(0.f, std::min(1.f, alpha));
            const std::string boxMode = persistentIcon ? getWeaponSpellBoxMode() : "hidden";
            const bool keepIcon = persistentIcon && boxMode != "hidden";

            if (!keepIcon)
            {
                frame->setVisible(alpha > 0.f);
                applyBarAlpha(frame, alpha);
                if (alpha > 0.f)
                {
                    for (unsigned int i = 0; i < frame->getChildCount(); ++i)
                    {
                        MyGUI::Widget* child = frame->getChildAt(i);
                        const bool iconAllowed = child != persistentIcon
                            || (persistentIcon == mWeapBox ? mWeaponVisible : mSpellVisible);
                        child->setVisible(iconAllowed);
                        applyBarAlpha(child, 1.f);
                    }
                }
                return;
            }

            // Weapon and spell boxes live inside the stamina/magicka frame. Keep the
            // parent alive, fade only the bar children, and apply the selected box mode.
            frame->setVisible(true);
            applyBarAlpha(frame, 1.f);
            for (unsigned int i = 0; i < frame->getChildCount(); ++i)
            {
                MyGUI::Widget* child = frame->getChildAt(i);
                if (child == persistentIcon)
                    continue;
                child->setVisible(alpha > 0.f);
                applyBarAlpha(child, alpha);
            }

            const bool iconAllowed = persistentIcon == mWeapBox ? mWeaponVisible : mSpellVisible;
            persistentIcon->setVisible(iconAllowed);
            const float persistentAlpha = boxMode == "visible" ? 1.f : 0.4f;
            applyBarAlpha(persistentIcon, std::max(persistentAlpha, alpha));
        };

        const std::string resourceMode = getResourceBarMode();
        if (!barEnabled || resourceMode == "hidden")
        {
            applyResourceState(0.f);
            return;
        }

        if (resourceMode == "always")
        {
            state.alpha = 1.f;
            applyResourceState(1.f);
            return;
        }

        // Keep the relevant resource bar visible for as long as the player is
        // actively holding a weapon or has magic readied. Start the normal
        // auto-hide delay only after the weapon/spell is put away.
        if (forceVisible)
        {
            state.idleTimer = 0.f;
            state.alpha = 1.f;
            applyResourceState(1.f);
            return;
        }

        state.idleTimer += dt;

        const bool isFull = state.modified <= 0 || state.current >= state.modified;
        const float hideDelay = isFull ? 7.f : 20.f;
        const float fadeDuration = 0.35f;

        float targetAlpha = 1.f;
        if (state.idleTimer > hideDelay)
            targetAlpha = std::max(0.f, 1.f - (state.idleTimer - hideDelay) / fadeDuration);

        state.alpha = targetAlpha;
        applyResourceState(state.alpha);
    }

    void HUD::setSelectedSpell(const std::string& spellId, int successChancePercent)
    {
        const ESM::Spell* spell =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().find(spellId);

        std::string spellName = spell->mName;
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(successChancePercent);

        mSpellBox->setUserString("ToolTipType", "Spell");
        mSpellBox->setUserString("Spell", spellId);

        // use the icon of the first effect
        const ESM::MagicEffect* effect =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(spell->mEffects.mList.front().mEffectID);

        std::string icon = effect->mIcon;
        int slashPos = icon.rfind('\\');
        icon.insert(slashPos+1, "b_");
        icon = MWBase::Environment::get().getWindowManager()->correctIconPath(icon);

        mSpellImage->setSpellIcon(icon);
    }

    void HUD::setSelectedEnchantItem(const MWWorld::Ptr& item, int chargePercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = itemName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(chargePercent);

        mSpellBox->setUserString("ToolTipType", "ItemPtr");
        mSpellBox->setUserData(MWWorld::Ptr(item));

        mSpellImage->setItem(item);
    }

    void HUD::setSelectedWeapon(const MWWorld::Ptr& item, int durabilityPercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaption(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "ItemPtr");
        mWeapBox->setUserData(MWWorld::Ptr(item));

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(durabilityPercent);

        mWeapImage->setItem(item);
    }

    void HUD::unsetSelectedSpell()
    {
        std::string spellName = "#{sNone}";
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaptionWithReplacing(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(0);
        mSpellImage->setItem(MWWorld::Ptr());
        mSpellBox->clearUserStrings();
    }

    void HUD::unsetSelectedWeapon()
    {
        std::string itemName = "#{sSkillHandtohand}";
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaptionWithReplacing(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(0);

        MWBase::World *world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();

        mWeapImage->setItem(MWWorld::Ptr());
        std::string icon = (player.getClass().getNpcStats(player).isWerewolf()) ? "icons\\k\\tx_werewolf_hand.dds" : "icons\\k\\stealth_handtohand.dds";
        mWeapImage->setIcon(icon);

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "Layout");
        mWeapBox->setUserString("ToolTipLayout", "HandToHandToolTip");
        mWeapBox->setUserString("Caption_HandToHandText", itemName);
        mWeapBox->setUserString("ImageTexture_HandToHandImage", icon);
    }

    void HUD::setCrosshairVisible(bool visible)
    {
        mCrosshairBaseVisible = visible;
        mCrosshair->setVisible(visible && Settings::Manager::getBool("crosshair", "HUD"));
    }
    
    void HUD::setCrosshairOwned(bool owned)
    {
        const int size = owned ? 32 : 64;
        mCrosshair->changeWidgetSkin(owned ? "HUD_Crosshair_Owned" : "HUD_Crosshair");

        // Keep both reticles exactly centred. The ownership hand is intentionally
        // half the size of the normal crosshair and must not inherit its 64x64 box.
        mCrosshair->setCoord(
            (mMainWidget->getWidth() - size) / 2,
            (mMainWidget->getHeight() - size) / 2,
            size, size);
    }
    
    void HUD::setHmsVisible(bool visible)
    {
        mHmsBaseVisible = visible;

        mHealth->setVisible(visible);
        mMagicka->setVisible(visible);
        mStamina->setVisible(visible);

        if (!visible)
        {
            mHealthFrame->setVisible(false);
            mMagickaFrame->setVisible(false);
            mFatigueFrame->setVisible(false);
        }
        else
        {
            registerBarChange(mHealthBarState, mHealthBarState.current, mHealthBarState.modified);
            registerBarChange(mMagickaBarState, mMagickaBarState.current, mMagickaBarState.modified);
            registerBarChange(mStaminaBarState, mStaminaBarState.current, mStaminaBarState.modified);

            mHealthFrame->setVisible(true);
            mMagickaFrame->setVisible(true);
            mFatigueFrame->setVisible(true);
            applyBarAlpha(mHealthFrame, 1.f);
            applyBarAlpha(mMagickaFrame, 1.f);
            applyBarAlpha(mFatigueFrame, 1.f);
        }

        updatePositions();
    }

    void HUD::setWeapVisible(bool visible)
    {
        mWeapBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSpellVisible(bool visible)
    {
        mSpellBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSneakVisible(bool visible)
    {
        mSneakBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setEffectVisible(bool visible)
    {
        mEffectBaseVisible = visible;
        mEffectBox->setVisible(visible && Settings::Manager::getBool("show status effects", "HUD"));
        updatePositions();
    }

    void HUD::setMinimapVisible(bool visible)
    {
        mMinimapBaseVisible = visible;
        mMinimapBox->setVisible(visible && Settings::Manager::getBool("show minimap", "HUD"));
        updatePositions();
    }

    void HUD::updatePositions()
    {
        int weapDx = 0, spellDx = 0;
        if (!mHealth->getVisible())
            spellDx = weapDx = mWeapBoxBaseLeft - mHealthManaStaminaBaseLeft;

        if (!mWeapBox->getVisible())
            spellDx += mSpellBoxBaseLeft - mWeapBoxBaseLeft;

        mWeaponVisible = mWeapBox->getVisible();
        mSpellVisible = mSpellBox->getVisible();
        if (!mWeaponVisible && !mSpellVisible)
            mWeaponSpellBox->setVisible(false);

        mWeapBox->setPosition(mWeapBoxBaseLeft - weapDx, mWeapBox->getTop());
        mSpellBox->setPosition(mSpellBoxBaseLeft - spellDx, mSpellBox->getTop());

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        mSneakBox->setPosition((viewSize.width - mSneakBox->getWidth()) / 2,
                               (viewSize.height - mSneakBox->getHeight()) / 2);

        if (mHorizontalCompass && mGameTimeBox)
        {
            // One centered information line directly under the horizontal compass.
            // It is deliberately wider than the old clock so long location names fit.
            const int infoWidth = std::max(360, mHorizontalCompass->getWidth());
            constexpr int infoHeight = 28;
            constexpr int infoGap = 1;
            const int infoLeft = (viewSize.width - infoWidth) / 2;
            const int infoTop = mHorizontalCompass->getTop() + mHorizontalCompass->getHeight() + infoGap;
            mGameTimeBox->setCoord(infoLeft, infoTop, infoWidth, infoHeight);
            mGameTimeBox->setFontHeight(18);
            mGameTimeBox->setTextAlign(MyGUI::Align::Center);
        }
        if (mCellNameClip)
            mCellNameClip->setVisible(false);
        if (mCellNameBox)
            mCellNameBox->setVisible(false);

        // effect box can have variable width -> variable left coordinate
        int effectsDx = 0;
        if (!mMinimapBox->getVisible ())
            effectsDx = mEffectBoxBaseRight - mMinimapBoxBaseRight;

        mMapVisible = mMinimapBox->getVisible ();

        mEffectBox->setPosition((viewSize.width - mEffectBoxBaseRight) - mEffectBox->getWidth() + effectsDx, mEffectBox->getTop());
    }

    void HUD::setFocusObject(const MWWorld::Ptr& focus)
    {
        const bool validActor = !focus.isEmpty() && focus.getClass().isActor()
            && focus != MWMechanics::getPlayer()
            && !focus.getClass().getCreatureStats(focus).isDead();

        if (validActor)
        {
            if (mFocusActor.isEmpty() || mFocusActor != focus)
            {
                mFocusActor = focus;
                mFocusActorPanelAlpha = 0.f;
                mTargetPanelPositionInitialized = false;
            }
            mFocusActorCurrentlyFaced = true;
            mFocusActorDistance = MWBase::Environment::get().getWorld()->getDistanceToFacedObject();
        }
        else
        {
            // Keep the previous actor while the panel fades out. Clearing it immediately
            // caused name, level and health to blink whenever the activation ray briefly
            // left an animated actor for one frame.
            mFocusActorCurrentlyFaced = false;
            mFocusActorDistance = -1.f;
        }
    }

    void HUD::setFocusObjectScreenCoords(float min_x, float min_y, float max_x, float max_y)
    {
        mFocusActorScreenX = (min_x + max_x) * 0.5f;
        mFocusActorScreenY = min_y;
    }

    void HUD::updateFocusedTargetPanel(float dt)
    {
        dt = std::max(0.f, dt);

        bool actorAlive = !mFocusActor.isEmpty();
        if (actorAlive)
        {
            actorAlive = mFocusActor.isInCell()
                && mFocusActor.getRefData().getCount() > 0
                && mFocusActor.getRefData().isEnabled()
                && !mFocusActor.getRefData().isDeleted()
                && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead();
        }

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool dialogueOpen = MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        const bool hoverEnabled = npcBarShowsHover(getNpcBarMode());
        bool panelShouldBeVisible = false;

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        if (actorAlive && mFocusActorCurrentlyFaced && hoverEnabled && !dialogueOpen && world)
        {
            // The target panel is intentionally binary: fully visible or fully hidden.
            // Do not leave semi-transparent text or health bars over the actor's face.
            const float horizontalOffset = std::abs(mFocusActorScreenX - 0.5f);
            constexpr float centreVisibleLimit = 0.46f;
            bool withinVisibleDistance = true;
            bool hidePeacefulActorAtPointBlank = false;

            if (mFocusActorDistance >= 0.f)
            {
                const float maximumDistance = std::max(1.f, world->getMaxActivationDistance());
                withinVisibleDistance = mFocusActorDistance <= maximumDistance * 1.05f;

                const MWWorld::Ptr player = MWMechanics::getPlayer();
                const MWMechanics::CreatureStats& targetStats
                    = mFocusActor.getClass().getCreatureStats(mFocusActor);
                const bool aggressiveTarget = targetStats.getAiSequence().isInCombat(player)
                    || MWBase::Environment::get().getMechanicsManager()->isAggressive(mFocusActor, player);

                // Only first-person needs the close-range face protection. In third-person,
                // preview and vanity modes the panel remains visible even at point-blank range.
                if (world->isFirstPerson() && !aggressiveTarget)
                {
                    // Hide only when the peaceful actor is directly in the player's face.
                    // Hysteresis prevents rapid toggling when standing on the boundary.
                    const float closeHideDistance = std::min(52.f, maximumDistance * 0.27f);
                    const float closeShowDistance = std::max(closeHideDistance + 14.f,
                        std::min(68.f, maximumDistance * 0.35f));
                    hidePeacefulActorAtPointBlank = mFocusActorPanelAlpha > 0.5f
                        ? mFocusActorDistance <= closeHideDistance
                        : mFocusActorDistance < closeShowDistance;
                }
            }

            panelShouldBeVisible = horizontalOffset <= centreVisibleLimit
                && withinVisibleDistance
                && !hidePeacefulActorAtPointBlank;

            // Keep the panel stable under the compass. It follows the actor slightly
            // from side to side, but ignores vertical animation of the bounding box.
            const float screenCentre = viewSize.width * 0.5f;
            const float maximumTravel = viewSize.width * 0.12f;
            const float actorOffset = (mFocusActorScreenX - 0.5f) * viewSize.width * 0.35f;
            const float desiredCentre = screenCentre
                + std::max(-maximumTravel, std::min(maximumTravel, actorOffset));

            if (!mTargetPanelPositionInitialized)
            {
                mTargetPanelCenterX = desiredCentre;
                mTargetPanelPositionInitialized = true;
            }
            else
            {
                const float positionBlend = 1.f - std::exp(-7.f * dt);
                mTargetPanelCenterX += (desiredCentre - mTargetPanelCenterX) * positionBlend;
            }
        }

        // No intermediate opacity. Appearance is now immediate (more than three times
        // faster than the previous fade), and close-range hiding is immediate as well.
        mFocusActorPanelAlpha = panelShouldBeVisible ? 1.f : 0.f;

        if ((!actorAlive || !mFocusActorCurrentlyFaced) && mFocusActorPanelAlpha < 0.01f)
        {
            mFocusActor = MWWorld::Ptr();
            mFocusActorPanelAlpha = 0.f;
            mTargetPanelPositionInitialized = false;
        }
    }

    void HUD::updateEnemyHealthBar()
    {
        const std::string npcBarMode = getNpcBarMode();
        const bool usingFocusActor = npcBarShowsHover(npcBarMode)
            && !mFocusActor.isEmpty()
            && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead()
            && !MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue)
            && mFocusActorPanelAlpha > 0.01f;

        MWWorld::Ptr enemy;
        if (usingFocusActor)
            enemy = mFocusActor;
        else if (npcBarShowsCombat(npcBarMode) && mEnemyActorId != -1)
            enemy = MWBase::Environment::get().getWorld()->searchPtrViaActorId(mEnemyActorId);

        if (enemy.isEmpty())
        {
            mEnemyHealth->setVisible(false);
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);
            return;
        }

        MWMechanics::CreatureStats& stats = enemy.getClass().getCreatureStats(enemy);
        if (stats.isDead() || stats.getHealth().getCurrent() <= 0.f)
        {
            mEnemyHealth->setVisible(false);
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);
            return;
        }

        const float maximumHealth = stats.getHealth().getModified();
        const float currentHealth = stats.getHealth().getCurrent();
        const int maximumHealthPoints = std::max(1, static_cast<int>(std::lround(maximumHealth)));
        const int currentHealthPoints = std::max(0, std::min(maximumHealthPoints,
            static_cast<int>(std::lround(currentHealth))));

        mEnemyHealth->setProgressRange(static_cast<size_t>(maximumHealthPoints));
        mEnemyHealth->setProgressPosition(static_cast<size_t>(currentHealthPoints));

        static const float fNPCHealthBarFade = MWBase::Environment::get().getWorld()->getStore()
            .get<ESM::GameSetting>().find("fNPCHealthBarFade")->mValue.getFloat();
        const float alpha = usingFocusActor ? mFocusActorPanelAlpha
            : (fNPCHealthBarFade > 0.f
                ? std::max(0.f, std::min(1.f, mEnemyHealthTimer / fNPCHealthBarFade))
                : 1.f);
        mEnemyHealth->setAlpha(alpha);

        if (usingFocusActor)
        {
            // Faced actor: stable name, level and health panel below the compass.
            mEnemyHealth->setSize(190, 16);
            if (mEnemyName)
            {
                mEnemyName->setSize(240, 20);
                mEnemyName->setCaption(enemy.getClass().getName(enemy) + "  -  "
                    + MyGUI::utility::toString(stats.getLevel()) + " lvl");
                mEnemyName->setAlpha(alpha);
            }
            if (mEnemySummary)
            {
                mEnemySummary->setSize(190, 16);
                mEnemySummary->setCaption(MyGUI::utility::toString(currentHealthPoints) + " / "
                    + MyGUI::utility::toString(maximumHealthPoints));
                mEnemySummary->setAlpha(alpha);
            }

            const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            const int nameWidth = mEnemyName ? mEnemyName->getWidth() : 0;
            const int nameHeight = mEnemyName ? mEnemyName->getHeight() : 0;
            const int barWidth = mEnemyHealth->getWidth();
            const int totalWidth = std::max(nameWidth, barWidth);
            const int totalHeight = nameHeight + 2 + mEnemyHealth->getHeight();

            constexpr int targetPanelSafeMargin = 14;
            const int horizontalMargin = std::min(targetPanelSafeMargin,
                std::max(0, (viewSize.width - totalWidth) / 2));
            const int verticalMargin = std::min(targetPanelSafeMargin,
                std::max(0, (viewSize.height - totalHeight) / 2));
            const int maximumLeft = std::max(horizontalMargin,
                viewSize.width - horizontalMargin - totalWidth);
            const int maximumTop = std::max(verticalMargin,
                viewSize.height - verticalMargin - totalHeight);

            const int stableCentreX = mTargetPanelPositionInitialized
                ? static_cast<int>(std::lround(mTargetPanelCenterX))
                : viewSize.width / 2;
            const int panelLeft = std::max(horizontalMargin,
                std::min(stableCentreX - totalWidth / 2, maximumLeft));

            // Keep the target panel below the compass and its shared clock/location line.
            // Reserving the line even during a fade prevents the target panel from jumping.
            int baseY = verticalMargin;
            if (mHorizontalCompass && mHorizontalCompass->getVisible())
            {
                const MyGUI::IntCoord compassCoord = mHorizontalCompass->getAbsoluteCoord();
                constexpr int targetPanelCompassGap = 7;
                baseY = std::max(baseY,
                    compassCoord.top + compassCoord.height + targetPanelCompassGap);
            }
            if (mGameTimeBox)
            {
                const MyGUI::IntCoord infoCoord = mGameTimeBox->getAbsoluteCoord();
                constexpr int targetPanelInfoGap = 5;
                baseY = std::max(baseY, infoCoord.top + infoCoord.height + targetPanelInfoGap);
            }
            baseY = std::min(baseY, maximumTop);

            if (mEnemyName)
                mEnemyName->setPosition(panelLeft + (totalWidth - nameWidth) / 2, baseY);
            const int barLeft = panelLeft + (totalWidth - barWidth) / 2;
            mEnemyHealth->setPosition(barLeft, baseY + nameHeight + 2);
            if (mEnemySummary)
                mEnemySummary->setPosition(barLeft, baseY + nameHeight + 2);
        }
        else
        {
            // Combat feedback when the compact target panel is disabled: a thin red bar above player health.
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);

            const MyGUI::IntCoord playerHealth = mHealth->getAbsoluteCoord();
            mEnemyHealth->setCoord(playerHealth.left, std::max(0, playerHealth.top - 9), playerHealth.width, 7);
        }
    }

    void HUD::setEnemy(const MWWorld::Ptr &enemy)
    {
        mEnemyActorId = enemy.getClass().getCreatureStats(enemy).getActorId();
        if (mEnemyName)
            mEnemyName->setVisible(false);
        if (mEnemySummary)
            mEnemySummary->setVisible(false);
        mEnemyHealthTimer = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fNPCHealthBarTime")->mValue.getFloat();
        const bool showCombatBar = npcBarShowsCombat(getNpcBarMode());
        mEnemyHealth->setVisible(showCombatBar);
        if (showCombatBar)
            updateEnemyHealthBar();
    }

    void HUD::resetEnemy()
    {
        mEnemyActorId = -1;
        mEnemyHealthTimer = -1;
        if (mEnemyName) mEnemyName->setVisible(false);
        if (mEnemySummary) mEnemySummary->setVisible(false);
    }

    void HUD::clear()
    {
        unsetSelectedSpell();
        unsetSelectedWeapon();
        resetEnemy();
        mFocusActor = MWWorld::Ptr();
        mFocusActorCurrentlyFaced = false;
        mFocusActorDistance = -1.f;
        mFocusActorPanelAlpha = 0.f;
        mTargetPanelPositionInitialized = false;
        if (mEnemyName)
            mEnemyName->setVisible(false);
        if (mEnemySummary)
            mEnemySummary->setVisible(false);
    }

    void HUD::customMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

    void HUD::doorMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

}
