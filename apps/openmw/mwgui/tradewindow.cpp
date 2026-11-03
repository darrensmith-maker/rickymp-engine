#include "tradewindow.hpp"

#include <algorithm>
#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ControllerManager.h>
#include <MyGUI_ControllerRepeatClick.h>
#include <MyGUI_ImageBox.h>

#include <components/widgets/numericeditbox.hpp>
#include <components/settings/settings.hpp>
#include <components/esm/loadmisc.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/dialoguemanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/interactionanimation.hpp"

#include "../mwrender/animation.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"

#include "inventorywindow.hpp"
#include "draganddrop.hpp"
#include "itemview.hpp"
#include "sortfilteritemmodel.hpp"
#include "containeritemmodel.hpp"
#include "tradeitemmodel.hpp"
#include "countdialog.hpp"
#include "tooltips.hpp"

namespace
{

    int getEffectiveValue (MWWorld::Ptr item, int count)
    {
        float price = static_cast<float>(item.getClass().getValue(item));
        if (item.getClass().hasItemHealth(item))
        {
            price *= item.getClass().getItemNormalizedHealth(item);
        }
        return static_cast<int>(price * count);
    }

}

namespace MWGui
{
    TradeWindow::TradeWindow(DragAndDrop* dragAndDrop)
        : WindowBase("openmw_trade_window.layout")
        , mSortModel(nullptr)
        , mTradeModel(nullptr)
        , mDragAndDrop(dragAndDrop)
        , mItemToSell(-1)
        , mCurrentBalance(0)
        , mCurrentMerchantOffer(0)
        , mViewModeButton(nullptr)
        , mViewModeIcon(nullptr)
        , mFilterKeys(nullptr)
        , mMerchantGoldIcon(nullptr)
    {
        getWidget(mFilterAll, "AllButton");
        getWidget(mFilterWeapon, "WeaponButton");
        getWidget(mFilterApparel, "ApparelButton");
        getWidget(mFilterMagic, "MagicButton");
        getWidget(mFilterMisc, "MiscButton");
        getWidget(mFilterKeys, "KeysButton");

        getWidget(mMaxSaleButton, "MaxSaleButton");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mOfferButton, "OfferButton");
        getWidget(mMerchantGoldIcon, "MerchantGoldIcon");
        getWidget(mMerchantGold, "MerchantGold");
        mMerchantGoldIcon->setNeedMouseFocus(false);
        mMerchantGold->setNeedMouseFocus(false);
        mMerchantGold->setTextAlign(MyGUI::Align::Right | MyGUI::Align::Bottom);
        mMerchantGold->setTextShadow(true);
        mMerchantGold->setTextShadowColour(MyGUI::Colour::Black);
        getWidget(mIncreaseButton, "IncreaseButton");
        getWidget(mDecreaseButton, "DecreaseButton");
        getWidget(mTotalBalance, "TotalBalance");
        getWidget(mTotalBalanceLabel, "TotalBalanceLabel");
        getWidget(mBottomPane, "BottomPane");
        getWidget(mFilterEdit, "FilterEdit");
        getWidget(mViewModeButton, "ViewModeButton");

