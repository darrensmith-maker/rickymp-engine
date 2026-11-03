#include "sortfilteritemmodel.hpp"

#include <MyGUI_LanguageManager.h>

#include <components/misc/stringops.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/loadalch.hpp>
#include <components/esm/loadappa.hpp>
#include <components/esm/loadarmo.hpp>
#include <components/esm/loadbook.hpp>
#include <components/esm/loadclot.hpp>
#include <components/esm/loadingr.hpp>
#include <components/esm/loadlock.hpp>
#include <components/esm/loadligh.hpp>
#include <components/esm/loadmisc.hpp>
#include <components/esm/loadprob.hpp>
#include <components/esm/loadrepa.hpp>
#include <components/esm/loadweap.hpp>
#include <components/esm/loadench.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/nullaction.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/alchemy.hpp"

namespace
{
    bool compareType(const std::string& type1, const std::string& type2)
    {
        // this defines the sorting order of types. types that are first in the vector appear before other types.
        std::vector<std::string> mapping;
        mapping.emplace_back(typeid(ESM::Weapon).name() );
        mapping.emplace_back(typeid(ESM::Armor).name() );
        mapping.emplace_back(typeid(ESM::Clothing).name() );
        mapping.emplace_back(typeid(ESM::Potion).name() );
        mapping.emplace_back(typeid(ESM::Ingredient).name() );
        mapping.emplace_back(typeid(ESM::Apparatus).name() );
        mapping.emplace_back(typeid(ESM::Book).name() );
        mapping.emplace_back(typeid(ESM::Light).name() );
        mapping.emplace_back(typeid(ESM::Miscellaneous).name() );
        mapping.emplace_back(typeid(ESM::Lockpick).name() );
        mapping.emplace_back(typeid(ESM::Repair).name() );
        mapping.emplace_back(typeid(ESM::Probe).name() );

        assert( std::find(mapping.begin(), mapping.end(), type1) != mapping.end() );
        assert( std::find(mapping.begin(), mapping.end(), type2) != mapping.end() );

        return std::find(mapping.begin(), mapping.end(), type1) < std::find(mapping.begin(), mapping.end(), type2);
    }

    struct Compare
    {
        bool mSortByType;
        MWGui::SortFilterItemModel::SortMode mSortMode;
        bool mAscending;

        Compare()
            : mSortByType(true)
            , mSortMode(MWGui::SortFilterItemModel::Sort_Default)
            , mAscending(true)
        {
        }

        template <class T>
        bool ordered(const T& left, const T& right) const
        {
            return mAscending ? left < right : right < left;
        }

