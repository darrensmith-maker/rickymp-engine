#ifndef CHARACTER_CREATION_HPP
#define CHARACTER_CREATION_HPP

#include <components/esm/loadclas.hpp>

#include <map>
#include <vector>

#include "statswatcher.hpp"

namespace osg
{
    class Group;
}

namespace Resource
{
    class ResourceSystem;
}

namespace mwmp
{
    class GUILogin;
}

namespace MWGui
{
    class WindowBase;

    class InfoBoxDialog;
    class RaceDialog;
    class DialogueWindow;
    class ClassChoiceDialog;
    class GenerateClassResultDialog;
    class PickClassDialog;
    class CreateClassDialog;
    class BirthDialog;
    class ReviewDialog;
    class MessageBoxManager;

    class CharacterCreation : public StatsListener
    {
    public:
    typedef std::vector<int> SkillList;

    CharacterCreation(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
    virtual ~CharacterCreation();

    //Show a dialog
    void spawnDialog(const char id);

    void setValue (const std::string& id, const MWMechanics::AttributeValue& value) override;
    void setValue (const std::string& id, const MWMechanics::DynamicStat<float>& value) override;
    void setValue(const ESM::Skill::SkillEnum parSkill, const MWMechanics::SkillValue& value) override;
    void configureSkills(const SkillList& major, const SkillList& minor) override;

    void onFrame(float duration);

    private:
    osg::Group* mParent;
    Resource::ResourceSystem* mResourceSystem;

    SkillList mPlayerMajorSkills, mPlayerMinorSkills;
    std::map<int, MWMechanics::AttributeValue> mPlayerAttributes;
    std::map<int, MWMechanics::SkillValue> mPlayerSkillValues;

    //Dialogs
    mwmp::GUILogin* mNameDialog;
    RaceDialog* mRaceDialog;
    ClassChoiceDialog* mClassChoiceDialog;
    InfoBoxDialog* mGenerateClassQuestionDialog;
    GenerateClassResultDialog* mGenerateClassResultDialog;
    PickClassDialog* mPickClassDialog;
    CreateClassDialog* mCreateClassDialog;
    BirthDialog* mBirthSignDialog;
    ReviewDialog* mReviewDialog;
    int mPendingReviewDialog;
    bool mEditingFromReview;

    // CharGen windows with RTT/animation must never destroy or switch themselves
    // from inside a MyGUI callback. All closing and mode changes are processed by
    // CharacterCreation::onFrame, with the next GUI mode opened one frame later.
    enum DeferredAction
    {
        DA_None,
        DA_NameDone,
        DA_UnifiedDone,
        DA_UnifiedBack,
        DA_ReviewDone,
        DA_ReviewBack
    };
    DeferredAction mDeferredAction;
    int mPendingOpenMode;

    //Player data
    std::string mPlayerName;
    std::string mPlayerRaceId;
    std::string mPlayerBirthSignId;
    ESM::Class mPlayerClass;
    std::string mPlayerClassImageId;

    //Class generation vars
    unsigned mGenerateClassStep;                 // Keeps track of current step in Generate Class dialog
    ESM::Class::Specialization mGenerateClassResponses[3];
    unsigned mGenerateClassSpecializations[3];   // A counter for each specialization which is increased when an answer is chosen
    std::string mGenerateClass;                  // In order: Combat, Magic, Stealth

    ////Dialog events
    //Name dialog
    void onNameDialogDone(WindowBase* parWindow);

    //Race dialog
    void onRaceDialogDone(WindowBase* parWindow);
    void onRaceDialogBack();
    void selectRace();

    //Class dialogs
    void onClassChoice(int _index);
    void onPickClassDialogDone(WindowBase* parWindow);
    void onPickClassDialogBack();
    void onCreateClassDialogDone(WindowBase* parWindow);
    void onCreateClassDialogBack();
    void showClassQuestionDialog();
    void onClassQuestionChosen(int _index);
    void onGenerateClassBack();
    void onGenerateClassDone(WindowBase* parWindow);
    void selectGeneratedClass();
    void selectCreatedClass();
    void selectPickedClass();

    //Birthsign dialog
    void onBirthSignDialogDone(WindowBase* parWindow);
    void onBirthSignDialogBack();
    void selectBirthSign();

    //Review dialog
    void onReviewDialogDone(WindowBase* parWindow);
    void onReviewDialogBack();
    void onReviewActivateDialog(int parDialog);
    void populateReviewDialog();

    // ArenaMW modern staged CharGen. Name remains the vanilla text dialog; Race,
    // Class and Birthsign reuse one modern window shell and commit one stage at a time.
    void onUnifiedDialogDone(WindowBase* parWindow);
    void onUnifiedDialogBack();

    enum CSE    //Creation Stage Enum
    {
        CSE_NotStarted,
        CSE_NameChosen,
        CSE_RaceChosen,
        CSE_ClassChosen,
        CSE_BirthSignChosen,
        CSE_ReviewBack,
        CSE_ReviewNext
    };

    CSE mCreationStage; // Which state the character creating is in, controls back/next/ok buttons

    void closeRaceDialog();
    void closeReviewDialog();
    void finishStageDeferred(CSE currentStage, int nextMode);
    void returnToReviewDeferred();
    void handleDialogDone(CSE currentStage, int nextMode);
    };
}

#endif
