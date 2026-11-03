#ifndef OPENMW_MWGUI_COMPANIONWINDOW_H
#define OPENMW_MWGUI_COMPANIONWINDOW_H

#include "windowbase.hpp"
#include "referenceinterface.hpp"

namespace MWGui
{
    namespace Widgets
    {
        class MWDynamicStat;
    }

    class MessageBoxManager;
    class ItemView;
    class DragAndDrop;
    class SortFilterItemModel;
    class CompanionItemModel;

    class CompanionWindow : public WindowBase, public ReferenceInterface
    {
    public:
        CompanionWindow(DragAndDrop* dragAndDrop, MessageBoxManager* manager);

        bool exit() override;

        void resetReference() override;

        void setPtr(const MWWorld::Ptr& npc) override;
        void onFrame (float dt) override;
        void clear() override { resetReference(); }

        // TES3MP: expose server-approved companion drags to ObjectList echo handling.
        bool isOnDragAndDrop() const;
        bool dragItemByPtr(const MWWorld::Ptr& itemPtr, int dragCount);

    private:
        ItemView* mItemView;
        SortFilterItemModel* mSortModel;
        CompanionItemModel* mModel;
        int mSelectedItem;

        DragAndDrop* mDragAndDrop;

        MyGUI::Button* mCloseButton;
        MyGUI::Button* mFilterAll;
        MyGUI::Button* mFilterWeapon;
        MyGUI::Button* mFilterApparel;
        MyGUI::Button* mFilterMagic;
        MyGUI::Button* mFilterMisc;
        MyGUI::EditBox* mFilterEdit;
        MyGUI::TextBox* mProfitLabel;
        Widgets::MWDynamicStat* mEncumbranceBar;
        MessageBoxManager* mMessageBoxManager;

        void onItemSelected(int index);
        void onItemDragStarted(int index);
        void onItemDoubleClicked(int index);
        void onNameFilterChanged(MyGUI::EditBox* _sender);
        void onFilterChanged(MyGUI::Widget* sender);
        void onBackgroundSelected();
        void dragItem(MyGUI::Widget* sender, int count);
        bool requestDrag(int count, ItemView* pendingTarget);
        void dropItem();

        void onMessageBoxButtonClicked(int button);

        void updateEncumbranceBar();
        void syncCompanionEquipment();

        void onCloseButtonClicked(MyGUI::Widget* _sender);

        void onReferenceUnavailable() override;
    };

}

#endif
