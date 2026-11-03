#include "playeranimationmenu.hpp"

#include <algorithm>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_TextBox.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/animation.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/Main.hpp"

#include "mode.hpp"

namespace
{
    std::string arenaText(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    std::string categoryLabel(const std::string& category)
    {
        if (category == "Idle") return arenaText("animation.group.idle");
        if (category == "Gestures") return arenaText("animation.group.gestures");
        if (category == "Poses") return arenaText("animation.group.poses");
        if (category == "Sitting") return arenaText("animation.group.sitting");
        if (category == "Social") return arenaText("animation.group.social");
        return category;
    }

    struct Candidate
    {
        const char* mCategory;
        const char* mCaption;
        const char* mGroup;
        float mSpeed;
        bool mOneShot;
        float mDuration;
        int mLoops;
        int mProp;
        bool mThirdPersonOnly;
    };

    const char* const sCategories[] = {
        "Idle",
        "Gestures",
        "Poses",
        "Sitting",
        "Social"
    };

    // The menu intentionally exposes only player-selected full-body poses and
    // gestures. Dialogue and contextual interaction animations remain engine-
    // controlled and are not shown here.
    const Candidate sAnimations[] = {
        { "Idle", "animation.name.idle_1", "idle", 1.00f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_2", "idle2", 1.00f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_3", "idle3", 1.00f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_4", "idle4", 1.00f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_5", "idle5", 1.00f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_6", "idle6", 1.00f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_7", "idle7", 1.00f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_8", "idle8", 1.00f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_9", "idle9", 1.00f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_2_alt", "idle2_copy", 0.90f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_3_alt", "idle3_copy", 0.90f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_6_alt", "idle6_copy", 0.90f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_7_alt", "idle7_copy", 0.90f, false, 0.f, 1, 0, false },
        { "Idle", "animation.name.idle_8_alt", "idle8_copy", 0.90f, false, 0.f, 1, 0, false },

        { "Gestures", "animation.name.arms_akimbo", "armsakimbo", 0.68f, false, 0.f, 1, 0, false },
        { "Gestures", "animation.name.arms_folded", "armsfolded", 0.68f, false, 0.f, 1, 0, false },
        { "Gestures", "animation.name.hands_behind_back", "armsatback", 0.68f, false, 0.f, 1, 0, false },
        { "Gestures", "animation.name.sun_shield", "armssunshield", 0.72f, false, 0.f, 1, 0, false },
        { "Gestures", "animation.name.almalexia_prayer", "armsalmapray", 0.74f, false, 0.f, 1, 0, false },
        { "Gestures", "animation.name.greeting", "armsgesture_greet", 0.90f, false, 0.f, 1, 0, false },
        { "Gestures", "animation.name.gesture", "armsgesture", 0.90f, false, 0.f, 1, 0, false },
        { "Gestures", "animation.name.pet", "petit", 1.00f, true, 2.00f, 1, 0, false },
        { "Gestures", "animation.name.follow_me", "followme", 1.00f, true, 1.0667f, 1, 0, false },
        { "Gestures", "animation.name.wait_here", "wait", 1.00f, true, 1.0667f, 1, 0, false },
        { "Gestures", "animation.name.prayer_upper", "prayer1", 0.80f, true, 2.50f, 1, 0, false },
        { "Gestures", "animation.name.prayer_full", "prayer2", 0.80f, true, 2.5417f, 1, 0, false },

        { "Poses", "animation.name.hand_on_hip", "handhippose", 0.62f, false, 0.f, 1, 0, false },
        { "Poses", "animation.name.ready_pose", "readypose", 0.68f, false, 0.f, 1, 0, false },
        { "Poses", "animation.name.formal_pose", "posealma3", 0.82f, false, 0.f, 1, 0, false },
        { "Poses", "animation.name.wall_lean", "posewalllean", 0.78f, false, 0.f, 1, 0, false },
        { "Poses", "animation.name.wall_lean_reverse", "posewalllean180", 0.78f, false, 0.f, 1, 0, false },
        { "Poses", "animation.name.squat", "gvsquat", 0.82f, false, 0.f, 1, 0, false },

        { "Sitting", "animation.name.sit_2", "vasitting2", 0.90f, false, 0.f, 1, 0, false },
        { "Sitting", "animation.name.sit_3", "vasitting3", 0.90f, false, 0.f, 1, 0, false },
        { "Sitting", "animation.name.sit_4", "vasitting4", 0.90f, false, 0.f, 1, 0, false },
        { "Sitting", "animation.name.sit_5", "vasitting5", 0.90f, false, 0.f, 1, 0, false },
        { "Sitting", "animation.name.sit_6", "vasitting6", 0.90f, false, 0.f, 1, 0, false },
        { "Sitting", "animation.name.sit_7", "vasitting7", 0.90f, false, 0.f, 1, 0, false },
        { "Sitting", "animation.name.sit_8", "vasitting8", 0.90f, false, 0.f, 1, 0, false },
        { "Sitting", "animation.name.sit_9", "vasitting9", 0.90f, false, 0.f, 1, 0, false },
        { "Sitting", "animation.name.sit_floor", "vasittingfloor", 0.90f, false, 0.f, 1, 0, false },

        // Sit Down Please 3.5.1. Player-selected animations keep using
        // LocalPlayer's existing network animation packets, so other clients
        // see the same Z-menu pose/one-shot without any local-only fallback.
        { "Sitting", "animation.name.sdp_sit_arms_knees", "sdparmsonkneessitidle1", 1.00f, false, 0.f, 1, 0, true },
        { "Sitting", "animation.name.sdp_sit_2", "sdpvasitting2", 1.00f, false, 0.f, 1, 0, true },
        { "Sitting", "animation.name.sdp_sit_3", "sdpvasitting3", 1.00f, false, 0.f, 1, 0, true },
        { "Sitting", "animation.name.sdp_sit_4", "sdpvasitting4", 1.00f, false, 0.f, 1, 0, true },
        { "Sitting", "animation.name.sdp_sit_6", "sdpvasitting6", 1.00f, false, 0.f, 1, 0, true },
        { "Poses", "animation.name.sdp_sleep_side", "sdpvasitting8", 1.00f, false, 0.f, 1, 0, true },
        { "Poses", "animation.name.sdp_lie_back", "sdpvasitting9", 1.00f, false, 0.f, 1, 0, true },

        { "Social", "animation.name.sdp_attentive", "sdppreachattentive", 1.00f, false, 0.f, 1, 0, false },
        { "Social", "animation.name.sdp_admonish", "sdppreachadmonish", 1.00f, true, 2.40f, 1, 0, false },
        { "Social", "animation.name.sdp_formal_1", "sdppreachformal01", 1.00f, false, 0.f, 1, 0, false },
        { "Social", "animation.name.sdp_formal_2", "sdppreachformal02", 1.00f, false, 0.f, 1, 0, false },
        { "Social", "animation.name.sdp_beckon", "sdppreachbeckon", 1.00f, true, 2.20f, 1, 0, false },
        { "Social", "animation.name.sdp_hold", "sdppreachhold", 1.00f, true, 2.20f, 1, 0, false },
        { "Social", "animation.name.sdp_scan", "sdppreachscan", 1.00f, true, 2.20f, 1, 0, false },
        { "Social", "animation.name.sdp_command_1", "sdppreachcommand01", 1.00f, true, 2.20f, 1, 0, false },
        { "Social", "animation.name.sdp_command_2", "sdppreachcommand02", 1.00f, true, 2.20f, 1, 0, false },
        { "Social", "animation.name.sdp_command_3", "sdppreachcommand03", 1.00f, true, 2.20f, 1, 0, false },
        { "Social", "animation.name.sdp_command_4", "sdppreachcommand04", 1.00f, true, 2.20f, 1, 0, false }
    };
}

namespace MWGui
{
    PlayerAnimationMenu::PlayerAnimationMenu()
        : WindowBase("openmw_player_animation_menu.layout")
        , mTitle(nullptr)
        , mList(nullptr)
        , mStage(Stage::Group)
    {
        getWidget(mTitle, "Title");
        getWidget(mList, "AnimationList");

        mList->setScrollVisible(true);
        // Mouse activation must be immediate: a single full click opens a group
        // or starts the selected animation. Keyboard confirmation remains bound
        // to Enter without relying on ListBox's double-click accept event.
        mList->setActivateOnClick(true);
        mList->eventListMouseItemActivate += MyGUI::newDelegate(this, &PlayerAnimationMenu::onAccept);
        mList->eventKeyButtonPressed += MyGUI::newDelegate(this, &PlayerAnimationMenu::onKeyButtonPressed);
        positionWindow();
    }

