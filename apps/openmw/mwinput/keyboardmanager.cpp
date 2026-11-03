#include "keyboardmanager.hpp"

#include <cctype>

#include <MyGUI_InputManager.h>

#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/player.hpp"

#include "actions.hpp"
#include "bindingsmanager.hpp"
#include "sdlmappings.hpp"

namespace MWInput
{
    namespace
    {
        bool togglePostProcessSetting(const char* category, const char* setting,
            const char* enabledMessage, const char* disabledMessage)
        {
            MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
            if (windowManager->isGuiMode())
                return false;

            const bool enabled = !Settings::Manager::getBool(setting, category);
            Settings::Manager::setBool(setting, category, enabled);
            const Settings::CategorySettingVector changed = Settings::Manager::getPendingChanges();
            MWBase::Environment::get().getWorld()->processChangedSettings(changed);
            Settings::Manager::resetPendingChanges();
            Settings::Manager::saveUser();
            windowManager->messageBox(enabled ? enabledMessage : disabledMessage);
            return true;
        }
    }

    KeyboardManager::KeyboardManager(BindingsManager* bindingsManager)
        : mBindingsManager(bindingsManager)
    {
    }

    void KeyboardManager::textInput(const SDL_TextInputEvent &arg)
    {
        MyGUI::UString ustring(&arg.text[0]);
        MyGUI::UString::utf32string utf32string = ustring.asUTF32();
        for (MyGUI::UString::utf32string::const_iterator it = utf32string.begin(); it != utf32string.end(); ++it)
            MyGUI::InputManager::getInstance().injectKeyPress(MyGUI::KeyCode::None, *it);
    }