        bool operator() (const MWGui::ItemStack& left, const MWGui::ItemStack& right)
        {
            if (mSortByType && left.mType != right.mType)
                return left.mType < right.mType;

            std::string leftName = Misc::StringUtils::lowerCaseUtf8(left.mBase.getClass().getName(left.mBase));
            std::string rightName = Misc::StringUtils::lowerCaseUtf8(right.mBase.getClass().getName(right.mBase));

            if (mSortMode == MWGui::SortFilterItemModel::Sort_Name && leftName != rightName)
                return ordered(leftName, rightName);
            if (mSortMode == MWGui::SortFilterItemModel::Sort_Count && left.mCount != right.mCount)
                return ordered(left.mCount, right.mCount);
            if (mSortMode == MWGui::SortFilterItemModel::Sort_Weight)
            {
                const float leftWeight = left.mBase.getClass().getWeight(left.mBase);
                const float rightWeight = right.mBase.getClass().getWeight(right.mBase);
                if (leftWeight != rightWeight)
                    return ordered(leftWeight, rightWeight);
            }
            if (mSortMode == MWGui::SortFilterItemModel::Sort_Value)
            {
                const int leftValue = left.mBase.getClass().getValue(left.mBase);
                const int rightValue = right.mBase.getClass().getValue(right.mBase);
                if (leftValue != rightValue)
                    return ordered(leftValue, rightValue);
            }

            if (mSortMode != MWGui::SortFilterItemModel::Sort_Default)
            {
                if (leftName != rightName)
                    return leftName < rightName;
                return left.mBase.getCellRef().getRefId() < right.mBase.getCellRef().getRefId();
            }

            float result = 0;

            // compare items by type
            leftName = left.mBase.getTypeName();
            rightName = right.mBase.getTypeName();

            if (leftName != rightName)
                return compareType(leftName, rightName);

            // compare items by name
            leftName = Misc::StringUtils::lowerCaseUtf8(left.mBase.getClass().getName(left.mBase));
            rightName = Misc::StringUtils::lowerCaseUtf8(right.mBase.getClass().getName(right.mBase));

            result = leftName.compare(rightName);
            if (result != 0)
                return result < 0;

            // compare items by enchantment:
            // 1. enchanted items showed before non-enchanted
            // 2. item with lesser charge percent comes after items with more charge percent
            // 3. item with constant effect comes before items with non-constant effects
            int leftChargePercent = -1;
            int rightChargePercent = -1;
            leftName = left.mBase.getClass().getEnchantment(left.mBase);
            rightName = right.mBase.getClass().getEnchantment(right.mBase);

            if (!leftName.empty())
            {
                const ESM::Enchantment* ench = MWBase::Environment::get().getWorld()->getStore().get<ESM::Enchantment>().search(leftName);
                if (ench)
                {
                    if (ench->mData.mType == ESM::Enchantment::ConstantEffect)
                        leftChargePercent = 101;
                    else
                        leftChargePercent = static_cast<int>(left.mBase.getCellRef().getNormalizedEnchantmentCharge(ench->mData.mCharge) * 100);
                }
            }

            if (!rightName.empty())
            {
                const ESM::Enchantment* ench = MWBase::Environment::get().getWorld()->getStore().get<ESM::Enchantment>().search(rightName);
                if (ench)
                {
                    if (ench->mData.mType == ESM::Enchantment::ConstantEffect)
                        rightChargePercent = 101;
                    else
                        rightChargePercent = static_cast<int>(right.mBase.getCellRef().getNormalizedEnchantmentCharge(ench->mData.mCharge) * 100);
                }
            }

            result = leftChargePercent - rightChargePercent;
            if (result != 0)
                return result > 0;

            // compare items by condition
            if (left.mBase.getClass().hasItemHealth(left.mBase) && right.mBase.getClass().hasItemHealth(right.mBase))
            {
                result = left.mBase.getClass().getItemHealth(left.mBase) - right.mBase.getClass().getItemHealth(right.mBase);
                if (result != 0)
                    return result > 0;
            }

            // compare items by remaining usage time
            result = left.mBase.getClass().getRemainingUsageTime(left.mBase) - right.mBase.getClass().getRemainingUsageTime(right.mBase);
            if (result != 0)
                return result > 0;

            // compare items by value
            result = left.mBase.getClass().getValue(left.mBase) - right.mBase.getClass().getValue(right.mBase);
            if (result != 0)
                return result > 0;

            // compare items by weight
            result = left.mBase.getClass().getWeight(left.mBase) - right.mBase.getClass().getWeight(right.mBase);
            if (result != 0)
                return result > 0;

            // compare items by Id
            leftName = left.mBase.getCellRef().getRefId();
            rightName = right.mBase.getCellRef().getRefId();

            result = leftName.compare(rightName);
            return result < 0;
        }
    };
}

namespace MWGui
{

    SortFilterItemModel::SortFilterItemModel(ItemModel *sourceModel)
        : mCategory(Category_All)
        , mFilter(0)
        , mSortByType(true)
        , mHideKeys(false)
        , mSortMode(Sort_Default)
        , mSortAscending(true)
        , mNameFilter("")
        , mEffectFilter("")
    {
        mSourceModel = sourceModel;
    }

    bool SortFilterItemModel::allowedToUseItems() const
    {
        return mSourceModel->allowedToUseItems();
    }

    void SortFilterItemModel::addDragItem (const MWWorld::Ptr& dragItem, size_t count)
    {
        mDragItems.emplace_back(dragItem, count);
    }

    void SortFilterItemModel::clearDragItems()
    {
        mDragItems.clear();
    }

