#ifndef MWGUI_ITEMVIEW_H
#define MWGUI_ITEMVIEW_H

#include <MyGUI_Widget.h>
#include <MyGUI_KeyCode.h>

#include "itemmodel.hpp"

namespace MyGUI
{
    class Button;
    class ScrollView;
    class Widget;
    class ImageBox;
}

namespace MWGui
{

    class ItemView final : public MyGUI::Widget
    {
    MYGUI_RTTI_DERIVED(ItemView)
    public:
        enum ViewMode
        {
            View_Grid,
            View_List
        };

        ItemView();
        ~ItemView() override;

        /// Register needed components with MyGUI's factory manager
        static void registerComponents ();

        /// Takes ownership of \a model
        void setModel (ItemModel* model);

        /// Return the currently owned model. Used by lightweight HUD item views.
        ItemModel* getModel() const { return mModel; }

        /// Compatibility hook for list-style views. ItemView has no external header.
        void disableHeader(bool) {}

        /// Enable ArenaMW's Inventory Extender-style table/grid presentation.
        /// Disabled by default so alchemy and small item pickers keep vanilla layout.
        void setExtendedMode(bool enabled);
        bool getExtendedMode() const { return mExtendedMode; }
        void setViewMode(ViewMode mode);
        ViewMode getViewMode() const { return mViewMode; }

        /// In extended list mode, emit eventItemClicked on a normal mouse
        /// release. Disabled by default so the regular inventory keeps its
        /// focus/double-click behaviour; two-pane transfer windows opt in.
        void setSingleClickActionEnabled(bool enabled) { mSingleClickActionEnabled = enabled; }

        /// Hide the built-in table/grid toggle when a host window provides its
        /// own control (ArenaMW inventory keeps view/search/weight on the bottom bar).
        void setInternalViewModeButtonVisible(bool visible);

        /// Clamp and visually focus an item. Returns the actual focused index.
        int forceItemFocused(int index);

        /// Navigate/activate the item grid from keyboard or controller focus.
        bool handleNavigationKey(MyGUI::KeyCode key);

        /// Height requested by the current item grid/list, including inner margins.
        int requestListSize() const;

        typedef MyGUI::delegates::CMultiDelegate1<ItemModel::ModelIndex> EventHandle_ModelIndex;
        typedef MyGUI::delegates::CMultiDelegate0 EventHandle_Void;
        /// Fired for the classic grid click action.
        EventHandle_ModelIndex eventItemClicked;
        /// Fired when a list row crosses the drag threshold. Kept separate from
        /// click/double-click so list mode can support real hold-drag-release DnD.
        EventHandle_ModelIndex eventItemDragStarted;
        /// Fired when an item row is double-clicked in extended list mode.
        EventHandle_ModelIndex eventItemDoubleClicked;
        /// Fired when the background was clicked (useful for drag and drop)
        EventHandle_Void eventBackgroundClicked;

        void update();

        /// Recalculate the current table/grid geometry without rebuilding the model.
        /// Used after parent-window layout changes that MyGUI alignment may apply
        /// without calling ItemView::setSize directly.
        void relayout();

        void resetScrollBars();

    private:
        void initialiseOverride() override;

        void layoutWidgets();
        void layoutGridWidgets(MyGUI::ScrollView* view);
        void layoutListWidgets(MyGUI::ScrollView* view);
        void updateExtendedGeometry();
        void updateHeaderCaptions();
        void updateHeaderIcons();
        MyGUI::ScrollView* getActiveScrollView() const;
        void clearScrollView(MyGUI::ScrollView* view);

        void setSize(const MyGUI::IntSize& _value) override;
        void setCoord(const MyGUI::IntCoord& _value) override;

        void onSelectedItem (MyGUI::Widget* sender);
        void onListItemPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onListItemDragged(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onListItemReleased(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onListItemDoubleClicked(MyGUI::Widget* sender);
        static ItemView* findVisibleItemViewAt(const MyGUI::IntPoint& point);
        void onSelectedBackground (MyGUI::Widget* sender);
        void onMouseWheelMoved(MyGUI::Widget* _sender, int _rel);
        void onToggleView(MyGUI::Widget* sender);
        void onSortDefault(MyGUI::Widget* sender);
        void onSortName(MyGUI::Widget* sender);
        void onSortCount(MyGUI::Widget* sender);
        void onSortWeight(MyGUI::Widget* sender);
        void onSortValue(MyGUI::Widget* sender);

        ItemModel* mModel;
        MyGUI::ScrollView* mScrollView;
        MyGUI::ScrollView* mListScrollView;
        MyGUI::Widget* mToolbar;
        MyGUI::Button* mViewModeButton;
        MyGUI::ImageBox* mViewModeIcon;
        MyGUI::Button* mDefaultSortButton;
        MyGUI::ImageBox* mDefaultSortIcon;
        MyGUI::Button* mNameHeader;
        MyGUI::Button* mCountHeader;
        MyGUI::Button* mWeightHeader;
        MyGUI::Button* mValueHeader;
        MyGUI::ImageBox* mNameSortIcon;
        MyGUI::ImageBox* mCountSortIcon;
        MyGUI::ImageBox* mWeightSortIcon;
        MyGUI::ImageBox* mValueSortIcon;
        bool mExtendedMode;
        bool mInternalViewModeButtonVisible;
        bool mSingleClickActionEnabled;
        ViewMode mViewMode;
        ItemModel::ModelIndex mListPressedIndex;
        int mListDragStartX;
        int mListDragStartY;
        bool mListDragStarted;
        int mKeyboardFocusedIndex;

    };

}

#endif
