#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>

#include <SDL_system.h>

#include <MyGUI_FactoryManager.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ScrollView.h>

#include <extern/PicoSHA2/picosha2.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/inputmanager.hpp"

#include "../mwgui/mapwindow.hpp"

#include "../mwworld/worldimp.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/cellstore.hpp"

#include "GUIController.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "GUI/PlayerMarkerCollection.hpp"
#include "GUI/GUIDialogList.hpp"
#include "GUI/GUIChat.hpp"
#include "LocalPlayer.hpp"
#include "DedicatedPlayer.hpp"
#include "PlayerList.hpp"


mwmp::GUIController::GUIController()
    : mInputBox(nullptr)
    , mAccountLoginBox(nullptr)
    , mListBox(nullptr)
    , mPreLoginPasswordAutoSubmitted(false)
{
    mChat = nullptr;
    keySay = SDL_SCANCODE_Y;
    keyChatMode = SDL_SCANCODE_F2;
}

mwmp::GUIController::~GUIController()
{

}

void mwmp::GUIController::cleanUp()
{
    mPlayerMarkers.clear();
    if (mChat != nullptr)
        delete mChat;
    mChat = nullptr;

    // A fresh connection gets one automatic submission from the credentials
    // collected by the account card, even if the previous connection ended
    // before authentication completed.
    mPreLoginPasswordAutoSubmitted = false;
}

void mwmp::GUIController::refreshGuiMode(MWGui::GuiMode guiMode)
{
    if (MWBase::Environment::get().getWindowManager()->containsMode(guiMode))
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(guiMode);
        MWBase::Environment::get().getWindowManager()->pushGuiMode(guiMode);
    }
}

void mwmp::GUIController::setupChat()
{
    assert(mChat == nullptr);

    float chatDelay = Settings::Manager::getFloat("delay", "Chat");
    int chatY = Settings::Manager::getInt("y", "Chat");
    int chatX = Settings::Manager::getInt("x", "Chat");
    int chatW = Settings::Manager::getInt("w", "Chat");
    int chatH = Settings::Manager::getInt("h", "Chat");

    keySay = SDL_GetScancodeFromName(Settings::Manager::getString("keySay", "Chat").c_str());
    keyChatMode = SDL_GetScancodeFromName(Settings::Manager::getString("keyChatMode", "Chat").c_str());

    mChat = new GUIChat(chatX, chatY, chatW, chatH);
    mChat->setDelay(chatDelay);
    mChat->setHistoryDisplayEnabled(!Main::isChatHistoryHidden());
}

void mwmp::GUIController::printChatMessage(std::string &msg)
{
    if (mChat != nullptr)
        mChat->print(msg);
}


void mwmp::GUIController::setChatVisible(bool chatVisible)
{
    if (!chatVisible)
    {
        mChat->setHistoryReviewState(false);
        mChat->hideSmoothly();
    }
    else
        mChat->refreshPresentation();
}

void mwmp::GUIController::showDialogList(const mwmp::BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();

    if (mListBox != NULL)
    {
        windowManager->removeDialog(mListBox);
        windowManager->removeCurrentModal(mListBox);
        mListBox = NULL;
    }

    std::vector<std::string> list;

    std::string buf;

    for (const auto &data : guiMessageBox.data)
    {
        if (data == '\n')
        {
            list.push_back(buf);
            buf.erase();
            continue;
        }
        buf += data;
    }

    list.push_back(buf);

    mListBox = new GUIDialogList(guiMessageBox.label, list);
    windowManager->pushGuiMode((MWGui::GuiMode)GM_TES3MP_ListBox);
}

void mwmp::GUIController::showMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    windowManager->messageBox(guiMessageBox.label);
}

std::vector<std::string> splitString(const std::string &str, char delim = ';')
{
    std::istringstream ss(str);
    std::vector<std::string> result;
    std::string token;
    while (std::getline(ss, token, delim))
        result.push_back(token);
    return result;
}

void mwmp::GUIController::showCustomMessageBox(const BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
    std::vector<std::string> buttons = splitString(guiMessageBox.buttons);
    windowManager->interactiveMessageBox(guiMessageBox.label, buttons, false, true);
}

