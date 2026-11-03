#ifndef OPENMW_GUILOGIN_HPP
#define OPENMW_GUILOGIN_HPP

#include "apps/openmw/mwgui/windowbase.hpp"

namespace MyGUI
{
    class Button;
    class ComboBox;
    class EditBox;
}

namespace mwmp
{
    /// ArenaMP account front-end used before the TES3MP base-info/login handshake.
    /// It deliberately collects name + password + server-interface language in one
    /// place; the password is then auto-submitted when the server sends its normal
    /// PasswordDialog so the wire protocol stays compatible with existing servers.
    class GUILogin : public MWGui::WindowModal
    {
    public:
        enum Mode
        {
            LoginMode,
            RegisterMode
        };

        explicit GUILogin(Mode mode = LoginMode);

        std::string getLogin() const;
        std::string getPassword() const;
        std::string getLanguage() const;
        bool isRegistrationMode() const { return mMode == RegisterMode; }

        void setLogin(const std::string& value);
        void setPassword(const std::string& value);
        void setLanguage(const std::string& value);
        void setLoginEditable(bool editable);
        void setRetryMode(bool retry);

        void onOpen() override;
        bool exit() override { return false; }

        MWGui::WindowBase::EventHandle_WindowBase eventDone;

    private:
        void onConnect(MyGUI::Widget* sender);
        void onExitClicked(MyGUI::Widget* sender);
        void onLoginAccepted(MyGUI::Edit* sender);
        void onPasswordAccepted(MyGUI::Edit* sender);
        void onPasswordConfirmAccepted(MyGUI::Edit* sender);
        void onLanguageChanged(MyGUI::ComboBox* sender, size_t index);
        void refreshStrings();
        void applyLanguage(const std::string& language, bool persist);

        MyGUI::EditBox* mLogin = nullptr;
        MyGUI::EditBox* mPassword = nullptr;
        MyGUI::EditBox* mPasswordConfirm = nullptr;
        MyGUI::ComboBox* mLanguage = nullptr;
        MyGUI::Button* mConnect = nullptr;
        MyGUI::Button* mExit = nullptr;
        Mode mMode = LoginMode;
        bool mRetryMode = false;
        bool mLoginEditable = true;
    };
}

#endif // OPENMW_GUILOGIN_HPP