    bool SortFilterItemModel::filterAccepts (const ItemStack& item)
    {
        MWWorld::Ptr base = item.mBase;

        if (mHideKeys && mCategory != Category_Keys && base.getClass().isKey(base))
            return false;

        int category = 0;
        if (base.getTypeName() == typeid(ESM::Armor).name()
                || base.getTypeName() == typeid(ESM::Clothing).name())
            category = Category_Apparel;
        else if (base.getTypeName() == typeid(ESM::Weapon).name())
            category = Category_Weapon;
        else if (base.getTypeName() == typeid(ESM::Ingredient).name()
                     || base.getTypeName() == typeid(ESM::Potion).name())
            category = Category_Magic;
        else if (base.getTypeName() == typeid(ESM::Miscellaneous).name()
                 || base.getTypeName() == typeid(ESM::Ingredient).name()
                 || base.getTypeName() == typeid(ESM::Repair).name()
                 || base.getTypeName() == typeid(ESM::Lockpick).name()
                 || base.getTypeName() == typeid(ESM::Light).name()
                 || base.getTypeName() == typeid(ESM::Apparatus).name()
                 || base.getTypeName() == typeid(ESM::Book).name()
                 || base.getTypeName() == typeid(ESM::Probe).name())
            category = Category_Misc;

        if (base.getClass().isKey(base))
            category |= Category_Keys;

        if (item.mFlags & ItemStack::Flag_Enchanted)
            category |= Category_Magic;

        if (!(category & mCategory))
            return false;

        if (mFilter & Filter_OnlyIngredients)
        {
            if (base.getTypeName() != typeid(ESM::Ingredient).name())
                return false;

            if (!mNameFilter.empty() && !mEffectFilter.empty())
                throw std::logic_error("name and magic effect filter are mutually exclusive");

            if (!mNameFilter.empty())
            {
                const auto itemName = Misc::StringUtils::lowerCaseUtf8(base.getClass().getName(base));
                return itemName.find(mNameFilter) != std::string::npos;
            }

            if (!mEffectFilter.empty())
            {
                MWWorld::Ptr player = MWBase::Environment::get().getWorld ()->getPlayerPtr();
                const auto alchemySkill = player.getClass().getSkill(player, ESM::Skill::Alchemy);

                const auto effects = MWMechanics::Alchemy::effectsDescription(base, alchemySkill);

                for (const auto& effect : effects)
                {
                    const auto ciEffect = Misc::StringUtils::lowerCaseUtf8(effect);

                    if (ciEffect.find(mEffectFilter) != std::string::npos)
                        return true;
                }
                return false;
            }
            return true;
        }

        if ((mFilter & Filter_OnlyEnchanted) && !(item.mFlags & ItemStack::Flag_Enchanted))
            return false;
        if ((mFilter & Filter_OnlyChargedSoulstones) && (base.getTypeName() != typeid(ESM::Miscellaneous).name()
                                                     || base.getCellRef().getSoul() == "" || !MWBase::Environment::get().getWorld()->getStore().get<ESM::Creature>().search(base.getCellRef().getSoul())))
            return false;
        if ((mFilter & Filter_OnlyRepairTools) && (base.getTypeName() != typeid(ESM::Repair).name()))
            return false;
        if ((mFilter & Filter_OnlyEnchantable) && (item.mFlags & ItemStack::Flag_Enchanted
                                               || (base.getTypeName() != typeid(ESM::Armor).name()
                                                   && base.getTypeName() != typeid(ESM::Clothing).name()
                                                   && base.getTypeName() != typeid(ESM::Weapon).name()
                                                   && base.getTypeName() != typeid(ESM::Book).name())))
            return false;
        if ((mFilter & Filter_OnlyEnchantable) && base.getTypeName() == typeid(ESM::Book).name()
                && !base.get<ESM::Book>()->mBase->mData.mIsScroll)
            return false;

        if ((mFilter & Filter_OnlyUsableItems) && base.getClass().getScript(base).empty())
        {
            std::shared_ptr<MWWorld::Action> actionOnUse = base.getClass().use(base);
            if (!actionOnUse || actionOnUse->isNullAction())
                return false;
        }

        if ((mFilter & Filter_OnlyRepairable) && (
                    !base.getClass().hasItemHealth(base)
                    || (base.getClass().getItemHealth(base) == base.getClass().getItemMaxHealth(base))
                    || (base.getTypeName() != typeid(ESM::Weapon).name()
                        && base.getTypeName() != typeid(ESM::Armor).name())))
            return false;

        if (mFilter & Filter_OnlyRechargable)
        {
            if (!(item.mFlags & ItemStack::Flag_Enchanted))
                return false;

            std::string enchId = base.getClass().getEnchantment(base);
            const ESM::Enchantment* ench = MWBase::Environment::get().getWorld()->getStore().get<ESM::Enchantment>().search(enchId);
            if (!ench)
            {
                Log(Debug::Warning) << "Warning: Can't find enchantment '" << enchId << "' on item " << base.getCellRef().getRefId();
                return false;
            }

            if (base.getCellRef().getEnchantmentCharge() >= ench->mData.mCharge
                    || base.getCellRef().getEnchantmentCharge() == -1)
                return false;
        }

        std::string compare = Misc::StringUtils::lowerCaseUtf8(item.mBase.getClass().getName(item.mBase));
        if(compare.find(mNameFilter) == std::string::npos)
            return false;

        return true;
    }

