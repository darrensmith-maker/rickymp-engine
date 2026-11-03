#ifndef OPENMW_GUICHAT_HPP
#define OPENMW_GUICHAT_HPP

#include <list>
#include <string>
#include <vector>

#include "apps/openmw/mwgui/windowbase.hpp"

namespace MyGUI
{
    class ScrollBar;
}

namespace mwmp
{
    class GUIController;
    class GUIChat : public MWGui::WindowBase
    {
        friend class GUIController;
    public:
        enum ChatWindowState
        {
            CHAT_VISIBLE = 0,
            CHAT_TRANSPARENT_30,
            CHAT_TRANSPARENT_60,
            CHAT_AUTOHIDE,
            CHAT_HIDDEN,
            CHAT_STATE_COUNT
        };

        MyGUI::EditBox* mCommandLine;
        MyGUI::EditBox* mHistory;
        MyGUI::ScrollBar* mHistoryScroll;

        typedef std::list<std::string> StringList;

        // History of previous entered commands
        StringList mCommandHistory;
        StringList::iterator mCurrent;
        std::string mEditString;

        GUIChat(int x, int y, int w, int h);

        void pressedChatMode(); //switch chat mode
        void pressedSay(); // show chat input, including while the chat itself is hidden
        void setDelay(float newDelay);
        void setHistoryDisplayEnabled(bool enabled);

        void update(float dt);

        virtual void onOpen();
        virtual void onClose();

        virtual bool exit();

        bool getEditState();
        std::string getHistoryText() const;
        void setMainMenuOpen(bool state);

        void setFont(const std::string &fntName);

        void onResChange(int width, int height);

        // Print a message to the console, in specified color.
        void print(const std::string &msg, const std::string& color = "#FFFFFF");

        // Clean chat
        void clean();

        // These are pre-colored versions that you should use.

        /// Output from successful console command
        void printOK(const std::string &msg);

        /// Error message
        void printError(const std::string &msg);

        void send(const std::string &str);

    protected:

    private:

        void keyPress(MyGUI::Widget* _sender,
                      MyGUI::KeyCode key,
                      MyGUI::Char _char);

        void acceptCommand(MyGUI::EditBox* _sender);

        void setEditState(bool state);
        void setHistoryReviewState(bool state);
        void scrollHistoryToBottom();
        void refreshPresentation();
        void revealTemporarily();
        void showSmoothly(float targetAlpha);
        void hideSmoothly();
        void applyAlpha(float alpha);
        float getRestingAlpha() const;
        std::string getModeMessage() const;
        std::string getModeSetting() const;
        void setModeFromSetting(const std::string& mode);
        void syncSettings();

        ChatWindowState windowState;
        bool editState;
        bool historyReviewState;
        bool mainMenuOpen;
        bool historyDisplayEnabled;
        bool externalHistoryDisplayEnabled;
        bool hideAfterFade;
        float delay;
        float revealTime;
        float currentAlpha;
        float targetAlpha;
    };
}
#endif //OPENMW_GUICHAT_HPP
