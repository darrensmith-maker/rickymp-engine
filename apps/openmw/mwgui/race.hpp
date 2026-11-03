#ifndef MWGUI_RACE_H
#define MWGUI_RACE_H

#include <memory>
#include <string>
#include <vector>

#include <MyGUI_RenderManager.h>
#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>

#include <components/esm/loadclas.hpp>
#include <components/esm/loadnpc.hpp>

#include "widgets.hpp"
#include "windowbase.hpp"

namespace MWRender
{
    class RaceSelectionPreview;
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
    class SelectSpecializationDialog;
    class SelectAttributeDialog;
    class SelectSkillDialog;

    // ArenaMW modern three-page character creator. The same window shell is reused
    // for Race, Class and Birthsign, but each vanilla chargen request shows only its page.
    class RaceDialog : public WindowModal
    {
    public:
        enum Gender
        {
            GM_Male,
            GM_Female
        };

        enum Page
        {
            Page_Appearance = 0,
            Page_Class = 1,
            Page_Birth = 2
        };

        RaceDialog(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        ~RaceDialog() override;

        const ESM::NPC& getResult() const;
        const std::string& getRaceId() const { return mCurrentRaceId; }
        Gender getGender() const { return mGenderIndex == 0 ? GM_Male : GM_Female; }

        float getPlayerScale() const { return mPlayerScale; }
        const ESM::Class& getClassResult() const { return mClassResult; }
        bool isCustomClass() const { return mCustomClass; }
        const std::string& getClassImageId() const { return mClassImageId; }
        const std::string& getBirthId() const { return mCurrentBirthId; }
        Page getPage() const { return mPage; }

        void setPlayerScale(float value);
        void setRaceId(const std::string& raceId);
        void setGender(Gender gender);
        void setClass(const ESM::Class& klass);
        void setClassImageId(const std::string& classId);
        void setBirthId(const std::string& birthId);
        void setPage(Page page);

        // Kept for compatibility with the historical RaceDialog API.
        void setNextButtonShow(bool shown);
        void onOpen() override;
        void onClose() override;
        void onFrame(float duration);
        bool exit() override { return false; }

        typedef MyGUI::delegates::CMultiDelegate0 EventHandle_Void;
        EventHandle_Void eventBack;
        EventHandle_WindowBase eventDone;

    protected:
        void onScaleChanged(MyGUI::ScrollBar* sender, size_t position);
        void onPreviewMousePressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onPreviewMouseDrag(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onPreviewMouseWheel(MyGUI::Widget* sender, int rel);

        void onSelectPreviousRace(MyGUI::Widget* sender);
        void onSelectNextRace(MyGUI::Widget* sender);
        void onSelectPreviousGender(MyGUI::Widget* sender);
        void onSelectNextGender(MyGUI::Widget* sender);
        void onSelectPreviousFace(MyGUI::Widget* sender);
        void onSelectNextFace(MyGUI::Widget* sender);
        void onSelectPreviousHair(MyGUI::Widget* sender);
        void onSelectNextHair(MyGUI::Widget* sender);

        void onSelectClass(MyGUI::ListBox* sender, size_t index);
        void onRandomCombatClass(MyGUI::Widget* sender);
        void onRandomMagicClass(MyGUI::Widget* sender);
        void onRandomStealthClass(MyGUI::Widget* sender);
        void onRandomAnyClass(MyGUI::Widget* sender);
        void onSelectBirth(MyGUI::ListBox* sender, size_t index);

        void onSpecializationClicked(MyGUI::Widget* sender);
        void onSpecializationSelected();
        void onAttributeClicked(Widgets::MWAttributePtr sender);
        void onAttributeSelected();
        void onSkillClicked(Widgets::MWSkillPtr sender);
        void onSkillSelected();
        void onSubDialogCancel();

        void onBackClicked(MyGUI::Widget* sender);
        void onNextClicked(MyGUI::Widget* sender);

    private:
        void updatePageVisibility();
        void updatePageHeader();

        void updateRaces();
        void updateRaceName();
        void updateAppearanceStats();
        void updatePreview();
        void resetPreviewView(bool headFocus);
        void recountParts();
        void getBodyParts(int part, std::vector<std::string>& out);

        void updateClasses();
        void loadCurrentClass();
        void updateClassStats();
        void promoteToCustomClass();
        void randomizeClass(ESM::Class::Specialization specialization);

        void updateBirths();
        void updateBirthSpells();

        static int wrap(int index, int max);

        osg::Group* mParent;
        Resource::ResourceSystem* mResourceSystem;

        Page mPage;
        MyGUI::Widget* mAppearancePage;
        MyGUI::Widget* mClassPage;
        MyGUI::Widget* mBirthPage;
        MyGUI::TextBox* mStepTitle;
        MyGUI::TextBox* mStepCounter;
        MyGUI::Button* mBackButton;
        MyGUI::Button* mNextButton;

        // Appearance page
        MyGUI::ImageBox* mPreviewImage;
        MyGUI::ScrollBar* mScale;
        MyGUI::TextBox* mRaceValue;
        MyGUI::TextBox* mGenderValue;
        MyGUI::TextBox* mFaceValue;
        MyGUI::TextBox* mHairValue;
        Widgets::MWAttributePtr mStartAttributes[8];

        std::vector<std::string> mRaceIds;
        std::vector<std::string> mRaceNames;
        std::vector<std::string> mAvailableHeads;
        std::vector<std::string> mAvailableHairs;
        int mRaceIndex;
        int mGenderIndex;
        int mFaceIndex;
        int mHairIndex;
        std::string mCurrentRaceId;
        float mPlayerScale;
        float mCurrentAngle;
        float mViewZoom;
        float mViewOffsetX;
        float mViewOffsetZ;
        int mPreviewDragX;
        int mPreviewDragY;

        std::unique_ptr<MWRender::RaceSelectionPreview> mPreview;
        std::unique_ptr<MyGUI::ITexture> mPreviewTexture;

        // Class page
        MyGUI::ListBox* mClassList;
        MyGUI::ImageBox* mClassImage;
        MyGUI::TextBox* mClassName;
        MyGUI::Button* mSpecializationName;
        Widgets::MWAttributePtr mFavoriteAttribute[2];
        Widgets::MWSkillPtr mMajorSkill[5];
        Widgets::MWSkillPtr mMinorSkill[5];
        ESM::Class mClassResult;
        std::string mCurrentClassId;
        std::string mClassImageId;
        bool mCustomClass;

        SelectSpecializationDialog* mSpecDialog;
        SelectAttributeDialog* mAttribDialog;
        SelectSkillDialog* mSkillDialog;
        Widgets::MWAttributePtr mAffectedAttribute;
        Widgets::MWSkillPtr mAffectedSkill;

        // Birth page
        MyGUI::ListBox* mBirthList;
        MyGUI::ImageBox* mBirthImage;
        MyGUI::ScrollView* mSpellArea;
        std::vector<MyGUI::Widget*> mSpellItems;
        std::string mCurrentBirthId;
    };
}

#endif
