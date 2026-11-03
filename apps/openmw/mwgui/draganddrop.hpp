#ifndef OPENMW_MWGUI_DRAGANDDROP_H
#define OPENMW_MWGUI_DRAGANDDROP_H

#include "itemmodel.hpp"

namespace MyGUI
{
    class Widget;
}

namespace MWGui
{

    class ItemView;
    class SortFilterItemModel;

    class DragAndDrop
    {
    public:
        bool mIsOnDragAndDrop;
        MyGUI::Widget* mDraggedWidget;
        ItemModel* mSourceModel;
        ItemView* mSourceView;
        SortFilterItemModel* mSourceSortModel;
        ItemStack mItem;
        int mDraggedCount;
        int mSourceIndex;

        enum DragMode
        {
            Drag_Normal,
            Drag_BarterPreview
        };

        DragAndDrop();

        void startDrag (int index, SortFilterItemModel* sortModel, ItemModel* sourceModel, ItemView* sourceView, int count);
        /// Visual-only barter drag. The item is only staged into the transaction
        /// when released over the opposite barter ItemView.
        void startBarterDrag(int index, SortFilterItemModel* sortModel, ItemModel* sourceModel, ItemView* sourceView, int count);
        bool isBarterDrag() const { return mDragMode == Drag_BarterPreview; }
        void setTransferTargetView(ItemView* view) { mTransferTargetView = view; }
        void clearTransferTargetView(ItemView* view) { if (mTransferTargetView == view) mTransferTargetView = nullptr; }
        ItemView* getTransferTargetView() const { return mTransferTargetView; }

        // TES3MP: a drag out of a server-authoritative container may be approved
        // after the mouse button was already released. Keep the intended target
        // until the echoed ID_CONTAINER/DRAG creates the real DragAndDrop.
        void beginServerDrag(ItemView* sourceView);
        bool isServerDragPending() const { return mPendingServerDrag; }
        bool queuePendingServerDropTarget(ItemView* targetView);
        ItemView* takePendingServerDropTarget(ItemView* sourceView);
        void cancelPendingServerDrag(ItemView* sourceView = nullptr);
        void drop (ItemModel* targetModel, ItemView* targetView);
        void onFrame();

        // TES3MP keeps the legacy deleteDragItems path for server-approved
        // container drops: the server reply owns the final destination state.
        void finish(bool deleteDragItems = false);

    private:
        DragMode mDragMode;
        ItemView* mTransferTargetView;
        bool mPendingServerDrag;
        ItemView* mPendingServerSourceView;
        ItemView* mPendingServerDropTarget;
        void createDragWidget();
    };

}

#endif
