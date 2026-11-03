#include "GUIChat.hpp"

#include <algorithm>
#include <cmath>
#include <MyGUI_EditBox.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ScrollBar.h>
#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwgui/windowmanagerimp.hpp"
#include "apps/openmw/mwinput/inputmanagerimp.hpp"
#include <MyGUI_InputManager.h>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/settings/settings.hpp>

#include "../Networking.hpp"
#include "../Main.hpp"
#include "../LocalPlayer.hpp"

#include "../GUIController.hpp"

namespace
{
    constexpr float sFullyVisibleAlpha = 1.f;
    constexpr float sThirtyPercentTransparentAlpha = 0.7f;
    constexpr float sSixtyPercentTransparentAlpha = 0.4f;
    constexpr float sFadeSpeed = 4.f;

    std::string localizeArena(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    float moveTowards(float value, float target, float maximumDelta)
    {
        if (value < target)
            return std::min(value + maximumDelta, target);
        return std::max(value - maximumDelta, target);
    }
}


namespace mwmp
{
    GUIChat::GUIChat(int x, int y, int w, int h)
            : WindowBase("tes3mp_chat.layout")
            , mHistoryScroll(nullptr)
            , windowState(CHAT_TRANSPARENT_30)
            , editState(false)
            , historyReviewState(false)
            , mainMenuOpen(false)
            , historyDisplayEnabled(true)
            , externalHistoryDisplayEnabled(true)
            , hideAfterFade(false)
            , delay(3.f)
            , revealTime(0.f)
            , currentAlpha(sThirtyPercentTransparentAlpha)
            , targetAlpha(sThirtyPercentTransparentAlpha)
    {
        setCoord(x, y, w, h);

        getWidget(mCommandLine, "edit_Command");
        getWidget(mHistory, "list_History");

        for (size_t i = 0; i < mHistory->getChildCount(); ++i)
        {
            if (MyGUI::ScrollBar* scroll = mHistory->getChildAt(i)->castType<MyGUI::ScrollBar>(false))
            {
                mHistoryScroll = scroll;
                break;
            }
        }
        if (mHistoryScroll)
        {
            mHistoryScroll->setVisible(false);
            mHistoryScroll->setNeedMouseFocus(false);
        }

        // Set up the command line box
        mCommandLine->eventEditSelectAccept +=
                newDelegate(this, &GUIChat::acceptCommand);
        mCommandLine->eventKeyButtonPressed +=
                newDelegate(this, &GUIChat::keyPress);

        setTitle("Chat");

        mHistory->setOverflowToTheLeft(false);
        mHistory->setEditWordWrap(true);
        mHistory->setEditReadOnly(true);
        mHistory->setTextShadow(true);
        mHistory->setTextShadowColour(MyGUI::Colour::Black);

        mHistory->setNeedKeyFocus(false);

        mCommandLine->setVisible(false);
        syncSettings();
        applyAlpha(currentAlpha);
    }

    void GUIChat::onOpen()
    {
        applyAlpha(currentAlpha);
    }

    void GUIChat::onClose()
    {
    }

    bool GUIChat::exit()
    {
        //WindowBase::exit();
        return true;
    }

    bool GUIChat::getEditState()
    {
        return editState;
    }

    void GUIChat::acceptCommand(MyGUI::EditBox *_sender)
    {
        const std::string &cm = mCommandLine->getOnlyText();

        // If they enter nothing, then it should be canceled.
        // Otherwise, there's no way of closing without having text.
        if (cm.empty())
        {
            mCommandLine->setCaption("");
            setEditState(false);
            return;
        }

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Player: %s", cm.c_str());

        // Add the command to the history, and set the current pointer to
        // the end of the list
        if (mCommandHistory.empty() || mCommandHistory.back() != cm)
            mCommandHistory.push_back(cm);
        mCurrent = mCommandHistory.end();
        mEditString.clear();

        // Reset the command line before the command execution.
        // It prevents the re-triggering of the acceptCommand() event for the same command
        // during the actual command execution
        mCommandLine->setCaption("");
        setEditState(false);
        send(cm);
    }

