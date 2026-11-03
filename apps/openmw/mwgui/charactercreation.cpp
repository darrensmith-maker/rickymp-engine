#include "charactercreation.hpp"

#include <algorithm>

#include <components/debug/debuglog.hpp>
#include <components/fallback/fallback.hpp>
#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/player.hpp"

/*
    Start of tes3mp addition

    Account login remains ArenaMP-owned while the visual character creator is
    shared with ArenaMW.
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/GUI/GUILogin.hpp"
/* End of tes3mp addition */

#include "race.hpp"
#include "class.hpp"
#include "birth.hpp"
#include "review.hpp"
#include "inventorywindow.hpp"

namespace
{
    struct Response
    {
        const std::string mText;
        const ESM::Class::Specialization mSpecialization;
    };

    struct Step
    {
        const std::string mText;
        const Response mResponses[3];
        const std::string mSound;
    };

    Step sGenerateClassSteps(int number)
    {
        number++;

        std::string question = Fallback::Map::getString("Question_" + MyGUI::utility::toString(number) + "_Question");
        std::string answer0 = Fallback::Map::getString("Question_" + MyGUI::utility::toString(number) + "_AnswerOne");
        std::string answer1 = Fallback::Map::getString("Question_" + MyGUI::utility::toString(number) + "_AnswerTwo");
        std::string answer2 = Fallback::Map::getString("Question_" + MyGUI::utility::toString(number) + "_AnswerThree");
        std::string sound = "vo\\misc\\chargen qa" + MyGUI::utility::toString(number) + ".wav";

        Response r0 = {answer0, ESM::Class::Combat};
        Response r1 = {answer1, ESM::Class::Magic};
        Response r2 = {answer2, ESM::Class::Stealth};

        // randomize order in which responses are displayed
        int order = Misc::Rng::rollDice(6);

        switch (order)
        {
            case 0:
                return {question, {r0, r1, r2}, sound};
            case 1:
                return {question, {r0, r2, r1}, sound};
            case 2:
                return {question, {r1, r0, r2}, sound};
            case 3:
                return {question, {r1, r2, r0}, sound};
            case 4:
                return {question, {r2, r0, r1}, sound};
            default:
                return {question, {r2, r1, r0}, sound};
        }
    }

    void updatePlayerHealth()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWMechanics::NpcStats& npcStats = player.getClass().getNpcStats(player);
        npcStats.updateHealth();
    }
}

namespace MWGui
{

