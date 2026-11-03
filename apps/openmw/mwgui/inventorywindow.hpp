#ifndef MGUI_Inventory_H
#define MGUI_Inventory_H

#include "windowpinnablebase.hpp"
#include "mode.hpp"

#include "../mwworld/ptr.hpp"
#include "../mwrender/characterpreview.hpp"

namespace MyGUI
{
    class ScrollView;
}

namespace osg
{
    class Group;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWGui
{
    namespace Widgets
    {
        class MWDynamicStat;
    }

    class ItemView;
    class SortFilterItemModel;
    class TradeItemModel;
    class DragAndDrop;
    class ItemModel;

    class InventoryWindow : public WindowPinnableBase
    {
        public:
            InventoryWindow(DragAndDrop* dragAndDrop, osg::Group* parent, Resource::ResourceSystem* resourceSystem);

            void onOpen() override;

            /// start trading, disables item drag&drop
            void setTrading(bool trading);

            void onFrame(float dt) override;

            void pickUpObject (MWWorld::Ptr object);

            MWWorld::Ptr getAvatarSelectedItem(int x, int y);
            int getPlayerGold() const;
            std::string resolveGoldIcon() const;

            void rebuildAvatar();

            SortFilterItemModel* getSortFilterModel();
            TradeItemModel* getTradeModel();
            ItemModel* getModel();
            ItemView* getItemView() const { return mItemView; }

            void updateItemView();

            /// Barter drop target used by TradeWindow for visual drag-and-drop.
            void completeBarterDragToMerchant(int sourceIndex, int count);

            void updatePlayer();

            void clear() override;

            void useItem(const MWWorld::Ptr& ptr, bool force=false);

            void setGuiMode(GuiMode mode);

            /// Cycle to previous/next weapon
            void cycle(bool next);

        protected:
            void onTitleDoubleClicked() override;

        private:
            DragAndDrop* mDragAndDrop;

            int mSelectedItem;

            MWWorld::Ptr mPtr;

            MWGui::ItemView* mItemView;
            SortFilterItemModel* mSortModel;
            TradeItemModel* mTradeModel;

            MyGUI::Widget* mAvatar;
            MyGUI::ImageBox* mAvatarImage;
            MyGUI::TextBox* mArmorRating;
            Widgets::MWDynamicStat* mEncumbranceBar;

            MyGUI::Widget* mLeftPane;
            MyGUI::Widget* mRightPane;
            MyGUI::Widget* mCategories;
            MyGUI::Widget* mBottomBar;

            // Native Inventory Extender layout toggle. Unlike the OpenMW 0.51
            // Lua version we can keep the vanilla paper doll available, but
            // hide it to give the table the full inventory width.
            MyGUI::Button* mPaperDollButton;
            MyGUI::ImageBox* mPaperDollIcon;
            MyGUI::Button* mViewModeButton;
            MyGUI::Widget* mWriterButton;
            MyGUI::ImageBox* mWriterIcon;
            MyGUI::ImageBox* mViewModeIcon;
            MyGUI::ImageBox* mGoldIcon;
            MyGUI::TextBox* mGoldLabel;
            bool mPaperDollVisible;

            MyGUI::Button* mFilterAll;
            MyGUI::Button* mFilterWeapon;
            MyGUI::Button* mFilterApparel;
            MyGUI::Button* mFilterMagic;
            MyGUI::Button* mFilterMisc;
            MyGUI::Button* mFilterKeys;

            MyGUI::EditBox* mFilterEdit;

            // The key ring itself is rendered as a virtual item inside ItemView.
            // This panel is only the popup that shows the contained real keys.
            MyGUI::Widget* mKeyRingPanel;
            MyGUI::TextBox* mKeyRingTitle;
            MyGUI::TextBox* mKeyRingWeight;
            MyGUI::ScrollView* mKeyRingList;
            bool mKeyRingOpen;
            float mKeyRingUpdateTimer;

            void refreshKeyRingPopupRows();

            GuiMode mGuiMode;

            int mLastXSize;
            int mLastYSize;

            std::unique_ptr<MyGUI::ITexture> mPreviewTexture;
            std::unique_ptr<MWRender::InventoryPreview> mPreview;

            bool mTrading;
            float mUpdateTimer;

            void toggleMaximized();

            void onItemSelected(int index);
            void onItemDragStarted(int index);
            void onItemDoubleClicked(int index);
            void onItemSelectedFromSourceModel(int index);

            void onBackgroundSelected();

            std::string getModeSetting() const;

            void sellItem(MyGUI::Widget* sender, int count);
            void dragItem(MyGUI::Widget* sender, int count);

            void onWindowResize(MyGUI::Window* _sender);
            void onFilterChanged(MyGUI::Widget* _sender);
            void onNameFilterChanged(MyGUI::EditBox* _sender);
            void onAvatarClicked(MyGUI::Widget* _sender);
            void onPaperDollClicked(MyGUI::Widget* _sender);
            void refreshPaperDollToggleVisual();
            void refreshWriterButtonVisual();
            void onViewModeClicked(MyGUI::Widget* _sender);
            void onWriterClicked(MyGUI::Widget* _sender);
            void onKeyRingClicked(MyGUI::Widget* _sender);
            void onPinToggled() override;

            void updateEncumbranceBar();
            void notifyContentChanged();
            void dirtyPreview();
            void updatePreviewSize();
            void updateArmorRating();
            void updateKeyRing();
            void updateBottomControls();
            void adjustKeyRingLayout();

            void adjustPanes();

            /// Unequips count items from mSelectedItem, if it is equipped, and then updates mSelectedItem in case the items were re-stacked
            void ensureSelectedItemUnequipped(int count);
    };
}

#endif // Inventory_H