    void GUIChat::onResChange(int width, int height)
    {
        setCoord(10, 40, width-10, height/2); // Original chat layout, shifted 30 px down.
    }

    void GUIChat::setFont(const std::string &fntName)
    {
        mHistory->setFontName(fntName);
        mCommandLine->setFontName(fntName);
    }

    void GUIChat::print(const std::string &msg, const std::string &color)
    {
        if(msg.size() == 0)
        {
            clean();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Chat cleaned");
        }
        else
        {
            mHistory->addText(color + msg);
            if (!historyReviewState)
                scrollHistoryToBottom();
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s", msg.c_str());

            // A received message temporarily restores full opacity. Auto-hide mode also
            // fades the chat in for the configured delay. Fully hidden chat stays hidden.
            if (historyDisplayEnabled && windowState != CHAT_HIDDEN && !mainMenuOpen)
                revealTemporarily();
        }
    }

    void GUIChat::printOK(const std::string &msg)
    {
        print(msg + "\n", "#FF00FF");
    }

    void GUIChat::printError(const std::string &msg)
    {
        print(msg + "\n", "#FF2222");
    }

    void GUIChat::send(const std::string &str)
    {
        LocalPlayer *localPlayer = Main::get().getLocalPlayer();

        Networking *networking = Main::get().getNetworking();

        localPlayer->chatMessage = str;

        networking->getPlayerPacket(ID_CHAT_MESSAGE)->setPlayer(localPlayer);
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->Send();
    }

    void GUIChat::clean()
    {
        mHistory->setCaption("");
        scrollHistoryToBottom();
    }

    void GUIChat::pressedChatMode()
    {
        if (!historyDisplayEnabled)
        {
            MWBase::Environment::get().getWindowManager()->messageBox(
                localizeArena("chat.history_hidden"));
            return;
        }

        windowState = static_cast<ChatWindowState>((static_cast<int>(windowState) + 1) % CHAT_STATE_COUNT);
        Settings::Manager::setString("mode", "Chat", getModeSetting());
        Settings::Manager::saveUser();
        revealTime = windowState == CHAT_AUTOHIDE ? delay : 0.f;

        const std::string chatMode = getModeMessage();
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Switch chat mode to %s", chatMode.c_str());
        MWBase::Environment::get().getWindowManager()->messageBox(chatMode);

        if (windowState == CHAT_HIDDEN)
        {
            setHistoryReviewState(false);
            setEditState(false);
        }

        refreshPresentation();
    }

    void GUIChat::setEditState(bool state)
    {
        if (state && historyReviewState)
            setHistoryReviewState(false);

        editState = state;
        mCommandLine->setVisible(editState);
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(editState ? mCommandLine : nullptr);

        if (editState)
            revealTime = 0.f;
        else if (windowState == CHAT_AUTOHIDE)
            revealTime = delay;

        refreshPresentation();
    }

    void GUIChat::scrollHistoryToBottom()
    {
        const size_t range = mHistory->getVScrollRange();
        if (range > 0)
            mHistory->setVScrollPosition(range - 1);
        mHistory->setTextCursor(mHistory->getCaption().size());
    }

    void GUIChat::setHistoryReviewState(bool state)
    {
        if (historyReviewState == state)
            return;

        historyReviewState = state;
        if (state)
        {
            editState = false;
            mCommandLine->setVisible(false);
            mMainWidget->setNeedMouseFocus(true);
            mHistory->setNeedMouseFocus(true);
            mHistory->setNeedKeyFocus(true);
            if (mHistoryScroll)
            {
                mHistoryScroll->setVisible(true);
                mHistoryScroll->setNeedMouseFocus(true);
            }
            MWBase::Environment::get().getInputManager()->changeInputMode(true);
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mHistory);
        }
        else
        {
            mMainWidget->setNeedMouseFocus(false);
            mHistory->setNeedMouseFocus(false);
            mHistory->setNeedKeyFocus(false);
            if (mHistoryScroll)
            {
                mHistoryScroll->setVisible(false);
                mHistoryScroll->setNeedMouseFocus(false);
            }
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);
            if (!mainMenuOpen)
                MWBase::Environment::get().getInputManager()->changeInputMode(false);
            scrollHistoryToBottom();
            if (windowState == CHAT_AUTOHIDE)
                revealTime = delay;
        }

