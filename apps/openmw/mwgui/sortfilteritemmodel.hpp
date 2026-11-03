#ifndef MWGUI_SORT_FILTER_ITEM_MODEL_H
#define MWGUI_SORT_FILTER_ITEM_MODEL_H

#include "itemmodel.hpp"

namespace MWGui
{

    class SortFilterItemModel : public ProxyItemModel
    {
    public:
        SortFilterItemModel (ItemModel* sourceModel);

        void update() override;

        bool filterAccepts (const ItemStack& item);

        bool allowedToUseItems() const override;
        ItemStack getItem (ModelIndex index) override;
        size_t getItemCount() override;

        /// Dragged items are not displayed.
        void addDragItem (const MWWorld::Ptr& dragItem, size_t count);
        void clearDragItems();

        void setCategory (int category);
        void setFilter (int filter);
        void setNameFilter (const std::string& filter);
        void setEffectFilter (const std::string& filter);

        /// Inventory-only presentation option used by ArenaMW's virtual key ring.
        /// Raw keys stay in the real ContainerStore (doors/scripts still see them),
        /// while this proxy replaces their individual rows with one synthetic
        /// key-ring item using the first key icon and the total key count.
        void setHideKeys(bool hide) { mHideKeys = hide; }

        /// Use ItemStack::Type for sorting?
        void setSortByType(bool sort) { mSortByType = sort; }

        enum SortMode
        {
            Sort_Default,
            Sort_Name,
            Sort_Count,
            Sort_Weight,
            Sort_Value
        };

        void setSortMode(SortMode mode, bool ascending);
        void toggleSortMode(SortMode mode);
        SortMode getSortMode() const { return mSortMode; }
        bool getSortAscending() const { return mSortAscending; }

        void onClose() override;
        bool onDropItem(const MWWorld::Ptr &item, int count) override;
        bool onTakeItem(const MWWorld::Ptr &item, int count) override;

        static constexpr int Category_Weapon = (1<<1);
        static constexpr int Category_Apparel = (1<<2);
        static constexpr int Category_Misc = (1<<3);
        static constexpr int Category_Magic = (1<<4);
        static constexpr int Category_Keys = (1<<5);
        static constexpr int Category_All = 255;
        // QuickLoot displays every ordinary inventory category.
        static constexpr int Category_Simple = Category_All;

        static constexpr int Filter_OnlyIngredients = (1<<0);
        static constexpr int Filter_OnlyEnchanted = (1<<1);
        static constexpr int Filter_OnlyEnchantable = (1<<2);
        static constexpr int Filter_OnlyChargedSoulstones = (1<<3);
        static constexpr int Filter_OnlyUsableItems = (1<<4); // Only items with a Use action
        static constexpr int Filter_OnlyRepairable = (1<<5);
        static constexpr int Filter_OnlyRechargable = (1<<6);
        static constexpr int Filter_OnlyRepairTools = (1<<7);


    private:
        std::vector<ItemStack> mItems;

        std::vector<std::pair<MWWorld::Ptr, size_t> > mDragItems;

        int mCategory;
        int mFilter;
        bool mSortByType;
        bool mHideKeys;
        SortMode mSortMode;
        bool mSortAscending;

        std::string mNameFilter; // filter by item name
        std::string mEffectFilter; // filter by magic effect
    };

}

#endif
