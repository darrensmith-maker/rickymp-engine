#include "spellbuyingwindow.hpp"

#include <algorithm>

#include <MyGUI_Gui.h>
#include <MyGUI_Button.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ScrollView.h>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/ObjectList.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"

#include <components/esm/loadmgef.hpp>

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/actorutil.hpp"

namespace MWGui
{
    SpellBuyingWindow::SpellBuyingWindow() :
        WindowBase("openmw_spell_buying_window.layout")
        , mCurrentY(0)
    {
        getWidget(mCancelButton, "CancelButton");
        getWidget(mPlayerGold, "PlayerGold");
        getWidget(mSpellsView, "SpellsView");

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SpellBuyingWindow::onCancelButtonClicked);
    }

    bool SpellBuyingWindow::sortSpells (const ESM::Spell* left, const ESM::Spell* right)
    {
        std::string leftName = Misc::StringUtils::lowerCase(left->mName);
        std::string rightName = Misc::StringUtils::lowerCase(right->mName);

        return leftName.compare(rightName) < 0;
    }

    void SpellBuyingWindow::addSpell(const ESM::Spell& spell)
    {
        const MWWorld::ESMStore &store =
            MWBase::Environment::get().getWorld()->getStore();

        int price = std::max(1, static_cast<int>(spell.mData.mCost*store.get<ESM::GameSetting>().find("fSpellValueMult")->mValue.getFloat()));
        price = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr,price,true);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);

        // Native SpellTrader-style row: first-effect icon, spell name, price,
        // existing vanilla tooltip, and a highlight for effects the player has
        // not learned yet. No additional localisation strings are required.
        const int lineHeight = std::max(22,
            MWBase::Environment::get().getWindowManager()->getFontHeight() + 4);
        const int iconSize = 18;
        const int iconLeft = 2;
        const ESM::MagicEffect* firstEffect = nullptr;
        if (!spell.mEffects.mList.empty())
            firstEffect = store.get<ESM::MagicEffect>().search(spell.mEffects.mList.front().mEffectID);
        const bool hasIcon = firstEffect && !firstEffect->mIcon.empty();
        const int textLeft = hasIcon ? iconLeft + iconSize + 4 : 0;
        const int rowWidth = mSpellsView->getWidth();

        const bool affordable = price <= playerGold;
        const bool unknownEffect = hasUnknownEffect(spell);
        const char* rowSkin = !affordable ? "SandTextButtonDisabled"
            : (unknownEffect ? "SpellTraderUnknownButton" : "SandTextButton");

        MyGUI::Button* toAdd =
            mSpellsView->createWidget<MyGUI::Button>(
                rowSkin, // can't use setEnabled since that removes tooltip
                textLeft,
                mCurrentY,
                std::max(1, rowWidth - textLeft),
                lineHeight,
                MyGUI::Align::Default
            );

        if (hasIcon)
        {
            MyGUI::ImageBox* icon = mSpellsView->createWidget<MyGUI::ImageBox>(
                "ImageBox", iconLeft, mCurrentY + std::max(0, (lineHeight - iconSize) / 2),
                iconSize, iconSize, MyGUI::Align::Default);
            icon->setImageTexture(MWBase::Environment::get().getWindowManager()->correctIconPath(firstEffect->mIcon));
            icon->setUserData(price);
            icon->setUserString("ToolTipType", "Spell");
            icon->setUserString("Spell", spell.mId);
            icon->setUserString("SpellCost", std::to_string(spell.mData.mCost));
            icon->eventMouseWheel += MyGUI::newDelegate(this, &SpellBuyingWindow::onMouseWheel);
            icon->eventMouseButtonClick += MyGUI::newDelegate(this, &SpellBuyingWindow::onSpellButtonClick);
            if (!affordable)
                icon->setAlpha(0.45f);
            mSpellsWidgetMap.insert(std::make_pair(icon, spell.mId));
        }

        mCurrentY += lineHeight;

        toAdd->setUserData(price);
        toAdd->setCaptionWithReplacing(spell.mName+"   -   "+MyGUI::utility::toString(price)+"#{sgp}");
        toAdd->setSize(std::max(1, rowWidth - textLeft), lineHeight);
        toAdd->eventMouseWheel += MyGUI::newDelegate(this, &SpellBuyingWindow::onMouseWheel);
        toAdd->setUserString("ToolTipType", "Spell");
        toAdd->setUserString("Spell", spell.mId);
        toAdd->setUserString("SpellCost", std::to_string(spell.mData.mCost));
        toAdd->eventMouseButtonClick += MyGUI::newDelegate(this, &SpellBuyingWindow::onSpellButtonClick);
        mSpellsWidgetMap.insert(std::make_pair (toAdd, spell.mId));

    }

    void SpellBuyingWindow::rebuildKnownEffectIds()
    {
        mKnownEffectIds.clear();

        const MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::Spells& spells = player.getClass().getCreatureStats(player).getSpells();
        for (MWMechanics::Spells::TIterator it = spells.begin(); it != spells.end(); ++it)
        {
            const ESM::Spell* knownSpell = it->first;
            if (!knownSpell || knownSpell->mData.mType != ESM::Spell::ST_Spell)
                continue;

            for (const ESM::ENAMstruct& effect : knownSpell->mEffects.mList)
                mKnownEffectIds.insert(effect.mEffectID);
        }
    }

    bool SpellBuyingWindow::hasUnknownEffect(const ESM::Spell& spell) const
    {
        for (const ESM::ENAMstruct& effect : spell.mEffects.mList)
        {
            if (mKnownEffectIds.find(effect.mEffectID) == mKnownEffectIds.end())
                return true;
        }
        return false;
    }

    void SpellBuyingWindow::clearSpells()
    {
        mSpellsView->setViewOffset(MyGUI::IntPoint(0,0));
        mCurrentY = 0;
        while (mSpellsView->getChildCount())
            MyGUI::Gui::getInstance().destroyWidget(mSpellsView->getChildAt(0));
        mSpellsWidgetMap.clear();
    }

    void SpellBuyingWindow::setPtr(const MWWorld::Ptr &actor)
    {
        setPtr(actor, 0);
    }

    void SpellBuyingWindow::setPtr(const MWWorld::Ptr& actor, int startOffset)
    {
        center();
        mPtr = actor;
        clearSpells();
        rebuildKnownEffectIds();

        MWMechanics::Spells& merchantSpells = actor.getClass().getCreatureStats (actor).getSpells();

        std::vector<const ESM::Spell*> spellsToSort;

        std::string prefix = "@";

        for (MWMechanics::Spells::TIterator iter = merchantSpells.begin(); iter!=merchantSpells.end(); ++iter)
        {
            const ESM::Spell* spell = iter->first;

            if (spell->mData.mType!=ESM::Spell::ST_Spell)
                continue; // don't try to sell diseases, curses or powers

            if (actor.getClass().isNpc())
            {
                const ESM::Race* race =
                        MWBase::Environment::get().getWorld()->getStore().get<ESM::Race>().find(
                        actor.get<ESM::NPC>()->mBase->mRace);
                if (race->mPowers.exists(spell->mId))
                    continue;
            }

            if (playerHasSpell(iter->first->mId))
                continue;

            spellsToSort.push_back(iter->first);
        }

        // EncoreMP spell replacement trial

        //get all spell records in the game
        const auto& allSpells = MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>();

        //make an empty vector to hold spells to be added to the merchant spell list
        std::vector<const ESM::Spell*> encoreSpellsToAdd;
        std::vector<std::string> spellsToDelete;

        //ranged based loop over merchant spells for sale, no modifying of original vector within this loop
        for (const ESM::Spell* s : spellsToSort)
        {
            if (!s) continue;

            std::string spellID = s->mId;
            std::string encoreID = prefix + spellID;
            const ESM::Spell* spellToInsert = nullptr;
            

            spellToInsert = allSpells.search(encoreID);

            
            if (spellToInsert && !spellToInsert->mId.empty() && spellToInsert->mData.mType == ESM::Spell::ST_Spell)
            {
                if (playerHasSpell(spellToInsert->mId))
                {
                    spellsToDelete.push_back(spellID);
                }
                else
                {
                    encoreSpellsToAdd.push_back(spellToInsert);
                    spellsToDelete.push_back(spellID);
                }
            }
        }

        //add all new spells, stored in the vector, to the merchant spell list for sale
        for (const ESM::Spell* s : encoreSpellsToAdd) 
        {
            spellsToSort.push_back(s);
        }

        //delete all the original spells in the merchant for sale list for which new IDs were found
        spellsToSort.erase(std::remove_if(spellsToSort.begin(), spellsToSort.end(),[&](const ESM::Spell* elem) 
            {
                if (!elem) return false;
                return std::find(spellsToDelete.begin(), spellsToDelete.end(), elem->mId)
                != spellsToDelete.end();
            }
            ),
            spellsToSort.end()
        );

        // EncoreMP spell replacement trial end

        std::stable_sort(spellsToSort.begin(), spellsToSort.end(), sortSpells);

        for (const ESM::Spell* spell : spellsToSort)
        {
            addSpell(*spell);
        }

        spellsToSort.clear();

        updateLabels();

        // Canvas size must be expressed with VScroll disabled, otherwise MyGUI would expand the scroll area when the scrollbar is hidden
        mSpellsView->setVisibleVScroll(false);
        mSpellsView->setCanvasSize (MyGUI::IntSize(mSpellsView->getWidth(), std::max(mSpellsView->getHeight(), mCurrentY)));
        mSpellsView->setVisibleVScroll(true);
        mSpellsView->setViewOffset(MyGUI::IntPoint(0, startOffset));
    }

    bool SpellBuyingWindow::playerHasSpell(const std::string &id)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        return player.getClass().getCreatureStats(player).getSpells().hasSpell(id);
    }

    void SpellBuyingWindow::onSpellButtonClick(MyGUI::Widget* _sender)
    {
        int price = *_sender->getUserData<int>();

        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (price > player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId))
            return;

        MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        MWMechanics::Spells& spells = stats.getSpells();
        spells.add (mSpellsWidgetMap.find(_sender)->second);

        /*
            Start of tes3mp addition

            Send an ID_PLAYER_SPELLBOOK packet every time a player buys a spell
        */
        mwmp::Main::get().getLocalPlayer()->sendSpellChange(mSpellsWidgetMap.find(_sender)->second, mwmp::SpellbookChanges::ADD);
        /*
            End of tes3mp addition
        */

        player.getClass().getContainerStore(player).remove(MWWorld::ContainerStore::sGoldId, price, player);

        // add gold to NPC trading gold pool
        MWMechanics::CreatureStats& npcStats = mPtr.getClass().getCreatureStats(mPtr);

        /*
            Start of tes3mp change (major)

            Don't unilaterally change the merchant's gold pool on our client and instead let the server do it
        */
        //npcStats.setGoldPool(npcStats.getGoldPool() + price);

        mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->addObjectMiscellaneous(mPtr, npcStats.getGoldPool() + price, npcStats.getLastRestockTime().getHour(),
            npcStats.getLastRestockTime().getDay());
        objectList->sendObjectMiscellaneous();
        /*
            End of tes3mp change (major)
        */

        setPtr(mPtr, mSpellsView->getViewOffset().top);

        MWBase::Environment::get().getWindowManager()->playSound("Item Gold Up");
    }

    void SpellBuyingWindow::onCancelButtonClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode (MWGui::GM_SpellBuying);
    }

    void SpellBuyingWindow::updateLabels()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);

        mPlayerGold->setCaptionWithReplacing("#{sGold}: " + MyGUI::utility::toString(playerGold));
        mPlayerGold->setCoord(8,
                              mPlayerGold->getTop(),
                              mPlayerGold->getTextSize().width,
                              mPlayerGold->getHeight());
    }

    void SpellBuyingWindow::onReferenceUnavailable()
    {
        // remove both Spells and Dialogue (since you always trade with the NPC/creature that you have previously talked to)
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_SpellBuying);
        MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
    }

    void SpellBuyingWindow::onMouseWheel(MyGUI::Widget* _sender, int _rel)
    {
        if (mSpellsView->getViewOffset().top + _rel*0.3 > 0)
            mSpellsView->setViewOffset(MyGUI::IntPoint(0, 0));
        else
            mSpellsView->setViewOffset(MyGUI::IntPoint(0, static_cast<int>(mSpellsView->getViewOffset().top + _rel*0.3f)));
    }
}