        refreshPresentation();
    }

    std::string GUIChat::getHistoryText() const
    {
        return mHistory->getCaption().asUTF8();
    }

    void GUIChat::setMainMenuOpen(bool state)
    {
        if (mainMenuOpen == state)
            return;

        mainMenuOpen = state;
        if (state)
        {
            // The pause menu already owns keyboard/mouse focus. Close the live
            // chat controls without clearing the focus assigned to menu buttons.
            editState = false;
            mCommandLine->setVisible(false);
            historyReviewState = false;
            mMainWidget->setNeedMouseFocus(false);
            mHistory->setNeedMouseFocus(false);
            mHistory->setNeedKeyFocus(false);
            if (mHistoryScroll)
            {
                mHistoryScroll->setVisible(false);
                mHistoryScroll->setNeedMouseFocus(false);
            }
            currentAlpha = 0.f;
            targetAlpha = 0.f;
            hideAfterFade = false;
            applyAlpha(0.f);
            setVisible(false);
        }
        else
            refreshPresentation();
    }

    void GUIChat::pressedSay()
    {
        if (!mCommandLine->getVisible())
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Opening chat.");

        setEditState(true);
    }

    void GUIChat::keyPress(MyGUI::Widget *_sender, MyGUI::KeyCode key, MyGUI::Char _char)
    {
        if (mCommandHistory.empty()) return;

        // Traverse history with up and down arrows
        if (key == MyGUI::KeyCode::ArrowUp)
        {
            // If the user was editing a string, store it for later
            if (mCurrent == mCommandHistory.end())
                mEditString = mCommandLine->getOnlyText();

            if (mCurrent != mCommandHistory.begin())
            {
                --mCurrent;
                mCommandLine->setCaption(*mCurrent);
            }
        }
        else if (key == MyGUI::KeyCode::ArrowDown)
        {
            if (mCurrent != mCommandHistory.end())
            {
                ++mCurrent;

                if (mCurrent != mCommandHistory.end())
                    mCommandLine->setCaption(*mCurrent);
                else
                    // Restore the edit string
                    mCommandLine->setCaption(mEditString);
            }
        }

    }

    void GUIChat::update(float dt)
    {
        syncSettings();

        if (mainMenuOpen)
            return;

        if (revealTime > 0.f && !editState && !historyReviewState)
        {
            revealTime = std::max(0.f, revealTime - dt);
            if (revealTime == 0.f)
                refreshPresentation();
        }

        if (!isVisible())
            return;

        currentAlpha = moveTowards(currentAlpha, targetAlpha, std::max(0.f, dt) * sFadeSpeed);
        applyAlpha(currentAlpha);

        if (hideAfterFade && currentAlpha <= 0.001f)
        {
            currentAlpha = 0.f;
            targetAlpha = 0.f;
            hideAfterFade = false;
            applyAlpha(0.f);
            setVisible(false);
        }
    }

    void GUIChat::refreshPresentation()
    {
        if (mainMenuOpen)
            return;

        if (!historyDisplayEnabled)
        {
            if (editState)
                showSmoothly(sFullyVisibleAlpha);
            else
                hideSmoothly();
            return;
        }

        if (editState || historyReviewState)
        {
            showSmoothly(sFullyVisibleAlpha);
            return;
        }

        if (revealTime > 0.f && windowState != CHAT_HIDDEN)
        {
            showSmoothly(sFullyVisibleAlpha);
            return;
        }

        const float restingAlpha = getRestingAlpha();
        if (restingAlpha > 0.f)
            showSmoothly(restingAlpha);
        else
            hideSmoothly();
    }

    void GUIChat::revealTemporarily()
    {
        revealTime = delay;
        refreshPresentation();
    }

    void GUIChat::showSmoothly(float alpha)
    {
        targetAlpha = std::max(0.f, std::min(1.f, alpha));
        hideAfterFade = false;

        if (!isVisible())
        {
            currentAlpha = 0.f;
            applyAlpha(0.f);
            setVisible(true);
        }
    }

    void GUIChat::hideSmoothly()
    {
        targetAlpha = 0.f;
        hideAfterFade = isVisible();

        if (!isVisible())
        {
            currentAlpha = 0.f;
            hideAfterFade = false;
        }
    }

    void GUIChat::applyAlpha(float alpha)
    {
        mHistory->setAlpha(alpha);
        mCommandLine->setAlpha(alpha);
    }

    float GUIChat::getRestingAlpha() const
    {
        switch (windowState)
        {
            case CHAT_VISIBLE:
                return sFullyVisibleAlpha;
            case CHAT_TRANSPARENT_30:
                return sThirtyPercentTransparentAlpha;
            case CHAT_TRANSPARENT_60:
                return sSixtyPercentTransparentAlpha;
            default:
                return 0.f;
        }
    }

    std::string GUIChat::getModeMessage() const
    {
        switch (windowState)
        {
            case CHAT_VISIBLE:
                return localizeArena("chat.mode.visible");
            case CHAT_TRANSPARENT_30:
                return localizeArena("chat.mode.opacity_30");
            case CHAT_TRANSPARENT_60:
                return localizeArena("chat.mode.opacity_60");
            case CHAT_AUTOHIDE:
                return localizeArena("chat.mode.autohide");
            case CHAT_HIDDEN:
                return localizeArena("chat.mode.hidden");
            default:
                return localizeArena("chat.mode.visible");
        }
    }


    std::string GUIChat::getModeSetting() const
    {
        switch (windowState)
        {
            case CHAT_VISIBLE: return "visible";
            case CHAT_TRANSPARENT_30: return "transparent30";
            case CHAT_TRANSPARENT_60: return "transparent60";
            case CHAT_AUTOHIDE: return "autohide";
            case CHAT_HIDDEN: return "hidden";
            default: return "transparent30";
        }
    }

    void GUIChat::setModeFromSetting(const std::string& mode)
    {
        if (mode == "visible")
            windowState = CHAT_VISIBLE;
        else if (mode == "transparent60")
            windowState = CHAT_TRANSPARENT_60;
        else if (mode == "autohide")
            windowState = CHAT_AUTOHIDE;
        else if (mode == "hidden")
            windowState = CHAT_HIDDEN;
        else
            windowState = CHAT_TRANSPARENT_30;
    }

    void GUIChat::syncSettings()
    {
        const bool enabled = externalHistoryDisplayEnabled
            && Settings::Manager::getBool("enabled", "Chat");
        const float configuredDelay = std::max(0.f, Settings::Manager::getFloat("delay", "Chat"));
        const std::string configuredMode = Settings::Manager::getString("mode", "Chat");

        bool changed = false;
        if (historyDisplayEnabled != enabled)
        {
            historyDisplayEnabled = enabled;
            mHistory->setVisible(enabled);
            if (!enabled && historyReviewState)
                setHistoryReviewState(false);
            changed = true;
        }
        if (std::abs(delay - configuredDelay) > 0.001f)
        {
            delay = configuredDelay;
            revealTime = std::min(revealTime, delay);
            changed = true;
        }

        const ChatWindowState previousState = windowState;
        setModeFromSetting(configuredMode);
        if (windowState != previousState)
        {
            if (windowState == CHAT_HIDDEN)
            {
                if (historyReviewState)
                    setHistoryReviewState(false);
                if (editState)
                    setEditState(false);
            }
            revealTime = windowState == CHAT_AUTOHIDE ? delay : 0.f;
            changed = true;
        }

        if (changed)
            refreshPresentation();
    }

    void GUIChat::setHistoryDisplayEnabled(bool enabled)
    {
        externalHistoryDisplayEnabled = enabled;
        syncSettings();
    }

    void GUIChat::setDelay(float newDelay)
    {
        delay = std::max(0.f, newDelay);
        if (revealTime > delay)
            revealTime = delay;
    }
}