    MyGUI::Widget* PlayerAnimationMenu::getDefaultKeyFocus()
    {
        return mList;
    }

    void PlayerAnimationMenu::positionWindow()
    {
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        MyGUI::IntCoord coord = mMainWidget->getCoord();
        coord.left = std::max(14, static_cast<int>(view.width * 0.035f));
        coord.top = std::max(14, (view.height - coord.height) / 2);
        mMainWidget->setCoord(coord);
    }

    void PlayerAnimationMenu::onResChange(int, int)
    {
        positionWindow();
    }

    void PlayerAnimationMenu::onOpen()
    {
        showGroups();
        positionWindow();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mList);
    }

    void PlayerAnimationMenu::showGroups()
    {
        mStage = Stage::Group;
        mEntries.clear();
        mGroups.clear();
        mList->removeAllItems();
        mList->setScrollVisible(false);
        mTitle->setCaption(arenaText("animation.title.group"));

        const std::string cancel = MWBase::Environment::get().getWindowManager()
            ->getGameSettingString("sCancel", "Cancel");
        mList->addItem(cancel);
        for (const char* category : sCategories)
        {
            mGroups.emplace_back(category);
            mList->addItem(categoryLabel(category));
        }
        mList->setIndexSelected(0);
        mList->beginToItemSelected();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mList);
    }

    void PlayerAnimationMenu::showAnimations(const std::string& group)
    {
        mStage = Stage::Animation;
        mEntries.clear();
        mList->removeAllItems();
        mList->setScrollVisible(true);
        mTitle->setCaption(categoryLabel(group));

        const std::string cancel = MWBase::Environment::get().getWindowManager()
            ->getGameSettingString("sCancel", "Cancel");
        const std::string back = MWBase::Environment::get().getWindowManager()
            ->getGameSettingString("sBack", "Back");
        mList->addItem(cancel);
        mList->addItem(back);

        for (const Candidate& candidate : sAnimations)
        {
            if (group != candidate.mCategory)
                continue;
            mEntries.push_back({ candidate.mCaption, candidate.mGroup, candidate.mSpeed,
                candidate.mOneShot, candidate.mDuration, candidate.mLoops,
                candidate.mProp, candidate.mThirdPersonOnly, false });
            mList->addItem(arenaText(candidate.mCaption));
        }

        mList->setIndexSelected(0);
        mList->beginToItemSelected();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mList);
    }

    void PlayerAnimationMenu::selectRelative(int direction)
    {
        const size_t count = mList->getItemCount();
        if (count == 0 || direction == 0)
            return;

        size_t selected = mList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= count)
            selected = 0;

        if (direction > 0)
            selected = (selected + count - 1) % count;
        else
            selected = (selected + 1) % count;

        mList->setIndexSelected(selected);
        mList->beginToItemAt(selected);
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mList);
    }

    bool PlayerAnimationMenu::handleMouseWheel(int rel)
    {
        if (!isVisible() || rel == 0)
            return false;

        // Match QuickLoot: every SDL wheel event advances exactly one row and
        // wraps cyclically at both ends.
        selectRelative(rel);
        return true;
    }

    void PlayerAnimationMenu::closeMenu()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_PlayerAnimationMenu);
    }

    void PlayerAnimationMenu::onKeyButtonPressed(MyGUI::Widget*, MyGUI::KeyCode key, MyGUI::Char)
    {
        if (key != MyGUI::KeyCode::Return && key != MyGUI::KeyCode::NumpadEnter)
            return;

        const size_t index = mList->getIndexSelected();
        if (index != MyGUI::ITEM_NONE)
            onAccept(mList, index);
    }

    void PlayerAnimationMenu::onAccept(MyGUI::ListBox*, size_t index)
    {
        mwmp::LocalPlayer* player = mwmp::Main::get().getLocalPlayer();
        if (!player)
        {
            closeMenu();
            return;
        }

        if (mStage == Stage::Group)
        {
            if (index == 0)
            {
                player->clearPersistentAnimation();
                player->cancelInteractionAnimation(true);
                closeMenu();
                return;
            }

            const size_t groupIndex = index - 1;
            if (groupIndex < mGroups.size())
                showAnimations(mGroups[groupIndex]);
            return;
        }

        if (index == 0)
        {
            player->clearPersistentAnimation();
            player->cancelInteractionAnimation(true);
            closeMenu();
            return;
        }
        if (index == 1)
        {
            showGroups();
            return;
        }

        const size_t entryIndex = index - 2;
        if (entryIndex >= mEntries.size())
            return;

        const AnimationEntry& entry = mEntries[entryIndex];
        if (entry.mWalkStyle)
        {
            player->setWalkAnimationStyle(entry.mGroup);
            closeMenu();
            return;
        }

        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (entry.mThirdPersonOnly && world->isFirstPerson())
            world->togglePOV(true);

        // User-selected menu animations always occupy the full Body layer.
        const int blendMask = MWRender::Animation::BlendMask_All;
        if (entry.mOneShot)
            player->playInteractionAnimation(entry.mGroup, blendMask, entry.mSpeed,
                entry.mLoops, entry.mDuration, entry.mProp);
        else
        {
            if (world->isFirstPerson())
                world->togglePOV(true);
            player->setPersistentAnimation(entry.mGroup, blendMask, entry.mSpeed);
        }
        closeMenu();
    }
}
