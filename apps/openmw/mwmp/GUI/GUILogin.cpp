#include "GUILogin.hpp"

#include <algorithm>
#include <cctype>

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_LanguageManager.h>

#include <components/settings/settings.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/windowmanager.hpp"
#include "apps/openmw/mwbase/statemanager.hpp"

namespace
{
    std::string normalizeLoginLanguage(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value == "ru" || value == "ru-ru" || value == "russian")
            return "ru";
        return "en";
    }

    std::string arenaText(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }
}

namespace mwmp
{
    GUILogin::GUILogin(Mode mode)
        : MWGui::WindowModal(mode == RegisterMode ? "tes3mp_register.layout" : "tes3mp_login.layout")
        , mMode(mode)
    {
        getWidget(mLogin, "EditLogin");
        getWidget(mPassword, "EditPassword");
        getWidget(mLanguage, "LanguageSelect");
        getWidget(mConnect, "ButtonConnect");
        getWidget(mExit, "ButtonExit");

        if (mMode == RegisterMode)
            getWidget(mPasswordConfirm, "EditPasswordConfirm");

        mPassword->setEditPassword(true);
        if (mPasswordConfirm)
            mPasswordConfirm->setEditPassword(true);

        mLogin->eventEditSelectAccept += MyGUI::newDelegate(this, &GUILogin::onLoginAccepted);
        mPassword->eventEditSelectAccept += MyGUI::newDelegate(this, &GUILogin::onPasswordAccepted);
        if (mPasswordConfirm)
            mPasswordConfirm->eventEditSelectAccept += MyGUI::newDelegate(this, &GUILogin::onPasswordConfirmAccepted);
        mLanguage->eventComboChangePosition += MyGUI::newDelegate(this, &GUILogin::onLanguageChanged);
        mConnect->eventMouseButtonClick += MyGUI::newDelegate(this, &GUILogin::onConnect);
        mExit->eventMouseButtonClick += MyGUI::newDelegate(this, &GUILogin::onExitClicked);

        // The language names are intentionally self-identifying so the selector
        // remains understandable before a language has been chosen.
        mLanguage->addItem("\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9");
        mLanguage->addItem("English");

        setLogin(Settings::Manager::getString("name", "Login"));
        setPassword(Settings::Manager::getString("password", "Login"));
        setLanguage(Settings::Manager::getString("interface language", "General"));

        refreshStrings();
        center();
        setVisible(false);
    }

    std::string GUILogin::getLogin() const
    {
        return mLogin->getOnlyText();
    }

    std::string GUILogin::getPassword() const
    {
        return mPassword->getOnlyText();
    }

    std::string GUILogin::getLanguage() const
    {
        return mLanguage->getIndexSelected() == 0 ? "ru" : "en";
    }

    void GUILogin::setLogin(const std::string& value)
    {
        mLogin->setCaption(value);
    }

    void GUILogin::setPassword(const std::string& value)
    {
        mPassword->setCaption(value);
    }

    void GUILogin::setLanguage(const std::string& value)
    {
        const std::string normalized = normalizeLoginLanguage(value);
        mLanguage->setIndexSelected(normalized == "ru" ? 0 : 1);
        applyLanguage(normalized, false);
        refreshStrings();
    }

    void GUILogin::setLoginEditable(bool editable)
    {
        mLoginEditable = editable;
        mLogin->setEditReadOnly(!editable);
        mLogin->setNeedKeyFocus(editable);
    }

    void GUILogin::setRetryMode(bool retry)
    {
        // Retry wording only applies to an existing account login. Registration
        // always has its own title/copy and never reuses the failed-login state.
        mRetryMode = retry && mMode == LoginMode;
        refreshStrings();
    }

    void GUILogin::onOpen()
    {
        MWGui::WindowModal::onOpen();
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();

        if (mMode == RegisterMode)
        {
            if (mPassword->getOnlyText().empty())
                windowManager->setKeyFocusWidget(mPassword);
            else
                windowManager->setKeyFocusWidget(mPasswordConfirm);
        }
        else if (mLoginEditable && mLogin->getOnlyText().empty())
            windowManager->setKeyFocusWidget(mLogin);
        else
            windowManager->setKeyFocusWidget(mPassword);
    }

    void GUILogin::onConnect(MyGUI::Widget*)
    {
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        if (getLogin().empty())
        {
            windowManager->messageBox("#{arenamp=login.name_required}");
            windowManager->setKeyFocusWidget(mLogin);
            return;
        }
        if (getPassword().empty())
        {
            windowManager->messageBox(mMode == RegisterMode
                ? "#{arenamp=register.password_required}"
                : "#{arenamp=login.password_required}");
            windowManager->setKeyFocusWidget(mPassword);
            return;
        }

        if (mMode == RegisterMode)
        {
            if (!mPasswordConfirm || mPasswordConfirm->getOnlyText().empty())
            {
                windowManager->messageBox("#{arenamp=register.confirm_required}");
                if (mPasswordConfirm)
                    windowManager->setKeyFocusWidget(mPasswordConfirm);
                return;
            }

            if (mPassword->getOnlyText() != mPasswordConfirm->getOnlyText())
            {
                windowManager->messageBox("#{arenamp=register.mismatch}");
                mPasswordConfirm->setCaption("");
                windowManager->setKeyFocusWidget(mPasswordConfirm);
                return;
            }
        }

        applyLanguage(getLanguage(), true);
        eventDone(this);
    }

    void GUILogin::onExitClicked(MyGUI::Widget*)
    {
        MWBase::Environment::get().getStateManager()->requestQuit();
    }

    void GUILogin::onLoginAccepted(MyGUI::Edit*)
    {
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mPassword);
    }

    void GUILogin::onPasswordAccepted(MyGUI::Edit*)
    {
        if (mMode == RegisterMode && mPasswordConfirm)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mPasswordConfirm);
        else
            onConnect(mPassword);
    }

    void GUILogin::onPasswordConfirmAccepted(MyGUI::Edit*)
    {
        onConnect(mPasswordConfirm);
    }

    void GUILogin::onLanguageChanged(MyGUI::ComboBox*, size_t)
    {
        applyLanguage(getLanguage(), true);
        refreshStrings();
    }

    void GUILogin::refreshStrings()
    {
        if (mMode == RegisterMode)
        {
            setText("LoginTitle", arenaText("register.title"));
            setText("LoginSubtitle", arenaText("register.subtitle"));
            setText("LabelName", arenaText("register.name"));
            setText("LabelPassword", arenaText("register.password"));
            setText("LabelPasswordConfirm", arenaText("register.confirm"));
            setText("LabelLanguage", arenaText("register.language"));
            mConnect->setCaption(arenaText("register.create"));
            mExit->setCaption(arenaText("register.exit"));
            return;
        }

        setText("LoginTitle", arenaText(mRetryMode ? "login.retry_title" : "login.title"));
        setText("LoginSubtitle", arenaText(mRetryMode ? "login.retry_subtitle" : "login.subtitle"));
        setText("LabelName", arenaText("login.name"));
        setText("LabelPassword", arenaText("login.password"));
        setText("LabelLanguage", arenaText("login.language"));
        mConnect->setCaption(arenaText("login.login"));
        mExit->setCaption(arenaText("login.exit"));
    }

    void GUILogin::applyLanguage(const std::string& language, bool persist)
    {
        const std::string normalized = normalizeLoginLanguage(language);
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        windowManager->setArenaLanguage(normalized);
        Settings::Manager::setString("interface language", "General", normalized);
        if (persist)
            Settings::Manager::saveUser();
    }
}
