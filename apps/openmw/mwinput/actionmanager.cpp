#include "actionmanager.hpp"

#include <MyGUI_InputManager.h>

#include <SDL_keyboard.h>

#include <components/settings/settings.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/GUIController.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/inputmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/player.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "actions.hpp"
#include "bindingsmanager.hpp"

namespace MWInput
{
    const float ZOOM_SCALE = 10.f; /// Used for scrolling camera in and out

    ActionManager::ActionManager(BindingsManager* bindingsManager,
            osgViewer::ScreenCaptureHandler::CaptureOperation* screenCaptureOperation,
            osg::ref_ptr<osgViewer::Viewer> viewer,
            osg::ref_ptr<osgViewer::ScreenCaptureHandler> screenCaptureHandler)
        : mBindingsManager(bindingsManager)
        , mViewer(viewer)
        , mScreenCaptureHandler(screenCaptureHandler)
        , mScreenCaptureOperation(screenCaptureOperation)
        , mAlwaysRunActive(Settings::Manager::getBool("always run", "Input"))
        , mSneaking(false)
        , mAttemptJump(false)
        , mOverencumberedMessageDelay(0.f)
        , mPreviewPOVDelay(0.f)
        , mTimeIdle(0.f)
        , mActivateHoldTime(0.f)
        , mActivateHoldPending(false)
    {
    }

    void ActionManager::update(float dt, bool triedToMove)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool guiMode = MWBase::Environment::get().getWindowManager()->isGuiMode();
        const bool gameRunning = MWBase::Environment::get().getStateManager()->getState()
            == MWBase::StateManager::State_Running;

        // ArenaMP placement mode: normal activation is deferred only for a
        // loose, ownerless world item. A short press stays vanilla Activate; a
        // deliberate ~1 second hold enters placement mode. Once grabbed, the
        // Activate key may be released without dropping the object.
        if (mActivateHoldPending)
        {
            const bool stillHeld = mBindingsManager->actionIsActive(A_Activate);
            if (!gameRunning || guiMode)
            {
                if (world->isPhysicsGrabActive())
                    world->finishPhysicsGrab();
                mActivateHoldObject = MWWorld::Ptr();
                mActivateHoldTime = 0.f;
                mActivateHoldPending = false;
            }
            else if (stillHeld)
            {
                mActivateHoldTime += dt;
                if (mActivateHoldTime >= 1.0f && !world->isPhysicsGrabActive())
                {
                    // The original reference must still be under the crosshair.
                    // Besides feeling predictable, this is important because
                    // beginPhysicsGrab uses the current faced-object distance as
                    // its hold distance; grabbing a stale reference would otherwise
                    // use the distance of a different target.
                    const MWWorld::Ptr facedNow = world->getFacedObject();
                    if (!mActivateHoldObject.isEmpty() && facedNow == mActivateHoldObject
                        && world->beginPhysicsGrab(mActivateHoldObject))
                    {
                        // The hold gesture has completed. Do not wait for the
                        // key-up event: releasing Activate must keep the grab alive.
                        mActivateHoldObject = MWWorld::Ptr();
                        mActivateHoldTime = 0.f;
                        mActivateHoldPending = false;
                    }
                    else
                    {
                        mActivateHoldObject = MWWorld::Ptr();
                        mActivateHoldTime = 0.f;
                        mActivateHoldPending = false;
                    }
                }
            }
            else
            {
                // Released before the hold threshold: this was a normal tap.
                activate();
                mActivateHoldObject = MWWorld::Ptr();
                mActivateHoldTime = 0.f;
                mActivateHoldPending = false;
            }
        }

        // Placement is a gameplay-only state. Entering a GUI/pause transition
        // finalizes the current object according to its Physics ON/OFF setting
        // and always removes the persistent hint.
        if (guiMode || !gameRunning)
        {
            if (world->isPhysicsGrabActive())
                world->finishPhysicsGrab();
            MWBase::Environment::get().getWindowManager()->setPhysicsGrabHint(false, 0, false);
            mAttemptJump = false;
            return;
        }

