#ifndef MGUI_CONTAINER_H
#define MGUI_CONTAINER_H

#include "windowbase.hpp"
#include "referenceinterface.hpp"

#include "itemmodel.hpp"

namespace MyGUI
{
    class EditBox;
    class Gui;
    class Widget;
    class ImageBox;
}

namespace MWGui
{
    namespace Widgets
    {
        class MWDynamicStat;
    }

    class ContainerWindow;
    class ItemView;
    class SortFilterItemModel;
}


namespace MWGui
{
    class ContainerWindow : public WindowBase, public ReferenceInterface
    {
    public:
        ContainerWindow(DragAndDrop* dragAndDrop);

        void setPtr(const MWWorld::Ptr& container) override;
        void onClose() override;
        void clear() override { resetReference(); }

        void onFrame(float dt) override;

        void resetReference() override;

        /*
            Start of tes3mp addition

            Make it possible to check from elsewhere whether there is currently an
            item being dragged in the container window
        */
        bool isOnDragAndDrop();
        /*
            End of tes3mp addition
        */

        /*
            Start of tes3mp addition

            Make it possible to drag a specific item Ptr instead of having to rely
            on an index that may have changed in the meantime, for drags that
            require approval from the server
        */
        bool dragItemByPtr(const MWWorld::Ptr& itemPtr, int dragCount);
        /*
            End of tes3mp addition
        */

    private:
        DragAndDrop* mDragAndDrop;

        MWGui::ItemView* mItemView;
        SortFilterItemModel* mSortModel;
        ItemModel* mModel;
        int mSelectedItem;

        MyGUI::Button* mTakeButton;
        MyGUI::Button* mCloseButton;
        MyGUI::Button* mFilterAll;
        MyGUI::Button* mFilterWeapon;
        MyGUI::Button* mFilterApparel;
        MyGUI::Button* mFilterMagic;
        MyGUI::Button* mFilterMisc;
        MyGUI::Button* mFilterKeys;
        MyGUI::EditBox* mFilterEdit;
        Widgets::MWDynamicStat* mEncumbranceBar;
        MyGUI::Widget* mBottomBar;
        MyGUI::Button* mViewModeButton;
        MyGUI::ImageBox* mViewModeIcon;

        void onItemSelected(int index);
        void onItemDragStarted(int index);
        void onItemDoubleClicked(int index);
        void onBackgroundSelected();
        void dragItem(MyGUI::Widget* sender, int count);
        bool requestDrag(int count, ItemView* pendingTarget);
        void dropItem();
        void onCloseButtonClicked(MyGUI::Widget* _sender);
        void onTakeAllButtonClicked(MyGUI::Widget* _sender);
        void onDisposeCorpseButtonClicked(MyGUI::Widget* sender);
        void onNameFilterChanged(MyGUI::EditBox* sender);
        void onFilterChanged(MyGUI::Widget* sender);
        void onViewModeClicked(MyGUI::Widget* sender);
        void updateEncumbranceBar();
        void updateBottomBarLayout();

        /// @return is taking the item allowed?
        bool onTakeItem(const ItemStack& item, int count);

        void onReferenceUnavailable() override;
    };
}
#endif // CONTAINER_H
