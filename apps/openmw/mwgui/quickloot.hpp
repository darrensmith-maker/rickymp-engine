#ifndef MWGUI_QUICKLOOT_H
#define MWGUI_QUICKLOOT_H

#include <array>
#include <string>

#include <MyGUI_KeyCode.h>

#include "layout.hpp"
#include "../mwworld/ptr.hpp"

#include "widgets.hpp"
#include "itemmodel.hpp"
#include "sortfilteritemmodel.hpp"

namespace MyGUI
{
    class TextBox;
    class Widget;
}

namespace MWGui
{
    class ItemWidget;

    class QuickLoot : public Layout
    {
    public:
        QuickLoot();
        ~QuickLoot() override;

        void onFrame(float frameDuration);
        void update(float frameDuration);

        void setEnabled(bool enabled);
        void setDelay(float delay);
        void setStationaryDelay(float delay);

        bool isVisible() const { return mMainWidget->getVisible() && mQuickLoot->getVisible(); }
        bool isPlaying() const { return mPlaying; }
        void setPlaying(bool playing) { mPlaying = playing; }

        /// Select exactly one previous/next QuickLoot row and consume the wheel event.
        bool handleMouseWheel(int rel);

        /// Activate the selected row. The container header opens the regular inventory.
        bool activateSelected();

        /// Handle QuickLoot-specific keyboard input. This declaration is part of the
        /// WindowManager/QuickLoot interface and must stay in sync with quickloot.cpp.
        bool handleKeyPress(MyGUI::KeyCode key);

        void clear();

        void setFocusObject(const MWWorld::Ptr& focus);
        void setFocusObjectScreenCoords(float min_x, float min_y, float max_x, float max_y);
        ///< set the screen-space position of the tooltip for focused object

        bool checkOwned();

        void resize();
        void ensureTrapTriggered();

    private:
        // One container header plus up to six visible item rows.
        static constexpr int sVisibleRows = 7;

        struct RowWidgets
        {
            MyGUI::Widget* mRoot = nullptr;
            MyGUI::TextBox* mMarker = nullptr;
            ItemWidget* mIcon = nullptr;
            MyGUI::TextBox* mCount = nullptr;
            MyGUI::TextBox* mWeight = nullptr;
            MyGUI::TextBox* mValue = nullptr;
            MyGUI::TextBox* mName = nullptr;
        };

        void playOpenAnimation();
        void playCloseAnimation() const;
        void setVisibleAll(bool visible);

        void clearModels();
        int getEntryCount() const;
        void refreshRows();
        void openStandardContainer();

        void onKeyButtonPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character);
        void onItemSelected(int index);

        MyGUI::Widget* mQuickLoot;
        ItemModel* mModel;
        std::array<RowWidgets, sVisibleRows> mRows;
        SortFilterItemModel* mSortModel;

        /// has the current container been "opened"
        bool mOpened;
        bool mShouldOpen;

        bool mHidden;
        bool mPlaying;
        bool mDismissed;

        MWWorld::Ptr mFocusObject;
        MWWorld::Ptr mLastFocusObject;
        std::string mContainerName;

        float mFocusToolTipX;
        float mFocusToolTipY;

        /// Adjust position for a tooltip so that it doesn't leave the screen and does not obscure the mouse cursor
        void position(MyGUI::IntPoint& position, MyGUI::IntSize size, MyGUI::IntSize viewportSize);

        float mDelay;
        float mRemainingDelay; // remaining time until tooltip will show

        int mLastMouseX;
        int mLastMouseY;

        bool mEnabled;
        float mFrameDuration;

        // QuickLoot must not interrupt traversal or combat. It becomes available
        // only after the player has remained stationary while focusing the same
        // container for a short dwell period.
        float mStationaryTime;
        float mStationaryDelay;
        bool mReadyToShow;
        bool mHasLastPlayerPosition;
        float mLastPlayerX;
        float mLastPlayerY;
        float mLastPlayerZ;

        /// Global row: 0 is the container header, item rows begin at 1.
        int mLastIndex;
        int mVisibleStart;
    };
}
#endif