    ItemStack SortFilterItemModel::getItem (ModelIndex index)
    {
        if (index < 0)
            throw std::runtime_error("Invalid index supplied");
        if (mItems.size() <= static_cast<size_t>(index))
            throw std::runtime_error("Item index out of range");
        return mItems[index];
    }

    size_t SortFilterItemModel::getItemCount()
    {
        return mItems.size();
    }

    void SortFilterItemModel::setCategory (int category)
    {
        mCategory = category;
    }

    void SortFilterItemModel::setFilter (int filter)
    {
        mFilter = filter;
    }

    void SortFilterItemModel::setNameFilter (const std::string& filter)
    {
        mNameFilter = Misc::StringUtils::lowerCaseUtf8(filter);
    }

    void SortFilterItemModel::setEffectFilter (const std::string& filter)
    {
        mEffectFilter = Misc::StringUtils::lowerCaseUtf8(filter);
    }

    void SortFilterItemModel::update()
    {
        mSourceModel->update();

        const size_t count = mSourceModel->getItemCount();

        mItems.clear();

        // When ArenaMW hides raw keys from the ordinary player inventory, keep
        // one synthetic row in their place. It uses the first key's actual NIF
        // inventory icon, but represents every key in the InventoryStore. The
        // null creator is an intentional sentinel used only by InventoryWindow
        // and ItemView to recognise that this row must never be moved/sold.
        ItemStack keyRing;
        bool haveKeyRing = false;
        size_t keyRingCount = 0;

        for (size_t i = 0; i < count; ++i)
        {
            ItemStack item = mSourceModel->getItem(i);

            for (std::vector<std::pair<MWWorld::Ptr, size_t> >::iterator it = mDragItems.begin(); it != mDragItems.end(); ++it)
            {
                if (item.mBase == it->first)
                {
                    if (item.mCount < it->second)
                        throw std::runtime_error("Dragging more than present in the model");
                    item.mCount -= it->second;
                }
            }

            if (item.mCount == 0)
                continue;

            if (mHideKeys && mCategory != Category_Keys && item.mBase.getClass().isKey(item.mBase))
            {
                if (!haveKeyRing)
                {
                    keyRing = item;
                    keyRing.mCreator = nullptr;
                    keyRing.mType = ItemStack::Type_Normal;
                    haveKeyRing = true;
                }
                keyRingCount += item.mCount;
                continue;
            }

            if (filterAccepts(item))
                mItems.push_back(item);
        }

        Compare cmp;
        cmp.mSortByType = mSortByType;
        cmp.mSortMode = mSortMode;
        cmp.mAscending = mSortAscending;
        std::sort(mItems.begin(), mItems.end(), cmp);

        if (haveKeyRing && keyRingCount > 0)
        {
            // The key ring behaves as a miscellaneous inventory item. It is
            // hidden by weapon/apparel/magic tabs and by specialised picker
            // filters. Search also treats its display name as a normal item.
            bool show = ((mCategory & Category_Misc) != 0 || (mCategory & Category_All) == Category_All) && mCategory != Category_Keys && mFilter == 0 && mEffectFilter.empty();
            if (show && !mNameFilter.empty())
            {
                const std::string ringName = Misc::StringUtils::lowerCaseUtf8(
                    MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=keyring.title}"));
                show = ringName.find(mNameFilter) != std::string::npos;
            }

            if (show)
            {
                keyRing.mCount = keyRingCount;
                // Keep this utility item easy to find regardless of the first
                // key's record id: place it at the top of the filtered result.
                mItems.insert(mItems.begin(), keyRing);
            }
        }
    }

    void SortFilterItemModel::setSortMode(SortMode mode, bool ascending)
    {
        mSortMode = mode;
        mSortAscending = ascending;
    }

    void SortFilterItemModel::toggleSortMode(SortMode mode)
    {
        if (mSortMode == mode)
            mSortAscending = !mSortAscending;
        else
        {
            mSortMode = mode;
            mSortAscending = (mode == Sort_Name);
        }
    }

    void SortFilterItemModel::onClose()
    {
        mSourceModel->onClose();
    }

    bool SortFilterItemModel::onDropItem(const MWWorld::Ptr &item, int count)
    {
        return mSourceModel->onDropItem(item, count);
    }

    bool SortFilterItemModel::onTakeItem(const MWWorld::Ptr &item, int count)
    {
        return mSourceModel->onTakeItem(item, count);
    }
}