        const bool physicsGrabActive = world->isPhysicsGrabActive();
        const int physicsGrabMoveMode = physicsGrabActive ? world->getPhysicsGrabMoveMode() : 0;

        if (physicsGrabActive)
        {
            // Placement controls are literal R/F as requested and are consumed by
            // KeyboardManager, so they never also ready a spell/weapon even when
            // the user has rebound those gameplay actions. SDL's keyboard state is
            // still available here for smooth held-key rotation.
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            const float rollInput = keys && keys[SDL_SCANCODE_F] ? 1.f : 0.f;
            const float pitchInput = keys && keys[SDL_SCANCODE_R] ? 1.f : 0.f;
            world->rotatePhysicsGrab(rollInput, pitchInput, dt);

            if (physicsGrabMoveMode != 0)
            {
                const float firstAxis =
                    (mBindingsManager->actionIsActive(A_MoveRight) ? 1.f : 0.f)
                    - (mBindingsManager->actionIsActive(A_MoveLeft) ? 1.f : 0.f);
                const float secondAxis =
                    (mBindingsManager->actionIsActive(A_MoveForward) ? 1.f : 0.f)
                    - (mBindingsManager->actionIsActive(A_MoveBackward) ? 1.f : 0.f);
                world->translatePhysicsGrab(firstAxis, secondAxis, dt);
            }
        }

        // A borderless dark help panel remains visible for the entire grab.
        MWBase::Environment::get().getWindowManager()->setPhysicsGrabHint(
            physicsGrabActive, physicsGrabMoveMode,
            physicsGrabActive && world->isPhysicsGrabPhysicsEnabled());

        // QuickLoot is shown only while the player is looking at a non-empty container.
        // Keep W/S state tracked by the binding system, but suppress forward/backward
        // movement for exactly that visible interval. This also handles the case where
        // W or S was already held before the overlay appeared and resumes movement as
        // soon as the player looks away and the overlay closes.
        const bool blockForwardBackward =
            MWBase::Environment::get().getWindowManager()->isQuickLootVisible();