void mwmp::GUIController::showInputBox(const BasePlayer::GUIMessageBox &guiMessageBox)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    LocalPlayer* localPlayer = Main::get().getLocalPlayer();

    const bool passwordDialog = guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog;
    const bool preLoginAccountPrompt = passwordDialog && !localPlayer->isLoggedIn();

    // CoreScripts' stable GUI ids are LOGIN=1 and REGISTER=2 (tableHelper.enum
    // is 1-based). Existing accounts may use the password already collected on
    // the initial ArenaMP account card for a fast login. New accounts must never
    // auto-submit: registration gets a dedicated confirmation screen first.
    // CoreScripts uses REGISTER=2. Keep a note-based fallback as well because
    // older/custom server scripts may preserve PasswordDialog semantics while
    // using a different GUI id. The registration prompt is the only pre-login
    // password dialog that normally carries explanatory note text.
    const bool registrationPrompt = preLoginAccountPrompt
        && (guiMessageBox.id == 2 || !guiMessageBox.note.empty());
    const bool loginPrompt = preLoginAccountPrompt && !registrationPrompt;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "ArenaMP account PasswordDialog: id=%d registration=%d login=%d",
        guiMessageBox.id, registrationPrompt ? 1 : 0, loginPrompt ? 1 : 0);

    if (loginPrompt && !mPreLoginPasswordAutoSubmitted)
    {
        const std::string savedPassword = Settings::Manager::getString("password", "Login");
        if (!savedPassword.empty())
        {
            mPreLoginPasswordAutoSubmitted = true;
            submitInputReply(savedPassword);
            return;
        }
    }

    windowManager->removeDialog(mInputBox);
    mInputBox = nullptr;
    windowManager->removeDialog(mAccountLoginBox);
    mAccountLoginBox = nullptr;

    // Do not stack the same TES3MP modal mode repeatedly when a server replaces
    // one password prompt with another (login retry / registration). A duplicate
    // mode can keep LocalPlayer::processCharGen() blocked even after the visible
    // dialog has gone away.
    const MWGui::GuiMode inputMode = (MWGui::GuiMode)GM_TES3MP_InputBox;
    if (!windowManager->containsMode(inputMode))
        windowManager->pushGuiMode(inputMode);

    if (preLoginAccountPrompt)
    {
        // Once the server knows the player name it also knows whether the account
        // exists. Registration therefore has its own branded card with a second
        // password field. The account name is locked in both modes because changing
        // it after PlayerBaseInfo requires reconnecting.
        mAccountLoginBox = new GUILogin(registrationPrompt ? GUILogin::RegisterMode : GUILogin::LoginMode);
        mAccountLoginBox->setLogin(Settings::Manager::getString("name", "Login"));
        mAccountLoginBox->setPassword(registrationPrompt
            ? Settings::Manager::getString("password", "Login")
            : "");
        mAccountLoginBox->setLanguage(Settings::Manager::getString("interface language", "General"));
        mAccountLoginBox->setLoginEditable(false);
        mAccountLoginBox->setRetryMode(loginPrompt && mPreLoginPasswordAutoSubmitted);
        mAccountLoginBox->eventDone += MyGUI::newDelegate(this, &GUIController::onAccountLoginDone);
        mAccountLoginBox->setVisible(true);
        return;
    }

    // Ordinary server InputDialog/PasswordDialog windows used after login stay
    // compact and keep their original semantics.
    mInputBox = new TextInputDialog();
    mInputBox->setEditPassword(passwordDialog);
    mInputBox->setTextLabel(guiMessageBox.label);
    mInputBox->setTextNote(guiMessageBox.note);
    mInputBox->eventDone += MyGUI::newDelegate(this, &GUIController::onInputBoxDone);
    mInputBox->setVisible(true);
}

void mwmp::GUIController::submitInputReply(const std::string& rawText)
{
    LocalPlayer *localPlayer = Main::get().getLocalPlayer();
    std::string textInput = rawText;

    // Password dialogs use TES3MP's historical client-side double hashing. Keep
    // the plain password only in local settings; the protocol payload remains
    // exactly what existing servers expect.
    if (localPlayer->guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog)
    {
        if (!localPlayer->isLoggedIn())
        {
            Settings::Manager::setString("password", "Login", rawText);
            Settings::Manager::saveUser();
        }

        textInput = picosha2::hash256_hex_string(textInput);
        textInput = picosha2::hash256_hex_string(textInput
            + picosha2::hash256_hex_string(picosha2::hash256_hex_string(textInput)));
    }

    localPlayer->guiMessageBox.data = textInput;

    PlayerPacket *playerPacket = Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX);
    playerPacket->setPlayer(localPlayer);
    playerPacket->Send();
}