    void KeyboardManager::keyPressed(const SDL_KeyboardEvent &arg)
    {
        // HACK: to make default keybinding for the console work without printing an extra "^" upon closing
        // This assumes that SDL_TextInput events always come *after* the key event
        // (which is somewhat reasonable, and hopefully true for all SDL platforms)
        auto kc = sdlKeyToMyGUI(arg.keysym.sym);
        if (mBindingsManager->getKeyBinding(A_Console) == arg.keysym.scancode
                && MWBase::Environment::get().getWindowManager()->isConsoleMode())
            SDL_StopTextInput();

        bool consumed = false;
        if (!arg.repeat && !mBindingsManager->isDetectingBindingState())
        {
            if (arg.keysym.scancode == SDL_SCANCODE_F3)
                consumed = togglePostProcessSetting("Post Processing", "enabled",
                    "#{arenamp=hotkey.postprocessing_on}", "#{arenamp=hotkey.postprocessing_off}");

            // Placement mode owns these literal keys while a prop is grabbed.
            // Consume them before the binding system so the keys cannot also
            // trigger jump, sneak, journal, ready weapon/spell, presets, etc.
            MWBase::World* world = MWBase::Environment::get().getWorld();
            const bool placementActive = world && world->isPhysicsGrabActive()
                && !MWBase::Environment::get().getWindowManager()->isGuiMode();
            if (!consumed && placementActive)
            {
                switch (arg.keysym.scancode)
                {
                    case SDL_SCANCODE_SPACE:
                        world->togglePhysicsGrabPhysics();
                        consumed = true;
                        break;
                    case SDL_SCANCODE_TAB:
                        world->cyclePhysicsGrabMoveMode();
                        consumed = true;
                        break;
                    case SDL_SCANCODE_LCTRL:
                    case SDL_SCANCODE_RCTRL:
                        world->resetPhysicsGrabTransform();
                        consumed = true;
                        break;
                    case SDL_SCANCODE_R:
                    case SDL_SCANCODE_F:
                        // Continuous rotation is sampled by ActionManager::update().
                        consumed = true;
                        break;
                    default:
                        break;
                }
            }
        }

        // SDL text input normally suppresses the matching printable key event and
        // sends only SDL_TEXTINPUT. That is correct for ordinary typing, but it
        // also swallowed Ctrl+C/V/X/Z before MyGUI::EditBox could execute its
        // clipboard/undo commands. Let the standard editing shortcuts through as
        // key events while keeping normal character entry on SDL_TEXTINPUT.
        const bool editingModifier = (arg.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
        const bool textEditingShortcut = SDL_IsTextInputActive() && editingModifier
            && (arg.keysym.scancode == SDL_SCANCODE_C
                || arg.keysym.scancode == SDL_SCANCODE_V
                || arg.keysym.scancode == SDL_SCANCODE_X
                || arg.keysym.scancode == SDL_SCANCODE_Z
                || arg.keysym.scancode == SDL_SCANCODE_Y
                || arg.keysym.scancode == SDL_SCANCODE_A);

        consumed = consumed || (SDL_IsTextInputActive() && !textEditingShortcut
                        && (!(SDLK_SCANCODE_MASK & arg.keysym.sym)
                        && (std::isprint(arg.keysym.sym)
                        // Don't trust isprint for symbols outside the extended ASCII range
                        || (kc == MyGUI::KeyCode::None && arg.keysym.sym > 0xff))));
        if (!consumed && kc != MyGUI::KeyCode::None && !mBindingsManager->isDetectingBindingState())
        {
            MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();

            // In GUI mode WASD mirrors the arrow keys. Text-entry widgets keep
            // literal WASD so search fields, books and console editing are unaffected.
            if (windowManager->isGuiMode() && !SDL_IsTextInputActive())
            {
                switch (arg.keysym.scancode)
                {
                    case SDL_SCANCODE_W: kc = MyGUI::KeyCode::ArrowUp; break;
                    case SDL_SCANCODE_S: kc = MyGUI::KeyCode::ArrowDown; break;
                    case SDL_SCANCODE_A: kc = MyGUI::KeyCode::ArrowLeft; break;
                    case SDL_SCANCODE_D: kc = MyGUI::KeyCode::ArrowRight; break;
                    default: break;
                }
            }

            // QuickLoot remains non-modal, but axis-placement mode owns the
            // movement bindings. Do not let UI navigation consume a key that is
            // currently supposed to translate the held object.
            MWBase::World* world = MWBase::Environment::get().getWorld();
            const bool placementMoveMode = world && world->isPhysicsGrabActive()
                && world->getPhysicsGrabMoveMode() != 0;
            const bool placementMovementKey = placementMoveMode
                && (arg.keysym.scancode == mBindingsManager->getKeyBinding(A_MoveLeft)
                    || arg.keysym.scancode == mBindingsManager->getKeyBinding(A_MoveRight)
                    || arg.keysym.scancode == mBindingsManager->getKeyBinding(A_MoveForward)
                    || arg.keysym.scancode == mBindingsManager->getKeyBinding(A_MoveBackward));

            if (!placementMovementKey && !windowManager->isGuiMode() && windowManager->handleQuickLootKeyPress(kc))
                consumed = true;
            else if (!placementMovementKey && windowManager->injectKeyPress(kc, 0, arg.repeat))
                consumed = true;

            mBindingsManager->setPlayerControlsEnabled(!consumed);
        }

        if (arg.repeat)
            return;

        MWBase::InputManager* input = MWBase::Environment::get().getInputManager();
        if (!input->controlsDisabled() && !consumed)
            mBindingsManager->keyPressed(arg);

        input->setJoystickLastUsed(false);
    }

    void KeyboardManager::keyReleased(const SDL_KeyboardEvent &arg)
    {
        MWBase::Environment::get().getInputManager()->setJoystickLastUsed(false);
        auto kc = sdlKeyToMyGUI(arg.keysym.sym);

        if (!mBindingsManager->isDetectingBindingState())
            mBindingsManager->setPlayerControlsEnabled(!MyGUI::InputManager::getInstance().injectKeyRelease(kc));

        // Always forward releases to clear a binding that may have been held
        // before placement mode consumed its key press (for example Ctrl/Sneak).
        mBindingsManager->keyReleased(arg);
    }
}
