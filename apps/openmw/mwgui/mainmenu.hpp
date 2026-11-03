#ifndef OPENMW_GAME_MWGUI_MAINMENU_H
#define OPENMW_GAME_MWGUI_MAINMENU_H

#include "windowbase.hpp"

namespace Gui
{
    class ImageButton;
}

namespace VFS
{
    class Manager;
}

namespace MWGui
{

    class BackgroundImage;
    class SaveGameDialog;
    class VideoWidget;

    class MainMenu : public WindowBase
    {
            int mWidth;
            int mHeight;

            bool mHasAnimatedMenu;

        public:

            MainMenu(int w, int h, const VFS::Manager* vfs, const std::string& versionDescription);
            ~MainMenu();

            void onResChange(int w, int h) override;

            void setVisible (bool visible) override;

            void onFrame(float dt) override;

            bool exit() override;

        private:
            const VFS::Manager* mVFS;

            MyGUI::Widget* mButtonBox;
            MyGUI::TextBox* mVersionText;
            MyGUI::TextBox* mPauseBrandText;
            MyGUI::EditBox* mChatHistory;
            std::string mLastChatHistory;

            BackgroundImage* mBackground;

            MyGUI::ImageBox* mVideoBackground;
            VideoWidget* mVideo; // For animated main menus

            std::map<std::string, Gui::ImageButton*> mButtons;

            void onButtonClicked (MyGUI::Widget* sender);
            void onNewGameConfirmed();
            void onExitConfirmed();

            void showBackground(bool show);

            void updateMenu();
            void updateChatGeometry();
            void updateChatHistory();

            SaveGameDialog* mSaveGameDialog;
    };

}

#endif