void mwmp::GUIController::onInputBoxDone(MWGui::WindowBase *parWindow)
{
    (void)parWindow;
    submitInputReply(mInputBox->getTextInput());

    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    windowManager->removeDialog(mInputBox);
    mInputBox = nullptr;
    windowManager->popGuiMode();
}

void mwmp::GUIController::onAccountLoginDone(MWGui::WindowBase *parWindow)
{
    (void)parWindow;
    if (!mAccountLoginBox)
        return;

    // Capture everything before destroying the modal. In particular, close the
    // TES3MP input GUI mode BEFORE the password reply is sent: registration can
    // make the server answer with ID_PLAYER_CHARGEN immediately, and CharGen is
    // intentionally blocked while any GUI mode is still active.
    const std::string password = mAccountLoginBox->getPassword();
    const std::string language = mAccountLoginBox->getLanguage();
    const bool registration = mAccountLoginBox->isRegistrationMode();

    Settings::Manager::setString("password", "Login", password);
    Settings::Manager::setString("interface language", "General", language);
    Settings::Manager::saveUser();
    MWBase::Environment::get().getWindowManager()->setArenaLanguage(language);

    if (!registration)
        mPreLoginPasswordAutoSubmitted = true;

    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    windowManager->removeDialog(mAccountLoginBox);
    mAccountLoginBox = nullptr;
    windowManager->removeGuiMode((MWGui::GuiMode)GM_TES3MP_InputBox);

    // The account card is shown after the initial PlayerBaseInfo handshake, so
    // re-send the selected RU/EN flag before the password response. The server
    // can then localise registration/login result messages for this player.
    LocalPlayer* localPlayer = Main::get().getLocalPlayer();
    localPlayer->updateLanguage();
    PlayerPacket* baseInfoPacket = Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_BASEINFO);
    baseInfoPacket->setPlayer(localPlayer);
    baseInfoPacket->Send();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
        "Submitting ArenaMP %s password reply after closing TES3MP input mode",
        registration ? "registration" : "login");
    submitInputReply(password);
}

bool mwmp::GUIController::pressedKey(int key)
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();
    if (mChat == nullptr || windowManager->isConsoleMode() || windowManager->getMode() != MWGui::GM_None)
        return false;
    if (key == keyChatMode)
    {
        mChat->pressedChatMode();
        return true;
    }
    else if (key == keySay)
    {
        mChat->pressedSay();
        return true;
    }
    return false;
}

void mwmp::GUIController::changeChatMode()
{
    mChat->pressedChatMode();
}

bool mwmp::GUIController::getChatEditState()
{
    return mChat->editState;
}

std::string mwmp::GUIController::getChatHistoryText() const
{
    return mChat != nullptr ? mChat->getHistoryText() : std::string();
}

bool mwmp::GUIController::getChatHistoryCoord(int& x, int& y, int& width, int& height) const
{
    if (mChat == nullptr || mChat->mHistory == nullptr)
        return false;

    const MyGUI::IntCoord coord = mChat->mHistory->getAbsoluteCoord();
    if (coord.width <= 0 || coord.height <= 0)
        return false;

    x = coord.left;
    y = coord.top;
    width = coord.width;
    height = coord.height;
    return true;
}

void mwmp::GUIController::setChatMainMenuOpen(bool state)
{
    if (mChat != nullptr)
        mChat->setMainMenuOpen(state);
}

void mwmp::GUIController::update(float dt)
{
    if (mChat != nullptr)
        mChat->update(dt);

    // Re-arm automatic credential submission only after a successful login, so
    // a wrong password produces the retry card instead of an infinite resend loop.
    if (Main::get().getLocalPlayer()->isLoggedIn())
        mPreLoginPasswordAutoSubmitted = false;
}