        // Configure player movement according to keyboard input. Actual movement will
        // be done in the physics system.
        if (MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
        {
            bool alwaysRunAllowed = false;

            MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();

            if (physicsGrabMoveMode != 0)
            {
                // In an axis-placement mode the normal walking channels are
                // repurposed exclusively for the held object.
                player.setAutoMove(false);
                player.setLeftRight(0);
                player.setForwardBackward(0);
                player.setUpDown(0);
            }
            else if (mBindingsManager->actionIsActive(A_MoveLeft) != mBindingsManager->actionIsActive(A_MoveRight))
            {
                alwaysRunAllowed = true;
                triedToMove = true;
                player.setLeftRight(mBindingsManager->actionIsActive(A_MoveRight) ? 1 : -1);
            }

            if (physicsGrabMoveMode != 0)
            {
                player.setForwardBackward(0);
            }
            else if (blockForwardBackward)
            {
                // Do not let W/S move the player while QuickLoot is visible.
                player.setForwardBackward(0);
            }
            else if (mBindingsManager->actionIsActive(A_MoveForward) != mBindingsManager->actionIsActive(A_MoveBackward))
            {
                alwaysRunAllowed = true;
                triedToMove = true;
                player.setAutoMove (false);
                player.setForwardBackward(mBindingsManager->actionIsActive(A_MoveForward) ? 1 : -1);
            }

            if (physicsGrabMoveMode == 0 && player.getAutoMove())
            {
                alwaysRunAllowed = true;
                triedToMove = true;
                player.setForwardBackward (1);
            }

            if (mAttemptJump && MWBase::Environment::get().getInputManager()->getControlSwitch("playerjumping"))
            {
                // Space is reserved for Physics ON/OFF during any placement-mode
                // submode, including Walk. KeyboardManager consumes the key; this
                // guard also prevents controller/alternate Jump bindings from
                // invoking the old placement behaviour.
                if (!physicsGrabActive)
                {
                    player.setUpDown(1);
                    triedToMove = true;
                    mOverencumberedMessageDelay = 0.f;
                }
                mAttemptJump = false;
            }

            // if player tried to start moving, but can't (due to being overencumbered), display a notification.
            if (triedToMove)
            {
                MWWorld::Ptr playerPtr = MWBase::Environment::get().getWorld ()->getPlayerPtr();
                mOverencumberedMessageDelay -= dt;
                if (playerPtr.getClass().getEncumbrance(playerPtr) > playerPtr.getClass().getCapacity(playerPtr))
                {
                    player.setAutoMove (false);
                    if (mOverencumberedMessageDelay <= 0)
                    {
                        MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage59}");
                        mOverencumberedMessageDelay = 1.0;
                    }
                }
            }

            if (MWBase::Environment::get().getInputManager()->getControlSwitch("playerviewswitch"))
            {
                const float switchLimit = 0.25;
                MWBase::World* world = MWBase::Environment::get().getWorld();
                if (mBindingsManager->actionIsActive(A_TogglePOV))
                {
                    if (world->isFirstPerson() ? mPreviewPOVDelay > switchLimit : mPreviewPOVDelay == 0)
                        world->togglePreviewMode(true);
                    mPreviewPOVDelay += dt;
                }
                else
                {
                    //disable preview mode
                    if (mPreviewPOVDelay > 0)
                        world->togglePreviewMode(false);
                    if (mPreviewPOVDelay > 0.f && mPreviewPOVDelay <= switchLimit)
                        world->togglePOV();
                    mPreviewPOVDelay = 0.f;
                }
            }

            if (triedToMove)
                MWBase::Environment::get().getInputManager()->resetIdleTime();

            static const bool isToggleSneak = Settings::Manager::getBool("toggle sneak", "Input");
            if (!isToggleSneak)
            {
                if(!MWBase::Environment::get().getInputManager()->joystickLastUsed())
                    player.setSneak(mBindingsManager->actionIsActive(A_Sneak));
            }

            float xAxis = mBindingsManager->getActionValue(A_MoveLeftRight);
            float yAxis = mBindingsManager->getActionValue(A_MoveForwardBackward);
            bool isRunning = osg::Vec2f(xAxis * 2 - 1, yAxis * 2 - 1).length2() > 0.25f;
            if ((mAlwaysRunActive && alwaysRunAllowed) || isRunning)
                player.setRunState(!mBindingsManager->actionIsActive(A_Run));
            else
                player.setRunState(mBindingsManager->actionIsActive(A_Run));
        }

        if ((!blockForwardBackward && (mBindingsManager->actionIsActive(A_MoveForward) ||
                mBindingsManager->actionIsActive(A_MoveBackward))) ||
            mBindingsManager->actionIsActive(A_MoveLeft) ||
            mBindingsManager->actionIsActive(A_MoveRight) ||
            mBindingsManager->actionIsActive(A_Jump) ||
            mBindingsManager->actionIsActive(A_Sneak) ||
            mBindingsManager->actionIsActive(A_TogglePOV) ||
            mBindingsManager->actionIsActive(A_ZoomIn) ||
            mBindingsManager->actionIsActive(A_ZoomOut))
        {
            resetIdleTime();
        }
        else
        {
            updateIdleTime(dt);
        }