    CharacterCreation::CharacterCreation(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : mParent(parent)
        , mResourceSystem(resourceSystem)
        , mNameDialog(nullptr)
        , mRaceDialog(nullptr)
        , mClassChoiceDialog(nullptr)
        , mGenerateClassQuestionDialog(nullptr)
        , mGenerateClassResultDialog(nullptr)
        , mPickClassDialog(nullptr)
        , mCreateClassDialog(nullptr)
        , mBirthSignDialog(nullptr)
        , mReviewDialog(nullptr)
        , mPendingReviewDialog(-1)
        , mEditingFromReview(false)
        , mDeferredAction(DA_None)
        , mPendingOpenMode(-1)
        , mGenerateClassStep(0)
    {
        mCreationStage = CSE_NotStarted;
        mGenerateClassResponses[0] = ESM::Class::Combat;
        mGenerateClassResponses[1] = ESM::Class::Magic;
        mGenerateClassResponses[2] = ESM::Class::Stealth;
        mGenerateClassSpecializations[0] = 0;
        mGenerateClassSpecializations[1] = 0;
        mGenerateClassSpecializations[2] = 0;

        // Setup player stats
        for (int i = 0; i < ESM::Attribute::Length; ++i)
            mPlayerAttributes.emplace(ESM::Attribute::sAttributeIds[i], MWMechanics::AttributeValue());

        for (int i = 0; i < ESM::Skill::Length; ++i)
            mPlayerSkillValues.emplace(ESM::Skill::sSkillIds[i], MWMechanics::SkillValue());
    }

    void CharacterCreation::setValue (const std::string& id, const MWMechanics::AttributeValue& value)
    {
        static const char *ids[] =
        {
            "AttribVal1", "AttribVal2", "AttribVal3", "AttribVal4",
            "AttribVal5", "AttribVal6", "AttribVal7", "AttribVal8", 0
        };

        for (int i=0; ids[i]; ++i)
        {
            if (ids[i]==id)
            {
                mPlayerAttributes[static_cast<ESM::Attribute::AttributeID>(i)] = value;
                if (mReviewDialog)
                    mReviewDialog->setAttribute(static_cast<ESM::Attribute::AttributeID>(i), value);

                break;
            }
        }
    }

    void CharacterCreation::setValue (const std::string& id, const MWMechanics::DynamicStat<float>& value)
    {
        if (mReviewDialog)
        {
            if (id == "HBar")
            {
                mReviewDialog->setHealth (value);
            }
            else if (id == "MBar")
            {
                mReviewDialog->setMagicka (value);
            }
            else if (id == "FBar")
            {
                mReviewDialog->setFatigue (value);
            }
        }
    }

    void CharacterCreation::setValue(const ESM::Skill::SkillEnum parSkill, const MWMechanics::SkillValue& value)
    {
        mPlayerSkillValues[parSkill] = value;
        if (mReviewDialog)
            mReviewDialog->setSkillValue(parSkill, value);
    }

    void CharacterCreation::configureSkills (const SkillList& major, const SkillList& minor)
    {
        if (mReviewDialog)
            mReviewDialog->configureSkills(major, minor);

        mPlayerMajorSkills = major;
        mPlayerMinorSkills = minor;
    }

    void CharacterCreation::closeRaceDialog()
    {
        if (!mRaceDialog)
            return;
        MWBase::Environment::get().getWindowManager()->removeDialog(mRaceDialog);
        mRaceDialog = nullptr;
    }

    void CharacterCreation::closeReviewDialog()
    {
        if (!mReviewDialog)
            return;
        MWBase::Environment::get().getWindowManager()->removeDialog(mReviewDialog);
        mReviewDialog = nullptr;
    }

    void CharacterCreation::finishStageDeferred(CSE currentStage, int nextMode)
    {
        MWBase::Environment::get().getWindowManager()->popGuiMode();

        // ArenaMP servers own the linear CharGen state. Keep the local state in
        // lock-step with the completed visual stage, while preserving the deferred
        // modal switching used by the modern ArenaMW creator.
        if (!mEditingFromReview && mwmp::Main::isInitialized())
            mwmp::Main::get().getLocalPlayer()->charGenState.currentStage = static_cast<int>(currentStage);

        if (mCreationStage == CSE_ReviewNext)
            mPendingOpenMode = GM_Review;
        else if (mCreationStage >= currentStage)
            mPendingOpenMode = nextMode;
        else
            mCreationStage = currentStage;
    }

    void CharacterCreation::returnToReviewDeferred()
    {
        mEditingFromReview = false;
        MWBase::Environment::get().getWindowManager()->popGuiMode();
        mPendingOpenMode = GM_Review;
    }

    void CharacterCreation::onFrame(float duration)
    {
        // Open the next GUI mode only on the frame AFTER the old modal was removed.
        // WindowManager::cleanupGarbage() runs after CharacterCreation::onFrame(), so
        // this guarantees old MyGUI widgets, RTT textures and preview animations are
        // destroyed before a new chargen window is constructed.
        if (mPendingOpenMode >= 0)
        {
            const int mode = mPendingOpenMode;
            mPendingOpenMode = -1;
            MWBase::Environment::get().getWindowManager()->pushGuiMode(static_cast<GuiMode>(mode));
            return;
        }

        // Review edit buttons only enqueue an intent from their MyGUI callback.
        if (mPendingReviewDialog >= 0)
        {
            const int dialog = mPendingReviewDialog;
            mPendingReviewDialog = -1;
            mEditingFromReview = true;

            closeReviewDialog();
            MWBase::Environment::get().getWindowManager()->popGuiMode();

            switch (dialog)
            {
                case ReviewDialog::NAME_DIALOG:
                    // The server has already bound the network account name. Do
                    // not reopen the account card from character review.
                    mEditingFromReview = false;
                    mPendingOpenMode = GM_Review;
                    break;
                case ReviewDialog::RACE_DIALOG:
                    mPendingOpenMode = GM_Race;
                    break;
                case ReviewDialog::CLASS_DIALOG:
                    mPendingOpenMode = GM_Class;
                    break;
                case ReviewDialog::BIRTHSIGN_DIALOG:
                    mPendingOpenMode = GM_Birth;
                    break;
                default:
                    mEditingFromReview = false;
                    mPendingOpenMode = GM_Review;
                    break;
            }
            return;
        }

        if (mDeferredAction != DA_None)
        {
            const DeferredAction action = mDeferredAction;
            mDeferredAction = DA_None;

            if (action == DA_NameDone)
            {
                if (mNameDialog)
                {
                    mPlayerName = mNameDialog->getLogin();
                    const std::string loginPassword = mNameDialog->getPassword();
                    const std::string loginLanguage = mNameDialog->getLanguage();
                    if (mPlayerName.length() > 31)
                        mPlayerName = mPlayerName.substr(0, 31);

                    Settings::Manager::setString("name", "Login", mPlayerName);
                    Settings::Manager::setString("password", "Login", loginPassword);
                    Settings::Manager::setString("interface language", "General", loginLanguage);
                    Settings::Manager::saveUser();
                    MWBase::Environment::get().getWindowManager()->setArenaLanguage(loginLanguage);
                    MWBase::Environment::get().getMechanicsManager()->setPlayerName(mPlayerName);
                    MWBase::Environment::get().getWindowManager()->removeDialog(mNameDialog);
                    mNameDialog = nullptr;
                }

                if (mEditingFromReview)
                    returnToReviewDeferred();
                else
                    finishStageDeferred(CSE_NameChosen, GM_Race);
                return;
            }

            if (action == DA_UnifiedDone)
            {
                if (!mRaceDialog)
                    return;

                const RaceDialog::Page page = mRaceDialog->getPage();
                if (page == RaceDialog::Page_Appearance)
                {
                    const ESM::NPC race = mRaceDialog->getResult();
                    const float playerScale = mRaceDialog->getPlayerScale();
                    mPlayerRaceId = race.mRace;
                    if (!mPlayerRaceId.empty())
                    {
                        MWBase::Environment::get().getMechanicsManager()->setPlayerRace(
                            race.mRace, race.isMale(), race.mHead, race.mHair);
                        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->rebuildAvatar();
                    }
                    MWBase::Environment::get().getWorld()->scaleObject(MWMechanics::getPlayer(), playerScale);
                    updatePlayerHealth();
                    closeRaceDialog();
                    if (mEditingFromReview)
                        returnToReviewDeferred();
                    else
                        finishStageDeferred(CSE_RaceChosen, GM_Class);
                    return;
                }

                if (page == RaceDialog::Page_Class)
                {
                    mPlayerClass = mRaceDialog->getClassResult();
                    mPlayerClassImageId = mRaceDialog->getClassImageId();
                    const bool customClass = mRaceDialog->isCustomClass();
                    if (customClass)
                        MWBase::Environment::get().getMechanicsManager()->setPlayerClass(mPlayerClass);
                    else if (!mPlayerClass.mId.empty())
                        MWBase::Environment::get().getMechanicsManager()->setPlayerClass(mPlayerClass.mId);
                    updatePlayerHealth();
                    closeRaceDialog();
                    if (mEditingFromReview)
                        returnToReviewDeferred();
                    else
                        finishStageDeferred(CSE_ClassChosen, GM_Birth);
                    return;
                }

                mPlayerBirthSignId = mRaceDialog->getBirthId();
                if (!mPlayerBirthSignId.empty())
                    MWBase::Environment::get().getMechanicsManager()->setPlayerBirthsign(mPlayerBirthSignId);
                updatePlayerHealth();
                closeRaceDialog();
                if (mEditingFromReview)
                    returnToReviewDeferred();
                else
                    finishStageDeferred(CSE_BirthSignChosen, GM_Review);
                return;
            }

            if (action == DA_UnifiedBack)
            {
                if (!mRaceDialog)
                    return;
                const RaceDialog::Page page = mRaceDialog->getPage();
                closeRaceDialog();

                if (mEditingFromReview)
                {
                    returnToReviewDeferred();
                    return;
                }

                MWBase::Environment::get().getWindowManager()->popGuiMode();
                if (mCreationStage == CSE_ReviewNext)
                    mPendingOpenMode = GM_Review;
                else if (page == RaceDialog::Page_Appearance)
                {
                    // Account name is already bound by the server. The appearance
                    // page intentionally has no Back button in MP. Treat an
                    // unexpected back event as a no-op instead of reopening login.
                    if (mwmp::Main::isInitialized())
                        mwmp::Main::get().getLocalPlayer()->charGenState.currentStage = 1;
                    mPendingOpenMode = GM_Race;
                }
                else if (page == RaceDialog::Page_Class)
                {
                    if (mwmp::Main::isInitialized())
                        mwmp::Main::get().getLocalPlayer()->charGenState.currentStage = 1;
                    mPendingOpenMode = GM_Race;
                }
                else
                {
                    if (mwmp::Main::isInitialized())
                        mwmp::Main::get().getLocalPlayer()->charGenState.currentStage = 2;
                    mPendingOpenMode = GM_Class;
                }
                return;
            }

            if (action == DA_ReviewDone)
            {
                if (mwmp::Main::isInitialized())
                    mwmp::Main::get().getLocalPlayer()->charGenState.currentStage = 5;
                closeReviewDialog();
                mEditingFromReview = false;
                MWBase::Environment::get().getWindowManager()->popGuiMode();
                return;
            }

            if (action == DA_ReviewBack)
            {
                closeReviewDialog();
                mEditingFromReview = false;
                mCreationStage = CSE_ReviewBack;
                if (mwmp::Main::isInitialized())
                    mwmp::Main::get().getLocalPlayer()->charGenState.currentStage = 3;
                MWBase::Environment::get().getWindowManager()->popGuiMode();
                mPendingOpenMode = GM_Birth;
                return;
            }
        }

        if (mReviewDialog)
            mReviewDialog->onFrame(duration);
        if (mRaceDialog)
            mRaceDialog->onFrame(duration);
    }

    void CharacterCreation::spawnDialog(const char id)
    {
        try
        {
            switch (id)
            {
                case GM_Name:
                    MWBase::Environment::get().getWindowManager()->removeDialog(mNameDialog);
                    mNameDialog = nullptr;
                    mNameDialog = new mwmp::GUILogin();
                    if (mPlayerName.empty())
                        mPlayerName = Settings::Manager::getString("name", "Login");
                    mNameDialog->setLogin(mPlayerName);
                    mNameDialog->setPassword(Settings::Manager::getString("password", "Login"));
                    mNameDialog->setLanguage(Settings::Manager::getString("interface language", "General"));
                    mNameDialog->setLoginEditable(true);
                    mNameDialog->setRetryMode(false);
                    mNameDialog->eventDone += MyGUI::newDelegate(this, &CharacterCreation::onNameDialogDone);
                    mNameDialog->setVisible(true);
                    break;

                case GM_Race:
                    if (!mRaceDialog)
                    {
                        mRaceDialog = new RaceDialog(mParent, mResourceSystem);
                        mRaceDialog->eventDone += MyGUI::newDelegate(this, &CharacterCreation::onUnifiedDialogDone);
                        mRaceDialog->eventBack += MyGUI::newDelegate(this, &CharacterCreation::onUnifiedDialogBack);
                    }
                    if (!mPlayerRaceId.empty())
                        mRaceDialog->setRaceId(mPlayerRaceId);
                    mRaceDialog->setPlayerScale(MWMechanics::getPlayer().getCellRef().getScale());
                    mRaceDialog->setPage(RaceDialog::Page_Appearance);
                    mRaceDialog->setVisible(true);
                    if (mCreationStage < CSE_NameChosen)
                        mCreationStage = CSE_NameChosen;
                    break;

                case GM_Class:
                case GM_ClassPick:
                case GM_ClassCreate:
                case GM_ClassGenerate:
                    // ArenaMW: all historical class entry points open the same modern
                    // profile editor. The question-based generator is intentionally bypassed.
                    if (!mRaceDialog)
                    {
                        mRaceDialog = new RaceDialog(mParent, mResourceSystem);
                        mRaceDialog->eventDone += MyGUI::newDelegate(this, &CharacterCreation::onUnifiedDialogDone);
                        mRaceDialog->eventBack += MyGUI::newDelegate(this, &CharacterCreation::onUnifiedDialogBack);
                    }
                    if (!mPlayerClass.mName.empty() || !mPlayerClass.mId.empty())
                        mRaceDialog->setClass(mPlayerClass);
                    if (!mPlayerClassImageId.empty())
                        mRaceDialog->setClassImageId(mPlayerClassImageId);
                    mRaceDialog->setPage(RaceDialog::Page_Class);
                    mRaceDialog->setVisible(true);
                    if (mCreationStage < CSE_RaceChosen)
                        mCreationStage = CSE_RaceChosen;
                    break;

                case GM_Birth:
                    if (!mRaceDialog)
                    {
                        mRaceDialog = new RaceDialog(mParent, mResourceSystem);
                        mRaceDialog->eventDone += MyGUI::newDelegate(this, &CharacterCreation::onUnifiedDialogDone);
                        mRaceDialog->eventBack += MyGUI::newDelegate(this, &CharacterCreation::onUnifiedDialogBack);
                    }
                    if (!mPlayerBirthSignId.empty())
                        mRaceDialog->setBirthId(mPlayerBirthSignId);
                    mRaceDialog->setPage(RaceDialog::Page_Birth);
                    mRaceDialog->setVisible(true);
                    if (mCreationStage < CSE_ClassChosen)
                        mCreationStage = CSE_ClassChosen;
                    break;

                case GM_Review:
                    // Review is reconstructed after every edit. Animated preview/RTT
                    // resources from the previous modal are fully destroyed first.
                    if (!mReviewDialog)
                    {
                        mReviewDialog = new ReviewDialog(mParent, mResourceSystem);
                        mReviewDialog->eventDone += MyGUI::newDelegate(this, &CharacterCreation::onReviewDialogDone);
                        mReviewDialog->eventBack += MyGUI::newDelegate(this, &CharacterCreation::onReviewDialogBack);
                        mReviewDialog->eventActivateDialog += MyGUI::newDelegate(this, &CharacterCreation::onReviewActivateDialog);
                    }
                    populateReviewDialog();
                    mReviewDialog->setVisible(true);
                    if (mCreationStage < CSE_BirthSignChosen)
                        mCreationStage = CSE_BirthSignChosen;
                    break;
            }
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Error: Failed to create chargen window: " << e.what();
        }
    }

    void CharacterCreation::populateReviewDialog()
    {
        if (!mReviewDialog)
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const ESM::NPC* playerNpc = world->getPlayerPtr().get<ESM::NPC>()->mBase;
        const MWWorld::Player player = world->getPlayer();
        const ESM::Class* playerClass = world->getStore().get<ESM::Class>().search(playerNpc->mClass);

        mReviewDialog->setPlayerName(playerNpc->mName);
        mReviewDialog->setRace(playerNpc->mRace);
        if (playerClass)
            mReviewDialog->setClass(*playerClass);
        mReviewDialog->setBirthSign(player.getBirthSign());
        mReviewDialog->setPlayerScale(MWMechanics::getPlayer().getCellRef().getScale());

        MWWorld::Ptr playerPtr = MWMechanics::getPlayer();
        const MWMechanics::CreatureStats& stats = playerPtr.getClass().getCreatureStats(playerPtr);
        mReviewDialog->setHealth(stats.getHealth());
        mReviewDialog->setMagicka(stats.getMagicka());
        mReviewDialog->setFatigue(stats.getFatigue());
        for (auto& attributePair : mPlayerAttributes)
            mReviewDialog->setAttribute(static_cast<ESM::Attribute::AttributeID>(attributePair.first), attributePair.second);
        for (auto& skillPair : mPlayerSkillValues)
            mReviewDialog->setSkillValue(static_cast<ESM::Skill::SkillEnum>(skillPair.first), skillPair.second);
        mReviewDialog->configureSkills(mPlayerMajorSkills, mPlayerMinorSkills);
    }

    void CharacterCreation::onUnifiedDialogDone(WindowBase*)
    {
        if (mRaceDialog && mDeferredAction == DA_None)
            mDeferredAction = DA_UnifiedDone;
    }

    void CharacterCreation::onUnifiedDialogBack()
    {
        if (mRaceDialog && mDeferredAction == DA_None)
            mDeferredAction = DA_UnifiedBack;
    }

    void CharacterCreation::onReviewDialogDone(WindowBase*)
    {
        if (mReviewDialog && mDeferredAction == DA_None)
            mDeferredAction = DA_ReviewDone;
    }

    void CharacterCreation::onReviewDialogBack()
    {
        if (mReviewDialog && mDeferredAction == DA_None)
            mDeferredAction = DA_ReviewBack;
    }

    void CharacterCreation::onReviewActivateDialog(int parDialog)
    {
        // Never close, hide or switch a modal from its own MyGUI callback. onFrame()
        // tears the Review RTT down, waits for WindowManager garbage cleanup, and only
        // then opens the requested editor on the following frame.
        if (mPendingReviewDialog < 0 && !mEditingFromReview && mDeferredAction == DA_None)
            mPendingReviewDialog = parDialog;
    }

    void CharacterCreation::selectPickedClass()
    {
        if (mPickClassDialog)
        {
            const std::string &classId = mPickClassDialog->getClassId();
            if (!classId.empty())
                MWBase::Environment::get().getMechanicsManager()->setPlayerClass(classId);

            const ESM::Class *klass =
                MWBase::Environment::get().getWorld()->getStore().get<ESM::Class>().find(classId);
            if (klass)
            {
                mPlayerClass = *klass;
            }
            MWBase::Environment::get().getWindowManager()->removeDialog(mPickClassDialog);
            mPickClassDialog = nullptr;
        }

        updatePlayerHealth();
    }

    void CharacterCreation::onPickClassDialogDone(WindowBase* parWindow)
    {
        selectPickedClass();

        handleDialogDone(CSE_ClassChosen, GM_Birth);
    }

    void CharacterCreation::onPickClassDialogBack()
    {
        selectPickedClass();

        MWBase::Environment::get().getWindowManager()->popGuiMode();
        MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Class);
    }