void mwmp::GUIController::processCustomMessageBoxInput(int pressedButton)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Pressed: %d", pressedButton);

    LocalPlayer* localPlayer = Main::get().getLocalPlayer();
    localPlayer->guiMessageBox.data = MyGUI::utility::toString(pressedButton);

    PlayerPacket* playerPacket = Main::get().getNetworking()->getPlayerPacket(ID_GUI_MESSAGEBOX);
    playerPacket->setPlayer(Main::get().getLocalPlayer());
    playerPacket->Send();
}

void mwmp::GUIController::WM_UpdateVisible(MWGui::GuiMode mode)
{
    switch((int)mode)
    {
        case GM_TES3MP_InputBox:
        {
            if (mInputBox != nullptr)
                mInputBox->setVisible(true);
            if (mAccountLoginBox != nullptr)
                mAccountLoginBox->setVisible(true);
            break;
        }
        case GM_TES3MP_ListBox:
        {
            if (mListBox != 0)
                mListBox->setVisible(true);
            break;
        }
        default:
            break;
    }
}

ESM::CustomMarker mwmp::GUIController::createMarker(const RakNet::RakNetGUID &guid)
{
    DedicatedPlayer *player = PlayerList::getPlayer(guid);
    ESM::CustomMarker mEditingMarker;
    if (!player)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Unknown player guid: %s", guid.ToString());
        return mEditingMarker;
    }

    mEditingMarker.mNote = player->npc.mName;

    const ESM::Cell *playerCell = &player->cell;

    mEditingMarker.mCell = player->cell.mCellId;

    mEditingMarker.mWorldX = player->position.pos[0];
    mEditingMarker.mWorldY = player->position.pos[1];

    mEditingMarker.mCell.mPaged = playerCell->isExterior();
    if (!playerCell->isExterior())
        mEditingMarker.mCell.mWorldspace = playerCell->mName;
    else
    {
        mEditingMarker.mCell.mWorldspace = ESM::CellId::sDefaultWorldspace;

        // Don't remove these, or the markers will stop showing up in exteriors
        mEditingMarker.mCell.mIndex.mX = playerCell->getGridX();
        mEditingMarker.mCell.mIndex.mY = playerCell->getGridY();
    }
    return mEditingMarker;
}


void mwmp::GUIController::updatePlayersMarkers(MWGui::LocalMapBase *localMapBase)
{
    // ArenaMP: remote-player marker widgets are intentionally disabled on local maps and the HUD
    // minimap. Recreating one MyGUI widget per network update/player caused instability when several
    // players shared a cell. Keep the marker collection for the world-map tooltip path, but never
    // instantiate local-map widgets for it.
    std::vector<MyGUI::Widget*>::iterator markerWidgetIterator = localMapBase->mPlayerMarkerWidgets.begin();
    for (; markerWidgetIterator != localMapBase->mPlayerMarkerWidgets.end(); ++markerWidgetIterator)
        MyGUI::Gui::getInstance().destroyWidget(*markerWidgetIterator);
    localMapBase->mPlayerMarkerWidgets.clear();
}

void mwmp::GUIController::setGlobalMapMarkerTooltip(MWGui::MapWindow *mapWindow, MyGUI::Widget *markerWidget, int x, int y)
{
    ESM::CellId cellId;
    cellId.mIndex.mX = x;
    cellId.mIndex.mY = y;
    cellId.mWorldspace = ESM::CellId::sDefaultWorldspace;
    cellId.mPaged = true;
    PlayerMarkerCollection::RangeType markers = mPlayerMarkers.getMarkers(cellId);
    std::vector<std::string> destNotes;
    for (PlayerMarkerCollection::ContainerType::const_iterator it = markers.first; it != markers.second; ++it)
        destNotes.push_back(it->second.mNote);

    if (!destNotes.empty())
    {
        MWGui::LocalMapBase::MarkerUserData data (nullptr);
        data.notes = destNotes;
        data.caption = markerWidget->getUserString("Caption_TextOneLine");

        markerWidget->setUserData(data);
        markerWidget->setUserString("ToolTipType", "MapMarker");
    }
    else
        markerWidget->setUserString("ToolTipType", "Layout");
}

void mwmp::GUIController::updateGlobalMapMarkerTooltips(MWGui::MapWindow *mapWindow)
{
    for (const auto &widget : mapWindow->mGlobalMapMarkers)
    {
        const int x = widget.first.first;
        const int y = widget.first.second;
        setGlobalMapMarkerTooltip(mapWindow, widget.second, x, y);
    }
}