        getWidget(mItemView, "ItemView");
        mItemView->setExtendedMode(true);
        mItemView->setSingleClickActionEnabled(true);
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &TradeWindow::onItemSelected);
        mItemView->eventItemDragStarted += MyGUI::newDelegate(this, &TradeWindow::onItemDragStarted);
        mItemView->eventItemDoubleClicked += MyGUI::newDelegate(this, &TradeWindow::onItemDoubleClicked);
        mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &TradeWindow::onBackgroundSelected);

        mViewModeIcon = mViewModeButton->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(5, 4, 18, 18), MyGUI::Align::Center, "ArenaTradeViewToggleIcon");
        mViewModeIcon->setNeedMouseFocus(false);
        mViewModeIcon->setColour(MyGUI::Colour(0.93f, 0.82f, 0.58f));

        mFilterAll->setStateSelected(true);
        mItemView->setInternalViewModeButtonVisible(false);
        mItemView->setExtendedMode(true);
        if (mViewModeIcon)
            mViewModeIcon->setImageTexture("icons/inventoryextender/Base/view_grid.dds");

        mFilterAll->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterWeapon->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterApparel->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterMagic->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterMisc->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterKeys->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &TradeWindow::onNameFilterChanged);
        mViewModeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onViewModeClicked);

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onCancelButtonClicked);
        mOfferButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onOfferButtonClicked);
        mMaxSaleButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onMaxSaleButtonClicked);
        mIncreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &TradeWindow::onIncreaseButtonPressed);
        mIncreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &TradeWindow::onBalanceButtonReleased);
        mDecreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &TradeWindow::onDecreaseButtonPressed);
        mDecreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &TradeWindow::onBalanceButtonReleased);

        mTotalBalance->eventValueChanged += MyGUI::newDelegate(this, &TradeWindow::onBalanceValueChanged);
        mTotalBalance->eventEditSelectAccept += MyGUI::newDelegate(this, &TradeWindow::onAccept);
        mTotalBalance->setMinValue(std::numeric_limits<int>::min()+1); // disallow INT_MIN since abs(INT_MIN) is undefined

        // Keep the merchant panel a little roomier than the old layout so the
        // ItemView scrollbar and both bottom action rows remain fully inside
        // the outer frame at UI scaling factors above 1.0.
        setCoord(320, 20, 700, 460);
    }

    void TradeWindow::setPtr(const MWWorld::Ptr& actor)
    {
        mPtr = actor;

        mCurrentBalance = 0;
        mCurrentMerchantOffer = 0;

        std::vector<MWWorld::Ptr> itemSources;
        // Important: actor goes first, so purchased items come out of the actor's pocket first
        itemSources.push_back(actor);
        MWBase::Environment::get().getWorld()->getContainersOwnedBy(actor, itemSources);

        std::vector<MWWorld::Ptr> worldItems;
        MWBase::Environment::get().getWorld()->getItemsOwnedBy(actor, worldItems);

        mTradeModel = new TradeItemModel(new ContainerItemModel(itemSources, worldItems), mPtr);
        mSortModel = new SortFilterItemModel(mTradeModel);
        mItemView->setModel (mSortModel);
        mItemView->resetScrollBars();

        updateLabels();

        setTitle(actor.getClass().getName(actor));

        onFilterChanged(mFilterAll);
        mFilterEdit->setCaption("");
    }

    void TradeWindow::onFrame(float dt)
    {
        (void)dt;
        checkReferenceAvailable();
        // NPC and player barter panes deliberately keep independent geometry.
        // This pane persists under "barter"; player inventory uses "inventory barter".
    }

    void TradeWindow::onNameFilterChanged(MyGUI::EditBox* _sender)
    {
        mSortModel->setNameFilter(_sender->getCaption());
        mItemView->update();
    }

    void TradeWindow::onFilterChanged(MyGUI::Widget* _sender)
    {
        if (_sender == mFilterAll)
            mSortModel->setCategory(SortFilterItemModel::Category_All);
        else if (_sender == mFilterWeapon)
            mSortModel->setCategory(SortFilterItemModel::Category_Weapon);
        else if (_sender == mFilterApparel)
            mSortModel->setCategory(SortFilterItemModel::Category_Apparel);
        else if (_sender == mFilterMagic)
            mSortModel->setCategory(SortFilterItemModel::Category_Magic);
        else if (_sender == mFilterMisc)
            mSortModel->setCategory(SortFilterItemModel::Category_Misc);
        else if (_sender == mFilterKeys)
            mSortModel->setCategory(SortFilterItemModel::Category_Keys);

        mFilterAll->setStateSelected(false);
        mFilterWeapon->setStateSelected(false);
        mFilterApparel->setStateSelected(false);
        mFilterMagic->setStateSelected(false);
        mFilterMisc->setStateSelected(false);
        mFilterKeys->setStateSelected(false);

        _sender->castType<MyGUI::Button>()->setStateSelected(true);

        mItemView->update();
    }

    int TradeWindow::getMerchantServices()
    {
        return mPtr.getClass().getServices(mPtr);
    }

    bool TradeWindow::exit()
    {
        mTradeModel->abort();
        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel()->abort();
        return true;
    }

    void TradeWindow::onItemSelected (int index)
    {
        if (!mSortModel || !mTradeModel || index < 0 || index >= static_cast<int>(mSortModel->getItemCount()))
            return;

        const ItemStack& item = mSortModel->getItem(index);
        int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;

        // ArenaMW barter uses modern one-click transfer. For stacked items use
        // Ctrl+click to move one, or drag-and-drop if you want a custom amount.
        mItemToSell = mSortModel->mapToSource(index);
        sellItem(nullptr, count);
    }

    void TradeWindow::onItemDragStarted(int index)
    {
        if (!mSortModel || !mTradeModel || mDragAndDrop->mIsOnDragAndDrop)
            return;

        const int sourceIndex = mSortModel->mapToSource(index);
        const ItemStack& item = mTradeModel->getItem(sourceIndex);
        const int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;
        mDragAndDrop->startBarterDrag(sourceIndex, mSortModel, mTradeModel, mItemView, count);
    }

    void TradeWindow::onItemDoubleClicked(int index)
    {
        // Barter rows already transfer on the first click. A second activation
        // would operate on the item that slid into the old row.
        (void)index;
    }

    void TradeWindow::onBackgroundSelected()
    {
        if (!mDragAndDrop->mIsOnDragAndDrop || !mDragAndDrop->isBarterDrag())
            return;

        if (mDragAndDrop->mSourceView == mItemView)
        {
            mDragAndDrop->finish();
            return;
        }

        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->completeBarterDragToMerchant(
            mDragAndDrop->mSourceIndex, mDragAndDrop->mDraggedCount);
        mDragAndDrop->finish();
    }

    void TradeWindow::completeBarterDragToPlayer(int sourceIndex, int count)
    {
        if (!mTradeModel || sourceIndex < 0 || sourceIndex >= static_cast<int>(mTradeModel->getItemCount()))
            return;
        mItemToSell = sourceIndex;
        const ItemStack& item = mTradeModel->getItem(sourceIndex);
        count = std::max(1, std::min(count, static_cast<int>(item.mCount)));
        sellItem(nullptr, count);
    }

    void TradeWindow::sellItem(MyGUI::Widget* sender, int count)
    {
        const ItemStack& item = mTradeModel->getItem(mItemToSell);
        std::string sound = item.mBase.getClass().getUpSoundId(item.mBase);
        MWBase::Environment::get().getWindowManager()->playSound(sound);

        TradeItemModel* playerTradeModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();

        if (item.mType == ItemStack::Type_Barter)
        {
            // this was an item borrowed to us by the player
            mTradeModel->returnItemBorrowedToUs(mItemToSell, count);
            playerTradeModel->returnItemBorrowedFromUs(mItemToSell, mTradeModel, count);
            buyFromNpc(item.mBase, count, true);
        }
        else
        {
            // borrow item to player
            playerTradeModel->borrowItemToUs(mItemToSell, mTradeModel, count);
            mTradeModel->borrowItemFromUs(mItemToSell, count);
            buyFromNpc(item.mBase, count, false);
        }

        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
        mItemView->update();
    }

    void TradeWindow::borrowItem (int index, size_t count)
    {
        TradeItemModel* playerTradeModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();
        mTradeModel->borrowItemToUs(index, playerTradeModel, count);
        mItemView->update();
        sellToNpc(playerTradeModel->getItem(index).mBase, count, false);
    }

    void TradeWindow::returnItem (int index, size_t count)
    {
        TradeItemModel* playerTradeModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();
        const ItemStack& item = playerTradeModel->getItem(index);
        mTradeModel->returnItemBorrowedFromUs(index, playerTradeModel, count);
        mItemView->update();
        sellToNpc(item.mBase, count, true);
    }

    void TradeWindow::addOrRemoveGold(int amount, const MWWorld::Ptr& actor)
    {
        MWWorld::ContainerStore& store = actor.getClass().getContainerStore(actor);

        if (amount > 0)
        {
            store.add(MWWorld::ContainerStore::sGoldId, amount, actor);
        }
        else
        {
            store.remove(MWWorld::ContainerStore::sGoldId, - amount, actor);
        }
    }

    void TradeWindow::onOfferButtonClicked(MyGUI::Widget* _sender)
    {
        TradeItemModel* playerItemModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();

        const MWWorld::Store<ESM::GameSetting> &gmst =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();

        // were there any items traded at all?
        const std::vector<ItemStack>& playerBought = playerItemModel->getItemsBorrowedToUs();
        const std::vector<ItemStack>& merchantBought = mTradeModel->getItemsBorrowedToUs();
        if (playerBought.empty() && merchantBought.empty())
        {
            // user notification
            MWBase::Environment::get().getWindowManager()->
                messageBox("#{sBarterDialog11}");
            return;
        }

        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);

        // check if the player can afford this
        if (mCurrentBalance < 0 && playerGold < std::abs(mCurrentBalance))
        {
            // user notification
            MWBase::Environment::get().getWindowManager()->
                messageBox("#{sBarterDialog1}");
            return;
        }

        // check if the merchant can afford this
        if (mCurrentBalance > 0 && getMerchantGold() < mCurrentBalance)
        {
            // user notification
            MWBase::Environment::get().getWindowManager()->
                messageBox("#{sBarterDialog2}");
            return;
        }

        // check if the player is attempting to sell back an item stolen from this actor
        for (const ItemStack& itemStack : merchantBought)
        {
            if (MWBase::Environment::get().getMechanicsManager()->isItemStolenFrom(itemStack.mBase.getCellRef().getRefId(), mPtr))
            {
                std::string msg = gmst.find("sNotifyMessage49")->mValue.getString();
                msg = Misc::StringUtils::format(msg, itemStack.mBase.getClass().getName(itemStack.mBase));
                MWBase::Environment::get().getWindowManager()->messageBox(msg);

                MWBase::Environment::get().getMechanicsManager()->confiscateStolenItemToOwner(player, itemStack.mBase, mPtr, itemStack.mCount);

                onCancelButtonClicked(mCancelButton);
                MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
                return;
            }
        }

        bool offerAccepted = mTrading.haggle(player, mPtr, mCurrentBalance, mCurrentMerchantOffer);

        // Apply disposition and play a voiced reaction if the merchant is an NPC.
        // Persuasion voice topics provide race/gender-specific positive and angry lines,
        // while DialogueManager handles subtitles and TES3MP actor-sound synchronization.
        if (mPtr.getClass().isNpc()) {
            int dispositionDelta = offerAccepted
                ? gmst.find("iBarterSuccessDisposition")->mValue.getInteger()
                : gmst.find("iBarterFailDisposition")->mValue.getInteger();

            MWBase::Environment::get().getDialogueManager()->applyBarterDispositionChange(dispositionDelta);
            MWBase::Environment::get().getDialogueManager()->say(mPtr,
                offerAccepted ? "Admire Success" : "Taunt Success");
        }

        // display message on haggle failure
        if (!offerAccepted) {
            MWBase::Environment::get().getWindowManager()->
                messageBox("#{sNotifyMessage9}");
            return;
        }

        // make the item transfer
        mTradeModel->transferItems();
        playerItemModel->transferItems();

        ///snapshot the gold you recieve, if any, for EncoreMP XP calculations further down
        int goldRecieved = 0;
        if (mCurrentBalance > 0)
        {
            goldRecieved = mCurrentBalance;
        }


        // transfer the gold
        if (mCurrentBalance != 0)
        {
            addOrRemoveGold(mCurrentBalance, player);

            /*
                Start of tes3mp change (major)

                Don't unilaterally change the merchant's gold pool on our client and instead let the server do it
            */
            //mPtr.getClass().getCreatureStats(mPtr).setGoldPool(
            //    mPtr.getClass().getCreatureStats(mPtr).getGoldPool() - mCurrentBalance);

            mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            MWMechanics::CreatureStats& merchantCreatureStats = mPtr.getClass().getCreatureStats(mPtr);
            objectList->addObjectMiscellaneous(mPtr, merchantCreatureStats.getGoldPool() - mCurrentBalance, merchantCreatureStats.getLastRestockTime().getHour(),
                merchantCreatureStats.getLastRestockTime().getDay());
            objectList->sendObjectMiscellaneous();
            /*
                End of tes3mp change (major)
            */
        }

        /// EncoreMP system for calcualting XP gained from sales

        int sumBaseSold = 0;
        int sumBasePurchased = 0;

        if (!merchantBought.empty())
        {
            ///sum the base gold value of all items you are selling
            for (const ItemStack& stack : merchantBought)
            {
                MWWorld::Ptr objectPtr = stack.mBase;
                int singleValue = objectPtr.getClass().getValue(objectPtr);

                for (int i = 0; i < stack.mCount; ++i)
                {
                    if (singleValue > 0)
                    {
                        sumBaseSold += singleValue;
                    }
                }
            }

            ///sum the base gold value of all items you are recieving
            if (!playerBought.empty())
            {
                for (const ItemStack& stack : playerBought)
                {
                    MWWorld::Ptr objectPtr = stack.mBase;
                    int singleValue = objectPtr.getClass().getValue(objectPtr);

                    for (int i = 0; i < stack.mCount; ++i)
                    {
                        if (singleValue > 0)
                        {
                            sumBasePurchased += singleValue;
                        }
                    }
                }

            }

        }

        MWWorld::Ptr playerPtr = MWBase::Environment::get().getWorld()->getPlayerPtr();

        /// calculate the barter value of everything you sold
        const int barterItemsSold = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(playerPtr, sumBaseSold, false);

        ///calculate the barter value of everything you purchased
        const int barterItemsPurchased = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(playerPtr, sumBasePurchased, true);

        ///sum the gold you recieved and the barter value of the items you recieved
        int totalValuePurchased = (barterItemsPurchased + goldRecieved);

        ///cap the total value of what you have gained to be no more than the barter value of what you sold
        totalValuePurchased = std::min(barterItemsSold, totalValuePurchased);

        ///calculate the ratio of what your items were worth in this transaction to what you got
        float xpMultiplier = 1.0f;
        if (barterItemsSold > 0)
        {
            xpMultiplier = ((float)totalValuePurchased / (float)barterItemsSold);
        }

        /// use the barter value of items sold to determine XP gain
        float xpAwardIncrements = 0.0f;
        if (barterItemsSold > 0)
        {
            xpAwardIncrements = (float(barterItemsSold) / 50.0f);
        }

        ///apply the multiplier for potentially underselling items
        xpAwardIncrements *= xpMultiplier;

        ///award experience if items were sold for a non zero value
        if (barterItemsSold > 0)
        {
            if (xpAwardIncrements > 0.0f)
            {
                player.getClass().skillUsageSucceeded(player, ESM::Skill::Mercantile, 0, xpAwardIncrements);
            }
        }


        /// end of EncoreMP xp gain system


        if (mCurrentBalance != 0
            && Settings::Manager::getBool("animated interactions", "GUI")
            && Settings::Manager::getBool("animated barter handoff", "GUI"))
        {
            if (mCurrentBalance < 0)
            {
                MWWorld::InteractionAnimation::playOneShot("give-to-player",
                    MWRender::Animation::BlendMask_UpperBody, 2.f, 0.8325f, 1,
                    MWWorld::InteractionAnimation::Prop_Gold);
            }
            else
            {
                MWWorld::InteractionAnimation::playOneShot("loot1",
                    MWRender::Animation::BlendMask_Torso
                        | MWRender::Animation::BlendMask_RightArm,
                    0.7f, 1.f);
            }
        }

        eventTradeDone();

        MWBase::Environment::get().getWindowManager()->playSound("Item Gold Up");
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
    }

    void TradeWindow::onAccept(MyGUI::EditBox *sender)
    {
        onOfferButtonClicked(sender);

        // To do not spam onAccept() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void TradeWindow::onCancelButtonClicked(MyGUI::Widget* _sender)
    {
        exit();
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
    }

    void TradeWindow::onMaxSaleButtonClicked(MyGUI::Widget* _sender)
    {
        mCurrentBalance = getMerchantGold();
        updateLabels();
    }

    void TradeWindow::addRepeatController(MyGUI::Widget *widget)
    {
        MyGUI::ControllerItem* item = MyGUI::ControllerManager::getInstance().createItem(MyGUI::ControllerRepeatClick::getClassTypeName());
        MyGUI::ControllerRepeatClick* controller = static_cast<MyGUI::ControllerRepeatClick*>(item);
        controller->eventRepeatClick += newDelegate(this, &TradeWindow::onRepeatClick);
        MyGUI::ControllerManager::getInstance().addItem(widget, controller);
    }

    void TradeWindow::onIncreaseButtonPressed(MyGUI::Widget* _sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        addRepeatController(_sender);
        onIncreaseButtonTriggered();
    }

    void TradeWindow::onDecreaseButtonPressed(MyGUI::Widget* _sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        addRepeatController(_sender);
        onDecreaseButtonTriggered();
    }

    void TradeWindow::onRepeatClick(MyGUI::Widget* widget, MyGUI::ControllerItem* controller)
    {
        if (widget == mIncreaseButton)
            onIncreaseButtonTriggered();
        else if (widget == mDecreaseButton)
            onDecreaseButtonTriggered();
    }

    void TradeWindow::onBalanceButtonReleased(MyGUI::Widget *_sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        MyGUI::ControllerManager::getInstance().removeItem(_sender);
    }

    void TradeWindow::onBalanceValueChanged(int value)
    {
        // Entering a "-" sign inverts the buying/selling state
        mCurrentBalance = (mCurrentBalance >= 0 ? 1 : -1) * value;
        updateLabels();

        if (value != std::abs(value))
            mTotalBalance->setValue(std::abs(value));
    }

    void TradeWindow::onIncreaseButtonTriggered()
    {
        // prevent overflows, and prevent entering INT_MIN since abs(INT_MIN) is undefined
        if (mCurrentBalance == std::numeric_limits<int>::max() || mCurrentBalance == std::numeric_limits<int>::min()+1)
            return;
        if (mCurrentBalance < 0) mCurrentBalance -= 1;
        else mCurrentBalance += 1;
        updateLabels();
    }

    void TradeWindow::onDecreaseButtonTriggered()
    {
        if (mCurrentBalance < 0) mCurrentBalance += 1;
        else mCurrentBalance -= 1;
        updateLabels();
    }

    void TradeWindow::updateLabels()
    {
        mTotalBalanceLabel->setCaption("");
        mTotalBalanceLabel->setVisible(false);

        mTotalBalance->setValue(std::abs(mCurrentBalance));

        mMerchantGold->setCaption(MyGUI::utility::toString(getMerchantGold()));
        if (mMerchantGoldIcon)
        {
            const std::string icon = resolveGoldIcon(mPtr);
            if (!icon.empty())
            {
                mMerchantGoldIcon->setVisible(true);
                mMerchantGoldIcon->setImageTexture(icon);
            }
            else
                mMerchantGoldIcon->setVisible(false);
        }
    }

    void TradeWindow::updateOffer()
    {
        TradeItemModel* playerTradeModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();

        int merchantOffer = 0;

        // The offered price must be capped at 75% of the base price to avoid exploits
        // connected to buying and selling the same item.
        // This value has been determined by researching the limitations of the vanilla formula
        // and may not be sufficient if getBarterOffer behavior has been changed

        // EncoreMP: I changed it, the above comments were from openMW, it wasn't sufficient when you considered haggling as well
        const std::vector<ItemStack>& playerBorrowed = playerTradeModel->getItemsBorrowedToUs();
        for (const ItemStack& itemStack : playerBorrowed)
        {
            const int basePrice = getEffectiveValue(itemStack.mBase, itemStack.mCount);
            const int cap = static_cast<int>(std::max(1.f, 0.90f * basePrice)); // Minimum buying price -- 90% of the base
            const int lowcap = static_cast<int>(1.50f * basePrice); // maximum buying price -- 150% of the base
            const int buyingPrice = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, basePrice, true);

            int localbuyprice = std::max(cap, buyingPrice);
            localbuyprice = std::min(lowcap, localbuyprice);

            merchantOffer -= localbuyprice;
        }

        const std::vector<ItemStack>& merchantBorrowed = mTradeModel->getItemsBorrowedToUs();
        for (const ItemStack& itemStack : merchantBorrowed)
        {
            const int basePrice = getEffectiveValue(itemStack.mBase, itemStack.mCount);
            const int cap = static_cast<int>(std::max(1.f, 0.60f * basePrice)); // Maximum selling price -- 60% of the base
            const int lowcap = static_cast<int>(0.20f * basePrice); // minimum selling price -- 20% of the base
            const int sellingPrice = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, basePrice, false);

            int localsellprice = std::max(lowcap, sellingPrice);
            localsellprice = std::min(cap, localsellprice);


            merchantOffer += mPtr.getClass().isNpc() ? localsellprice : sellingPrice;
        }

        int diff = merchantOffer - mCurrentMerchantOffer;
        mCurrentMerchantOffer = merchantOffer;
        mCurrentBalance += diff;
        updateLabels();
    }

    void TradeWindow::sellToNpc(const MWWorld::Ptr& item, int count, bool boughtItem)
    {
        updateOffer();
    }

    void TradeWindow::buyFromNpc(const MWWorld::Ptr& item, int count, bool soldItem)
    {
        updateOffer();
    }

    void TradeWindow::onViewModeClicked(MyGUI::Widget*)
    {
        if (!mItemView)
            return;

        ItemView::ViewMode mode = mItemView->getViewMode();
        mItemView->setViewMode(mode == ItemView::View_List ? ItemView::View_Grid : ItemView::View_List);
        if (mViewModeIcon)
            mViewModeIcon->setImageTexture(std::string("icons/inventoryextender/Base/")
                + (mItemView->getViewMode() == ItemView::View_List ? "view_grid.dds" : "view_table.dds"));
        mViewModeButton->setStateSelected(mItemView->getViewMode() == ItemView::View_List);
    }

    void TradeWindow::onReferenceUnavailable()
    {
        // remove both Trade and Dialogue (since you always trade with the NPC/creature that you have previously talked to)
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
        MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
    }

    std::string TradeWindow::resolveGoldIcon(const MWWorld::Ptr&) const
    {
        const ESM::Miscellaneous* gold = MWBase::Environment::get().getWorld()->getStore()
            .get<ESM::Miscellaneous>().search(MWWorld::ContainerStore::sGoldId);
        if (!gold || gold->mIcon.empty())
            return std::string();

        // Match ItemWidget's icon-path normalization so the merchant's coin
        // icon is the real inventory icon rather than a missing-texture tile.
        return MWBase::Environment::get().getWindowManager()->correctIconPath(gold->mIcon);
    }

    int TradeWindow::getMerchantGold()
    {
        int merchantGold = mPtr.getClass().getCreatureStats(mPtr).getGoldPool();
        return merchantGold;
    }

    void TradeWindow::resetReference()
    {
        ReferenceInterface::resetReference();
        mItemView->setModel(nullptr);
        mTradeModel = nullptr;
        mSortModel = nullptr;
    }

    void TradeWindow::onClose()
    {
        // Make sure the window was actually closed and not temporarily hidden.
        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Barter))
            return;
        resetReference();
    }
}