        mAttemptJump = false;
    }
    
    bool ActionManager::isPreviewModeEnabled()
    {
        return MWBase::Environment::get().getWorld()->isPreviewModeEnabled();
    }

    void ActionManager::resetIdleTime()
    {
        if (mTimeIdle < 0)
            MWBase::Environment::get().getWorld()->toggleVanityMode(false);
        mTimeIdle = 0.f;
    }

    void ActionManager::updateIdleTime(float dt)
    {
        static const float vanityDelay = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>()
                .find("fVanityDelay")->mValue.getFloat();
        if (mTimeIdle >= 0.f)
            mTimeIdle += dt;
        if (mTimeIdle > vanityDelay)
        {
            MWBase::Environment::get().getWorld()->toggleVanityMode(true);
            mTimeIdle = -1.f;
        }
    }

    void ActionManager::executeAction(int action)
    {
        auto* inputManager = MWBase::Environment::get().getInputManager();
        auto* windowManager = MWBase::Environment::get().getWindowManager();
        // trigger action activated
        switch (action)
        {
        case A_GameMenu:
            toggleMainMenu ();
            break;
        case A_Screenshot:
            screenshot();
            break;
        case A_Inventory:
            toggleInventory ();
            break;
        case A_Console:
            toggleConsole ();
            break;
        case A_Activate:
            inputManager->resetIdleTime();
            if (!windowManager->isGuiMode() && inputManager->getControlSwitch("playercontrols"))
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();

                // A second press of the same Take/Activate action ends placement.
                // Physics ON releases a live prop; Physics OFF freezes the exact
                // arranged transform. It never also activates the object underneath.
                if (world->isPhysicsGrabActive())
                {
                    world->finishPhysicsGrab();
                    mActivateHoldObject = MWWorld::Ptr();
                    mActivateHoldTime = 0.f;
                    mActivateHoldPending = false;
                    windowManager->setPhysicsGrabHint(false, 0, false);
                    break;
                }

                MWWorld::Ptr faced = world->getFacedObject();
                if (!faced.isEmpty() && world->canPhysicsGrab(faced))
                {
                    mActivateHoldObject = faced;
                    mActivateHoldTime = 0.f;
                    mActivateHoldPending = true;
                    break;
                }
            }
            activate();
            break;
        case A_MoveLeft:
        case A_MoveRight:
        case A_MoveForward:
        case A_MoveBackward:
            // In X-Y/X-Z/Z-Y placement modes these action channels belong to
            // the object exclusively. Do not also synthesize GUI arrow presses
            // (QuickLoot/list navigation, etc.). Walk mode keeps vanilla input.
            if (MWBase::Environment::get().getWorld()->isPhysicsGrabActive()
                && MWBase::Environment::get().getWorld()->getPhysicsGrabMoveMode() != 0)
                break;
            handleGuiArrowKey(action);
            break;
        case A_Journal:
            toggleJournal();
            break;
        case A_AutoMove:
            toggleAutoMove();
            break;
        case A_AlwaysRun:
            toggleWalking();
            break;
        case A_ToggleWeapon:
            // When an object is physically held, this action is temporarily a
            // screen-plane rotate modifier.  Do not also draw/sheath the weapon.
            if (!MWBase::Environment::get().getWorld()->isPhysicsGrabActive())
                toggleWeapon();
            break;
        case A_Rest:
            rest();
            break;
        case A_ToggleSpell:
            // Same rule for the second rotation plane.  Outside physics-grab mode
            // the original ready/unready magic action remains unchanged.
            if (!MWBase::Environment::get().getWorld()->isPhysicsGrabActive())
                toggleSpell();
            break;
        case A_QuickKey1:
            quickKey(1);
            break;
        case A_QuickKey2:
            quickKey(2);
            break;
        case A_QuickKey3:
            quickKey(3);
            break;
        case A_QuickKey4:
            quickKey(4);
            break;
        case A_QuickKey5:
            quickKey(5);
            break;
        case A_QuickKey6:
            quickKey(6);
            break;
        case A_QuickKey7:
            quickKey(7);
            break;
        case A_QuickKey8:
            quickKey(8);
            break;
        case A_QuickKey9:
            quickKey(9);
            break;
        case A_QuickKey10:
            quickKey(10);
            break;
        case A_QuickKeysMenu:
            showQuickKeysMenu();
            break;
        case A_PlayerAnimationMenu:
            showPlayerAnimationMenu();
            break;
        case A_ToggleHUD:
            windowManager->toggleHud();
            break;
        case A_ToggleDebug:
            windowManager->toggleDebugWindow();
            break;
        case A_ZoomIn:
            if (inputManager->getControlSwitch("playerviewswitch") && inputManager->getControlSwitch("playercontrols") && !windowManager->isGuiMode())
                MWBase::Environment::get().getWorld()->adjustCameraDistance(-ZOOM_SCALE);
            break;
        case A_ZoomOut:
            if (inputManager->getControlSwitch("playerviewswitch") && inputManager->getControlSwitch("playercontrols") && !windowManager->isGuiMode())
                MWBase::Environment::get().getWorld()->adjustCameraDistance(ZOOM_SCALE);
            break;
        case A_QuickSave:
            quickSave();
            break;
        case A_QuickLoad:
            quickLoad();
            break;
        case A_CycleSpellLeft:
            if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Magic))
                MWBase::Environment::get().getWindowManager()->cycleSpell(false);
            break;
        case A_CycleSpellRight:
            if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Magic))
                MWBase::Environment::get().getWindowManager()->cycleSpell(true);
            break;
        case A_CycleWeaponLeft:
            if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Inventory))
                MWBase::Environment::get().getWindowManager()->cycleWeapon(false);
            break;
        case A_CycleWeaponRight:
            if (checkAllowedToUseItems() && windowManager->isAllowed(MWGui::GW_Inventory))
                MWBase::Environment::get().getWindowManager()->cycleWeapon(true);
            break;
        case A_Sneak:
            static const bool isToggleSneak = Settings::Manager::getBool("toggle sneak", "Input");
            if (isToggleSneak)
            {
                toggleSneaking();
            }
            break;
        }
    }

    bool ActionManager::checkAllowedToUseItems() const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            // Cannot use items or spells while in werewolf form
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return false;
        }
        return true;
    }

    void ActionManager::screenshot()
    {
        const std::string& settingStr = Settings::Manager::getString("screenshot type", "Video");
        bool regularScreenshot = settingStr.size() == 0 || settingStr.compare("regular") == 0;

        if (regularScreenshot)
        {
            mScreenCaptureHandler->setFramesToCapture(1);
            mScreenCaptureHandler->captureNextFrame(*mViewer);
        }
        else
        {
            osg::ref_ptr<osg::Image> screenshot (new osg::Image);

            if (MWBase::Environment::get().getWorld()->screenshot360(screenshot.get()))
            {
                (*mScreenCaptureOperation) (*(screenshot.get()), 0);
                // FIXME: mScreenCaptureHandler->getCaptureOperation() causes crash for some reason
            }
        }
    }

    void ActionManager::toggleMainMenu()
    {
        /*
            Start  of tes3mp addition

            Don't allow the main menu to be toggled while TES3MP listboxes are open
        */
        if (MWBase::Environment::get().getWindowManager()->getMode() == mwmp::GUIController::GM_TES3MP_ListBox)
        {
            return;
        }
        /*
            End of tes3mp addition
        */

        if (MyGUI::InputManager::getInstance().isModalAny())
        {
            MWBase::Environment::get().getWindowManager()->exitCurrentModal();
            return;
        }

        if (MWBase::Environment::get().getWindowManager()->isConsoleMode())
        {
            MWBase::Environment::get().getWindowManager()->toggleConsole();
            return;
        }

        if (!MWBase::Environment::get().getWindowManager()->isGuiMode()) //No open GUIs, open up the MainMenu
        {
            MWBase::Environment::get().getWindowManager()->pushGuiMode (MWGui::GM_MainMenu);
        }
        else //Close current GUI
        {
            MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
        }
    }

    void ActionManager::toggleSpell()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode()) return;

        // Not allowed before the magic window is accessible
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playermagic") || !MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;

        if (!checkAllowedToUseItems())
            return;

        // Not allowed if no spell selected
        MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
        MWWorld::InventoryStore& inventory = player.getPlayer().getClass().getInventoryStore(player.getPlayer());
        if (MWBase::Environment::get().getWindowManager()->getSelectedSpell().empty() &&
            inventory.getSelectedEnchantItem() == inventory.end())
            return;

        if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(player.getPlayer()))
            return;

        MWMechanics::DrawState_ state = player.getDrawState();
        if (state == MWMechanics::DrawState_Weapon || state == MWMechanics::DrawState_Nothing)
            player.setDrawState(MWMechanics::DrawState_Spell);
        else
            player.setDrawState(MWMechanics::DrawState_Nothing);
    }

    void ActionManager::quickLoad()
    {
        if (!MyGUI::InputManager::getInstance().isModalAny())
            MWBase::Environment::get().getStateManager()->quickLoad();
    }

    void ActionManager::quickSave()
    {
        if (!MyGUI::InputManager::getInstance().isModalAny())
            MWBase::Environment::get().getStateManager()->quickSave();
    }

    void ActionManager::toggleWeapon()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode()) return;

        // Not allowed before the inventory window is accessible
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playerfighting") || !MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;

        MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
        // We want to interrupt animation only if attack is preparing, but still is not triggered
        // Otherwise we will get a "speedshooting" exploit, when player can skip reload animation by hitting "Toggle Weapon" key twice
        if (MWBase::Environment::get().getMechanicsManager()->isAttackPreparing(player.getPlayer()))
            player.setAttackingOrSpell(false);
        else if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(player.getPlayer()))
            return;

        MWMechanics::DrawState_ state = player.getDrawState();
        if (state == MWMechanics::DrawState_Spell || state == MWMechanics::DrawState_Nothing)
            player.setDrawState(MWMechanics::DrawState_Weapon);
        else
            player.setDrawState(MWMechanics::DrawState_Nothing);
    }

    void ActionManager::rest()
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;

        if (!MWBase::Environment::get().getWindowManager()->getRestEnabled() || MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;

        /*
            Start of tes3mp addition

            Ignore attempts to rest if the player has not logged in on the server yet

            Set LocalPlayer's isUsingBed to be able to distinguish bed use from regular rest
            menu use
        */
        if (!mwmp::Main::get().getLocalPlayer()->isLoggedIn())
            return;

        mwmp::Main::get().getLocalPlayer()->isUsingBed = false;
        /*
            End of tes3mp addition
        */

        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Rest); //Open rest GUI
    }

    void ActionManager::toggleInventory()
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;

        if (MyGUI::InputManager::getInstance().isModalAny())
            return;

        if (MWBase::Environment::get().getWindowManager()->isConsoleMode())
            return;

        /*
            Start of tes3mp addition

            Ignore attempts to open inventory if the player has not logged in on the server yet
        */
        if (!mwmp::Main::get().getLocalPlayer()->isLoggedIn())
            return;
        /*
            End of tes3mp addition
        */

        // Toggle between game mode and inventory mode
        if(!MWBase::Environment::get().getWindowManager()->isGuiMode())
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Inventory);
        else
        {
            MWGui::GuiMode mode = MWBase::Environment::get().getWindowManager()->getMode();
            if(mode == MWGui::GM_Inventory || mode == MWGui::GM_Container)
                MWBase::Environment::get().getWindowManager()->popGuiMode();
        }

        // .. but don't touch any other mode, except container.
    }

    void ActionManager::toggleConsole()
    {
        if (MyGUI::InputManager::getInstance().isModalAny())
            return;

        /*
            Start of tes3mp addition

            If a player's console is disabled by the server, go no further
        */
        if (!mwmp::Main::get().getLocalPlayer()->consoleAllowed)
            return;
        /*
            End of tes3mp addition
        */

        MWBase::Environment::get().getWindowManager()->toggleConsole();
    }

    void ActionManager::toggleJournal()
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
            return;
        if (MyGUI::InputManager::getInstance ().isModalAny())
            return;

        if (MWBase::Environment::get().getWindowManager()->getMode() != MWGui::GM_Journal
                && MWBase::Environment::get().getWindowManager()->getMode() != MWGui::GM_MainMenu
                && MWBase::Environment::get().getWindowManager()->getMode() != MWGui::GM_Settings
                && MWBase::Environment::get().getWindowManager ()->getJournalAllowed())
        {
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Journal);
        }
        else if (MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Journal))
        {
            MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Journal);
        }
    }

    void ActionManager::quickKey (int index)
    {
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols") || !MWBase::Environment::get().getInputManager()->getControlSwitch("playerfighting") || !MWBase::Environment::get().getInputManager()->getControlSwitch("playermagic"))
            return;
        if (!checkAllowedToUseItems())
            return;

        if (MWBase::Environment::get().getWorld()->getGlobalFloat ("chargenstate")!=-1)
            return;

        if (!MWBase::Environment::get().getWindowManager()->isGuiMode())
            MWBase::Environment::get().getWindowManager()->activateQuickKey (index);
    }

    void ActionManager::showQuickKeysMenu()
    {
        if (!MWBase::Environment::get().getWindowManager()->isGuiMode ()
                && MWBase::Environment::get().getWorld()->getGlobalFloat ("chargenstate")==-1)
        {
            if (!checkAllowedToUseItems())
                return;

            MWBase::Environment::get().getWindowManager()->pushGuiMode (MWGui::GM_QuickKeysMenu);
        }
        else if (MWBase::Environment::get().getWindowManager()->getMode () == MWGui::GM_QuickKeysMenu)
        {
            while (MyGUI::InputManager::getInstance().isModalAny())
            { //Handle any open Modal windows
                MWBase::Environment::get().getWindowManager()->exitCurrentModal();
            }
            MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode(); //And handle the actual main window
        }
    }

    void ActionManager::showPlayerAnimationMenu()
    {
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        if (!windowManager->isGuiMode()
            && MWBase::Environment::get().getWorld()->getGlobalFloat("chargenstate") == -1)
        {
            windowManager->pushGuiMode(MWGui::GM_PlayerAnimationMenu);
        }
        else if (windowManager->getMode() == MWGui::GM_PlayerAnimationMenu)
        {
            windowManager->exitCurrentGuiMode();
        }
    }

    void ActionManager::activate()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode())
        {
            bool joystickUsed = MWBase::Environment::get().getInputManager()->joystickLastUsed();
            if (!SDL_IsTextInputActive() && !mBindingsManager->isLeftOrRightButton(A_Activate, joystickUsed))
                MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::Return, 0, false);
        }
        else if (MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
        {
            // QuickLoot follows the player's actual Activate binding (keyboard, mouse or controller).
            // If it handled the action, do not also activate/open the world object.
            if (MWBase::Environment::get().getWindowManager()->activateQuickLoot())
                return;

            MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
            player.activate();
        }
    }

    void ActionManager::toggleAutoMove()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode()) return;

        if (MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols"))
        {
            MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
            player.setAutoMove (!player.getAutoMove());
        }
    }

    void ActionManager::toggleWalking()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode() || SDL_IsTextInputActive()) return;
        mAlwaysRunActive = !mAlwaysRunActive;

        Settings::Manager::setBool("always run", "Input", mAlwaysRunActive);
    }

    void ActionManager::toggleSneaking()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode()) return;
        if (!MWBase::Environment::get().getInputManager()->getControlSwitch("playercontrols")) return;
        mSneaking = !mSneaking;
        MWWorld::Player& player = MWBase::Environment::get().getWorld()->getPlayer();
        player.setSneak(mSneaking);
    }

    void ActionManager::handleGuiArrowKey(int action)
    {
        bool joystickUsed = MWBase::Environment::get().getInputManager()->joystickLastUsed();
        // This is currently keyboard-specific code
        // TODO: see if GUI controls can be refactored into a single function
        if (joystickUsed)
            return;

        if (SDL_IsTextInputActive())
            return;

        if (mBindingsManager->isLeftOrRightButton(action, joystickUsed))
            return;

        MyGUI::KeyCode key;
        switch (action)
        {
        case A_MoveLeft:
            key = MyGUI::KeyCode::ArrowLeft;
            break;
        case A_MoveRight:
            key = MyGUI::KeyCode::ArrowRight;
            break;
        case A_MoveForward:
            key = MyGUI::KeyCode::ArrowUp;
            break;
        case A_MoveBackward:
        default:
            key = MyGUI::KeyCode::ArrowDown;
            break;
        }

        MWBase::Environment::get().getWindowManager()->injectKeyPress(key, 0, false);
    }
}
