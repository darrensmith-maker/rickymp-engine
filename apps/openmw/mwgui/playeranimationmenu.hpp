#ifndef MWGUI_PLAYERANIMATIONMENU_H
#define MWGUI_PLAYERANIMATIONMENU_H

#include "windowbase.hpp"

#include <MyGUI_KeyCode.h>
#include <MyGUI_Types.h>

#include <string>
#include <vector>

namespace MyGUI
{
    class ListBox;
    class TextBox;
}

namespace MWGui
{
    class PlayerAnimationMenu : public WindowBase
    {
    public:
        PlayerAnimationMenu();

        void onOpen() override;
        void onResChange(int width, int height) override;
        MyGUI::Widget* getDefaultKeyFocus() override;

        /// Cycle the current selection exactly one row and keep it visible.
        bool handleMouseWheel(int rel);

    private:
        enum class Stage
        {
            Group,
            Animation
        };

        struct AnimationEntry
        {
            std::string mCaption;
            std::string mGroup;
            float mSpeed;
            bool mOneShot;
            float mDuration;
            int mLoops;
            int mProp;
            bool mThirdPersonOnly;
            bool mWalkStyle;
        };

        void showGroups();
        void showAnimations(const std::string& group);
        void closeMenu();
        void onAccept(MyGUI::ListBox* sender, size_t index);
        void onKeyButtonPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character);
        void positionWindow();
        void selectRelative(int direction);

        MyGUI::TextBox* mTitle;
        MyGUI::ListBox* mList;
        Stage mStage;
        std::vector<std::string> mGroups;
        std::vector<AnimationEntry> mEntries;
    };
}

#endif