    void CharacterCreation::onClassChoice(int _index)
    {
        MWBase::Environment::get().getWindowManager()->removeDialog(mClassChoiceDialog);
        mClassChoiceDialog = nullptr;

        MWBase::Environment::get().getWindowManager()->popGuiMode();

        switch(_index)
        {
            case ClassChoiceDialog::Class_Generate:
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_ClassGenerate);
                break;
            case ClassChoiceDialog::Class_Pick:
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_ClassPick);
                break;
            case ClassChoiceDialog::Class_Create:
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_ClassCreate);
                break;
            case ClassChoiceDialog::Class_Back:
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Race);
                break;

        };
    }

    void CharacterCreation::onNameDialogDone(WindowBase*)
    {
        if (mNameDialog && mDeferredAction == DA_None)
            mDeferredAction = DA_NameDone;
    }

    void CharacterCreation::selectRace()
    {
        if (mRaceDialog)
        {
            const ESM::NPC &data = mRaceDialog->getResult();
            mPlayerRaceId = data.mRace;
            if (!mPlayerRaceId.empty()) {
                MWBase::Environment::get().getMechanicsManager()->setPlayerRace(
                    data.mRace,
                    data.isMale(),
                    data.mHead,
                    data.mHair
                );
            }
            MWBase::Environment::get().getWindowManager()->getInventoryWindow()->rebuildAvatar();

            MWBase::Environment::get().getWindowManager()->removeDialog(mRaceDialog);
            mRaceDialog = nullptr;
        }

        updatePlayerHealth();
    }

    void CharacterCreation::onRaceDialogBack()
    {
        selectRace();

        MWBase::Environment::get().getWindowManager()->popGuiMode();
        MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Name);
    }

    void CharacterCreation::onRaceDialogDone(WindowBase* parWindow)
    {
        selectRace();

        handleDialogDone(CSE_RaceChosen, GM_Class);
    }

    void CharacterCreation::selectBirthSign()
    {
        if (mBirthSignDialog)
        {
            mPlayerBirthSignId = mBirthSignDialog->getBirthId();
            if (!mPlayerBirthSignId.empty())
                MWBase::Environment::get().getMechanicsManager()->setPlayerBirthsign(mPlayerBirthSignId);
            MWBase::Environment::get().getWindowManager()->removeDialog(mBirthSignDialog);
            mBirthSignDialog = nullptr;
        }

        updatePlayerHealth();
    }

    void CharacterCreation::onBirthSignDialogDone(WindowBase* parWindow)
    {
        selectBirthSign();

        handleDialogDone(CSE_BirthSignChosen, GM_Review);
    }

    void CharacterCreation::onBirthSignDialogBack()
    {
        selectBirthSign();

        MWBase::Environment::get().getWindowManager()->popGuiMode();
        MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Class);
    }

    void CharacterCreation::selectCreatedClass()
    {
        if (mCreateClassDialog)
        {
            ESM::Class klass;
            klass.mName = mCreateClassDialog->getName();
            klass.mDescription = mCreateClassDialog->getDescription();
            klass.mData.mSpecialization = mCreateClassDialog->getSpecializationId();
            klass.mData.mIsPlayable = 0x1;

            std::vector<int> attributes = mCreateClassDialog->getFavoriteAttributes();
            assert(attributes.size() == 2);
            klass.mData.mAttribute[0] = attributes[0];
            klass.mData.mAttribute[1] = attributes[1];

            std::vector<ESM::Skill::SkillEnum> majorSkills = mCreateClassDialog->getMajorSkills();
            std::vector<ESM::Skill::SkillEnum> minorSkills = mCreateClassDialog->getMinorSkills();
            assert(majorSkills.size() >= sizeof(klass.mData.mSkills)/sizeof(klass.mData.mSkills[0]));
            assert(minorSkills.size() >= sizeof(klass.mData.mSkills)/sizeof(klass.mData.mSkills[0]));
            for (size_t i = 0; i < sizeof(klass.mData.mSkills)/sizeof(klass.mData.mSkills[0]); ++i)
            {
                klass.mData.mSkills[i][1] = majorSkills[i];
                klass.mData.mSkills[i][0] = minorSkills[i];
            }

            MWBase::Environment::get().getMechanicsManager()->setPlayerClass(klass);
            mPlayerClass = klass;

            // Do not delete dialog, so that choices are remembered in case we want to go back and adjust them later
            mCreateClassDialog->setVisible(false);
        }
        updatePlayerHealth();
    }

    void CharacterCreation::onCreateClassDialogDone(WindowBase* parWindow)
    {
        selectCreatedClass();

        handleDialogDone(CSE_ClassChosen, GM_Birth);
    }

    void CharacterCreation::onCreateClassDialogBack()
    {
        // not done in MW, but we do it for consistency with the other dialogs
        selectCreatedClass();

        MWBase::Environment::get().getWindowManager()->popGuiMode();
        MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Class);
    }

    void CharacterCreation::onClassQuestionChosen(int _index)
    {
        MWBase::Environment::get().getSoundManager()->stopSay();

        MWBase::Environment::get().getWindowManager()->removeDialog(mGenerateClassQuestionDialog);
        mGenerateClassQuestionDialog = nullptr;

        if (_index < 0 || _index >= 3)
        {
            MWBase::Environment::get().getWindowManager()->popGuiMode();
            MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Class);
            return;
        }

        ESM::Class::Specialization specialization = mGenerateClassResponses[_index];
        if (specialization == ESM::Class::Combat)
            ++mGenerateClassSpecializations[0];
        else if (specialization == ESM::Class::Magic)
            ++mGenerateClassSpecializations[1];
        else if (specialization == ESM::Class::Stealth)
            ++mGenerateClassSpecializations[2];
        ++mGenerateClassStep;
        showClassQuestionDialog();
    }

    void CharacterCreation::showClassQuestionDialog()
    {
        if (mGenerateClassStep == 10)
        {
            unsigned combat = mGenerateClassSpecializations[0];
            unsigned magic = mGenerateClassSpecializations[1];
            unsigned stealth = mGenerateClassSpecializations[2];

            if (combat > 7)
            {
                mGenerateClass = "Warrior";
            }
            else if (magic > 7)
            {
                mGenerateClass = "Mage";
            }
            else if (stealth > 7)
            {
                mGenerateClass = "Thief";
            }
            else
            {
                switch (combat)
                {
                    case 4:
                        mGenerateClass = "Rogue";
                        break;
                    case 5:
                        if (stealth == 3)
                            mGenerateClass = "Scout";
                        else
                            mGenerateClass = "Archer";
                        break;
                    case 6:
                        if (stealth == 1)
                            mGenerateClass = "Barbarian";
                        else if (stealth == 3)
                            mGenerateClass = "Crusader";
                        else
                            mGenerateClass = "Knight";
                        break;
                    case 7:
                        mGenerateClass = "Warrior";
                        break;
                    default:
                        switch (magic)
                        {
                            case 4:
                                mGenerateClass = "Spellsword";
                                break;
                            case 5:
                                mGenerateClass = "Witchhunter";
                                break;
                            case 6:
                                if (combat == 2)
                                    mGenerateClass = "Sorcerer";
                                else if (combat == 3)
                                    mGenerateClass = "Healer";
                                else
                                    mGenerateClass = "Battlemage";
                                break;
                            case 7:
                                mGenerateClass = "Mage";
                                break;
                            default:
                                switch (stealth)
                                {
                                    case 3:
                                        if (magic == 3)
                                            mGenerateClass = "Bard"; // unreachable
                                        else
                                            mGenerateClass = "Warrior";
                                        break;
                                    case 5:
                                        if (magic == 3)
                                            mGenerateClass = "Monk";
                                        else
                                            mGenerateClass = "Pilgrim";
                                        break;
                                    case 6:
                                        if (magic == 1)
                                            mGenerateClass = "Agent";
                                        else if (magic == 3)
                                            mGenerateClass = "Assassin";
                                        else
                                            mGenerateClass = "Acrobat";
                                        break;
                                    case 7:
                                        mGenerateClass = "Thief";
                                        break;
                                    default:
                                        mGenerateClass = "Warrior";
                                }
                        }
                }
            }

            MWBase::Environment::get().getWindowManager()->removeDialog(mGenerateClassResultDialog);
            mGenerateClassResultDialog = nullptr;

            mGenerateClassResultDialog = new GenerateClassResultDialog();
            mGenerateClassResultDialog->setClassId(mGenerateClass);
            mGenerateClassResultDialog->eventBack += MyGUI::newDelegate(this, &CharacterCreation::onGenerateClassBack);
            mGenerateClassResultDialog->eventDone += MyGUI::newDelegate(this, &CharacterCreation::onGenerateClassDone);
            mGenerateClassResultDialog->setVisible(true);
            return;
        }

        if (mGenerateClassStep > 10)
        {
            MWBase::Environment::get().getWindowManager()->popGuiMode();
            MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Class);
            return;
        }

        MWBase::Environment::get().getWindowManager()->removeDialog(mGenerateClassQuestionDialog);
        mGenerateClassQuestionDialog = nullptr;

        mGenerateClassQuestionDialog = new InfoBoxDialog();

        Step step = sGenerateClassSteps(mGenerateClassStep);
        mGenerateClassResponses[0] = step.mResponses[0].mSpecialization;
        mGenerateClassResponses[1] = step.mResponses[1].mSpecialization;
        mGenerateClassResponses[2] = step.mResponses[2].mSpecialization;

        InfoBoxDialog::ButtonList buttons;
        mGenerateClassQuestionDialog->setText(step.mText);
        buttons.push_back(step.mResponses[0].mText);
        buttons.push_back(step.mResponses[1].mText);
        buttons.push_back(step.mResponses[2].mText);
        mGenerateClassQuestionDialog->setButtons(buttons);
        mGenerateClassQuestionDialog->eventButtonSelected += MyGUI::newDelegate(this, &CharacterCreation::onClassQuestionChosen);
        mGenerateClassQuestionDialog->setVisible(true);

        MWBase::Environment::get().getSoundManager()->say(step.mSound);
    }

    void CharacterCreation::selectGeneratedClass()
    {
        MWBase::Environment::get().getWindowManager()->removeDialog(mGenerateClassResultDialog);
        mGenerateClassResultDialog = nullptr;

        MWBase::Environment::get().getMechanicsManager()->setPlayerClass(mGenerateClass);

        const ESM::Class *klass =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::Class>().find(mGenerateClass);

        mPlayerClass = *klass;

        updatePlayerHealth();
    }

    void CharacterCreation::onGenerateClassBack()
    {
        selectGeneratedClass();

        MWBase::Environment::get().getWindowManager()->popGuiMode();
        MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Class);
    }

    void CharacterCreation::onGenerateClassDone(WindowBase* parWindow)
    {
        selectGeneratedClass();

        handleDialogDone(CSE_ClassChosen, GM_Birth);
    }

    CharacterCreation::~CharacterCreation()
    {
        delete mNameDialog;
        delete mRaceDialog;
        delete mClassChoiceDialog;
        delete mGenerateClassQuestionDialog;
        delete mGenerateClassResultDialog;
        delete mPickClassDialog;
        delete mCreateClassDialog;
        delete mBirthSignDialog;
        delete mReviewDialog;
    }

    void CharacterCreation::handleDialogDone(CSE currentStage, int nextMode)
    {
        MWBase::Environment::get().getWindowManager()->popGuiMode();
        if (mCreationStage == CSE_ReviewNext)
        {
            MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Review);
        }
        else if (mCreationStage >= currentStage)
        {
            MWBase::Environment::get().getWindowManager()->pushGuiMode((GuiMode)nextMode);
        }
        else
        {
            mCreationStage = currentStage;
        }
    }
}
