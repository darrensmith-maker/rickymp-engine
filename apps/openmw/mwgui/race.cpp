#include "race.hpp"

#include <algorithm>
#include <cmath>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>

#include <osg/Texture2D>

#include <components/debug/debuglog.hpp>
#include <components/esm/attr.hpp>
#include <components/esm/loadbody.hpp>
#include <components/esm/loadrace.hpp>
#include <components/esm/loadskil.hpp>
#include <components/esm/loadspel.hpp>
#include <components/misc/stringops.hpp>
#include <components/misc/rng.hpp>
#include <components/myguiplatform/myguitexture.hpp>

#include "class.hpp"
#include "tooltips.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/characterpreview.hpp"
#include "../mwworld/esmstore.hpp"

namespace
{
    bool sortBySecond(const std::pair<std::string, std::string>& left,
        const std::pair<std::string, std::string>& right)
    {
        return left.second < right.second;
    }

    bool sortBirthSigns(const std::pair<std::string, const ESM::BirthSign*>& left,
        const std::pair<std::string, const ESM::BirthSign*>& right)
    {
        return left.second->mName < right.second->mName;
    }
}

namespace MWGui
{
    int RaceDialog::wrap(int index, int max)
    {
        if (max <= 0)
            return 0;
        if (index < 0)
            return max - 1;
        if (index >= max)
            return 0;
        return index;
    }

    RaceDialog::RaceDialog(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : WindowModal("openmw_chargen_race.layout")
        , mParent(parent)
        , mResourceSystem(resourceSystem)
        , mPage(Page_Appearance)
        , mAppearancePage(nullptr)
        , mClassPage(nullptr)
        , mBirthPage(nullptr)
        , mStepTitle(nullptr)
        , mStepCounter(nullptr)
        , mBackButton(nullptr)
        , mNextButton(nullptr)
        , mPreviewImage(nullptr)
        , mScale(nullptr)
        , mRaceValue(nullptr)
        , mGenderValue(nullptr)
        , mFaceValue(nullptr)
        , mHairValue(nullptr)
        , mRaceIndex(0)
        , mGenderIndex(0)
        , mFaceIndex(0)
        , mHairIndex(0)
        , mPlayerScale(1.f)
        , mCurrentAngle(0.f)
        , mViewZoom(1.f)
        , mViewOffsetX(0.f)
        , mViewOffsetZ(0.f)
        , mPreviewDragX(0)
        , mPreviewDragY(0)
        , mClassList(nullptr)
        , mClassImage(nullptr)
        , mClassName(nullptr)
        , mSpecializationName(nullptr)
        , mCustomClass(false)
        , mSpecDialog(nullptr)
        , mAttribDialog(nullptr)
        , mSkillDialog(nullptr)
        , mAffectedAttribute(nullptr)
        , mAffectedSkill(nullptr)
        , mBirthList(nullptr)
        , mBirthImage(nullptr)
        , mSpellArea(nullptr)
    {
        center();

        getWidget(mAppearancePage, "AppearancePage");
        getWidget(mClassPage, "ClassPage");
        getWidget(mBirthPage, "BirthPage");
        getWidget(mStepTitle, "StepTitle");
        getWidget(mStepCounter, "StepCounter");
        getWidget(mBackButton, "BackButton");
        getWidget(mNextButton, "NextButton");
        mBackButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onBackClicked);
        mNextButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onNextClicked);

        getWidget(mPreviewImage, "PreviewImage");
        getWidget(mScale, "HeightScale");
        getWidget(mRaceValue, "RaceValue");
        getWidget(mGenderValue, "GenderValue");
        getWidget(mFaceValue, "FaceValue");
        getWidget(mHairValue, "HairValue");

        // 61 integer steps -> 0.85 .. 1.15 in 0.005 increments.
        mScale->setScrollRange(61);
        mScale->setScrollPosition(30);
        mScale->setScrollViewPage(1);
        mScale->setScrollPage(1);
        mScale->setScrollWheelPage(2);
        mScale->eventScrollChangePosition += MyGUI::newDelegate(this, &RaceDialog::onScaleChanged);

        mPreviewImage->setNeedMouseFocus(true);
        mPreviewImage->eventMouseButtonPressed += MyGUI::newDelegate(this, &RaceDialog::onPreviewMousePressed);
        mPreviewImage->eventMouseDrag += MyGUI::newDelegate(this, &RaceDialog::onPreviewMouseDrag);
        mPreviewImage->eventMouseWheel += MyGUI::newDelegate(this, &RaceDialog::onPreviewMouseWheel);

        MyGUI::Button* button = nullptr;
        getWidget(button, "PrevRaceButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousRace);
        getWidget(button, "NextRaceButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextRace);
        getWidget(button, "PrevGenderButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousGender);
        getWidget(button, "NextGenderButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextGender);
        getWidget(button, "PrevFaceButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousFace);
        getWidget(button, "NextFaceButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextFace);
        getWidget(button, "PrevHairButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousHair);
        getWidget(button, "NextHairButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextHair);

        for (int i = 0; i < 8; ++i)
        {
            getWidget(mStartAttributes[i], "StartAttribute" + MyGUI::utility::toString(i));
            mStartAttributes[i]->setAttributeId(i);
            ToolTips::createAttributeToolTip(mStartAttributes[i], i);
        }

        // Class page.
        getWidget(mClassList, "ClassList");
        getWidget(mClassImage, "ClassImage");
        getWidget(mClassName, "ClassName");
        getWidget(mSpecializationName, "SpecializationName");
        mClassList->setScrollVisible(true);
        mClassList->eventListChangePosition += MyGUI::newDelegate(this, &RaceDialog::onSelectClass);
        mSpecializationName->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSpecializationClicked);
        getWidget(button, "RandomCombatButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onRandomCombatClass);
        getWidget(button, "RandomMagicButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onRandomMagicClass);
        getWidget(button, "RandomStealthButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onRandomStealthClass);
        getWidget(button, "RandomAnyButton");
        button->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onRandomAnyClass);

        for (int i = 0; i < 2; ++i)
        {
            getWidget(mFavoriteAttribute[i], "FavoriteAttribute" + MyGUI::utility::toString(i));
            mFavoriteAttribute[i]->eventClicked += MyGUI::newDelegate(this, &RaceDialog::onAttributeClicked);
        }
        for (int i = 0; i < 5; ++i)
        {
            getWidget(mMajorSkill[i], "MajorSkill" + MyGUI::utility::toString(i));
            getWidget(mMinorSkill[i], "MinorSkill" + MyGUI::utility::toString(i));
            mMajorSkill[i]->eventClicked += MyGUI::newDelegate(this, &RaceDialog::onSkillClicked);
            mMinorSkill[i]->eventClicked += MyGUI::newDelegate(this, &RaceDialog::onSkillClicked);
        }

        // Birthsign page.
        getWidget(mBirthList, "BirthsignList");
        getWidget(mBirthImage, "BirthsignImage");
        getWidget(mSpellArea, "SpellArea");
        mBirthList->setScrollVisible(true);
        mBirthList->eventListChangePosition += MyGUI::newDelegate(this, &RaceDialog::onSelectBirth);

        updateRaces();
        updateClasses();
        updateBirths();
        updatePageVisibility();
    }

    RaceDialog::~RaceDialog()
    {
        if (mSpecDialog)
            MWBase::Environment::get().getWindowManager()->removeDialog(mSpecDialog);
        if (mAttribDialog)
            MWBase::Environment::get().getWindowManager()->removeDialog(mAttribDialog);
        if (mSkillDialog)
            MWBase::Environment::get().getWindowManager()->removeDialog(mSkillDialog);
    }

    void RaceDialog::setNextButtonShow(bool)
    {
        // Compatibility no-op. The modern shell owns the Back/Next captions.
    }

    void RaceDialog::setPlayerScale(float value)
    {
        mPlayerScale = std::max(0.85f, std::min(1.15f, value));
        if (mScale)
        {
            const size_t position = static_cast<size_t>(std::lround((mPlayerScale - 0.85f) / 0.005f));
            mScale->setScrollPosition(std::min(position, mScale->getScrollRange() - 1));
        }
        if (mPreview)
            mPreview->setUserScale(mPlayerScale);

        // Height changes should never leave a tall head or short feet outside the RTT.
        // Re-fit the whole body; the player can zoom in again afterwards.
        resetPreviewView(false);
    }

    void RaceDialog::setGender(Gender gender)
    {
        mGenderIndex = gender == GM_Male ? 0 : 1;
        mHairIndex = 0;
        recountParts();
        updateAppearanceStats();
        resetPreviewView(false);
        updatePreview();
    }

    void RaceDialog::setRaceId(const std::string& raceId)
    {
        if (raceId.empty())
            return;

        for (size_t i = 0; i < mRaceIds.size(); ++i)
        {
            if (Misc::StringUtils::ciEqual(mRaceIds[i], raceId))
            {
                mRaceIndex = static_cast<int>(i);
                mCurrentRaceId = mRaceIds[i];
                recountParts();
                updateRaceName();
                updateAppearanceStats();
                resetPreviewView(false);
                updatePreview();
                return;
            }
        }
    }

    void RaceDialog::setClass(const ESM::Class& klass)
    {
        if (!klass.mName.empty() || !klass.mId.empty())
        {
            mClassResult = klass;
            mCurrentClassId = klass.mId;
            if (!klass.mId.empty())
                mClassImageId = klass.mId;
            mCustomClass = klass.mId.empty();
            updateClassStats();
        }
    }

    void RaceDialog::setClassImageId(const std::string& classId)
    {
        if (classId.empty())
            return;
        mClassImageId = classId;
        updateClassStats();
    }

    void RaceDialog::setBirthId(const std::string& birthId)
    {
        mCurrentBirthId = birthId;
        if (!mBirthList)
            return;

        mBirthList->setIndexSelected(MyGUI::ITEM_NONE);
        for (size_t i = 0; i < mBirthList->getItemCount(); ++i)
        {
            const std::string* id = mBirthList->getItemDataAt<std::string>(i);
            if (id && Misc::StringUtils::ciEqual(*id, birthId))
            {
                mBirthList->setIndexSelected(i);
                break;
            }
        }
        updateBirthSpells();
    }

    void RaceDialog::setPage(Page page)
    {
        mPage = page;
        updatePageVisibility();
        updatePageHeader();
    }

    void RaceDialog::updatePageVisibility()
    {
        if (!mAppearancePage)
            return;
        mAppearancePage->setVisible(mPage == Page_Appearance);
        mClassPage->setVisible(mPage == Page_Class);
        mBirthPage->setVisible(mPage == Page_Birth);
        // In ArenaMP the account name is already bound before Appearance starts,
        // so Page_Appearance must not navigate back to the login/account card.
        mBackButton->setVisible(mPage != Page_Appearance);
        mNextButton->setCaption(mPage == Page_Birth
            ? MWBase::Environment::get().getWindowManager()->getGameSettingString("sOK", "OK")
            : MWBase::Environment::get().getWindowManager()->getGameSettingString("sNext", "Next"));
    }

    void RaceDialog::updatePageHeader()
    {
        if (!mStepTitle)
            return;
        static const char* titles[] = {
            "#{arenamp=chargen.appearance}",
            "#{arenamp=chargen.class}",
            "#{arenamp=chargen.birthsign}"
        };
        mStepTitle->setCaptionWithReplacing(titles[static_cast<int>(mPage)]);
        mStepCounter->setCaption(MyGUI::utility::toString(static_cast<int>(mPage) + 1) + " / 3");
    }

    void RaceDialog::onOpen()
    {
        WindowModal::onOpen();

        updateRaces();
        updateClasses();
        updateBirths();

        if (mPage == Page_Appearance && !mPreview)
        {
            mPreview.reset(new MWRender::RaceSelectionPreview(mParent, mResourceSystem));
            mPreview->rebuild();
            mPreview->setAngle(mCurrentAngle);
            mPreview->setUserScale(mPlayerScale);
            mPreview->setViewZoom(mViewZoom);
            mPreview->setViewOffset(mViewOffsetX, mViewOffsetZ);
            mPreviewTexture.reset(new osgMyGUI::OSGTexture(mPreview->getTexture()));
            mPreviewImage->setRenderItemTexture(mPreviewTexture.get());
            mPreviewImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));

            const ESM::NPC& proto = mPreview->getPrototype();
            if (mCurrentRaceId.empty())
                setRaceId(proto.mRace);
            mGenderIndex = proto.isMale() ? 0 : 1;
            recountParts();
            for (size_t i = 0; i < mAvailableHeads.size(); ++i)
                if (Misc::StringUtils::ciEqual(mAvailableHeads[i], proto.mHead))
                    mFaceIndex = static_cast<int>(i);
            for (size_t i = 0; i < mAvailableHairs.size(); ++i)
                if (Misc::StringUtils::ciEqual(mAvailableHairs[i], proto.mHair))
                    mHairIndex = static_cast<int>(i);
            updatePreview();
        }
        else if (mPage == Page_Appearance && mPreview)
        {
            mPreviewImage->setRenderItemTexture(mPreviewTexture.get());
            mPreview->redraw();
        }

        updateAppearanceStats();
        updateClassStats();
        updateBirthSpells();
        updatePageVisibility();
        updatePageHeader();

        if (mPage == Page_Appearance)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mScale);
        else if (mPage == Page_Class)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mClassList);
        else
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mBirthList);
    }

    void RaceDialog::onClose()
    {
        WindowModal::onClose();
        if (mPreviewImage)
            mPreviewImage->setRenderItemTexture(nullptr);
        mPreviewTexture.reset();
        mPreview.reset();
    }

    void RaceDialog::onBackClicked(MyGUI::Widget*)
    {
        eventBack();
    }

    void RaceDialog::onNextClicked(MyGUI::Widget*)
    {
        if (mPage == Page_Appearance && mCurrentRaceId.empty())
            return;
        if (mPage == Page_Class && mClassResult.mName.empty())
            return;
        if (mPage == Page_Birth && mCurrentBirthId.empty())
            return;
        eventDone(this);
    }

    void RaceDialog::onFrame(float duration)
    {
        if (mPage == Page_Appearance && mPreview)
            mPreview->update(duration);
    }

    void RaceDialog::onPreviewMousePressed(MyGUI::Widget*, int left, int top, MyGUI::MouseButton)
    {
        mPreviewDragX = left;
        mPreviewDragY = top;
    }

    void RaceDialog::onPreviewMouseDrag(MyGUI::Widget*, int left, int top, MyGUI::MouseButton id)
    {
        const int dx = left - mPreviewDragX;
        const int dy = top - mPreviewDragY;
        mPreviewDragX = left;
        mPreviewDragY = top;

        if (id == MyGUI::MouseButton::Left)
        {
            mCurrentAngle += static_cast<float>(dx) * 0.012f;
            if (mPreview)
                mPreview->setAngle(mCurrentAngle);
        }
        else if (id == MyGUI::MouseButton::Right)
        {
            mViewOffsetX = std::max(-45.f, std::min(45.f, mViewOffsetX - static_cast<float>(dx) * 0.22f));
            mViewOffsetZ = std::max(-40.f, std::min(40.f, mViewOffsetZ + static_cast<float>(dy) * 0.22f));
            if (mPreview)
                mPreview->setViewOffset(mViewOffsetX, mViewOffsetZ);
        }
    }

    void RaceDialog::onPreviewMouseWheel(MyGUI::Widget*, int rel)
    {
        if (rel == 0)
            return;
        mViewZoom = std::max(0.85f, std::min(2.35f, mViewZoom + (rel > 0 ? 0.12f : -0.12f)));
        if (mPreview)
            mPreview->setViewZoom(mViewZoom);
    }

    void RaceDialog::onScaleChanged(MyGUI::ScrollBar*, size_t position)
    {
        setPlayerScale(0.85f + static_cast<float>(position) * 0.005f);
    }

    void RaceDialog::updateRaces()
    {
        const std::string old = mCurrentRaceId;
        mRaceIds.clear();
        mRaceNames.clear();

        std::vector<std::pair<std::string, std::string>> races;
        for (const ESM::Race& race : MWBase::Environment::get().getWorld()->getStore().get<ESM::Race>())
        {
            if (race.mData.mFlags & ESM::Race::Playable)
                races.emplace_back(race.mId, race.mName);
        }
        std::sort(races.begin(), races.end(), sortBySecond);
        for (const auto& race : races)
        {
            mRaceIds.push_back(race.first);
            mRaceNames.push_back(race.second);
        }

        if (mRaceIds.empty())
            return;

        mRaceIndex = 0;
        for (size_t i = 0; i < mRaceIds.size(); ++i)
            if (!old.empty() && Misc::StringUtils::ciEqual(mRaceIds[i], old))
                mRaceIndex = static_cast<int>(i);
        mCurrentRaceId = mRaceIds[mRaceIndex];
        updateRaceName();
    }

    void RaceDialog::updateRaceName()
    {
        if (!mRaceValue || mRaceIds.empty())
            return;
        mRaceValue->setCaption(mRaceNames[mRaceIndex]);
        mGenderValue->setCaptionWithReplacing(mGenderIndex == 0
            ? "#{arenamp=chargen.gender_male}"
            : "#{arenamp=chargen.gender_female}");
        mFaceValue->setCaption(mAvailableHeads.empty()
            ? "-" : MyGUI::utility::toString(mFaceIndex + 1) + " / " + MyGUI::utility::toString(mAvailableHeads.size()));
        mHairValue->setCaption(mAvailableHairs.empty()
            ? "-" : MyGUI::utility::toString(mHairIndex + 1) + " / " + MyGUI::utility::toString(mAvailableHairs.size()));
    }

    void RaceDialog::getBodyParts(int part, std::vector<std::string>& out)
    {
        out.clear();
        const MWWorld::Store<ESM::BodyPart>& store
            = MWBase::Environment::get().getWorld()->getStore().get<ESM::BodyPart>();

        std::vector<std::string> preferredHair;
        std::vector<std::string> alternateHair;
        const bool femaleActor = mGenderIndex != 0;

        for (const ESM::BodyPart& bodypart : store)
        {
            if (bodypart.mData.mFlags & ESM::BodyPart::BPF_NotPlayable)
                continue;
            if (bodypart.mData.mType != ESM::BodyPart::MT_Skin)
                continue;
            if (bodypart.mData.mPart != static_cast<ESM::BodyPart::MeshPart>(part))
                continue;

            const bool femalePart = (bodypart.mData.mFlags & ESM::BodyPart::BPF_Female) != 0;
            // Heads stay sex-bound. Hairstyles remain available to both sexes, but
            // the actor's own sex is shown first so the initial choices feel natural.
            if (part != ESM::BodyPart::MP_Hair && femaleActor != femalePart)
                continue;

            const bool firstPerson = bodypart.mId.size() >= 3
                && bodypart.mId[bodypart.mId.size() - 3] == '1'
                && bodypart.mId[bodypart.mId.size() - 2] == 's'
                && bodypart.mId[bodypart.mId.size() - 1] == 't';
            if (firstPerson || !Misc::StringUtils::ciEqual(bodypart.mRace, mCurrentRaceId))
                continue;

            if (part == ESM::BodyPart::MP_Hair)
            {
                if (femaleActor == femalePart)
                    preferredHair.push_back(bodypart.mId);
                else
                    alternateHair.push_back(bodypart.mId);
            }
            else
                out.push_back(bodypart.mId);
        }

        if (part == ESM::BodyPart::MP_Hair)
        {
            out.insert(out.end(), preferredHair.begin(), preferredHair.end());
            out.insert(out.end(), alternateHair.begin(), alternateHair.end());
        }
    }

    void RaceDialog::recountParts()
    {
        getBodyParts(ESM::BodyPart::MP_Head, mAvailableHeads);
        getBodyParts(ESM::BodyPart::MP_Hair, mAvailableHairs);
        mFaceIndex = wrap(mFaceIndex, static_cast<int>(mAvailableHeads.size()));
        mHairIndex = wrap(mHairIndex, static_cast<int>(mAvailableHairs.size()));
        updateRaceName();
    }

    void RaceDialog::updateAppearanceStats()
    {
        if (mCurrentRaceId.empty())
            return;
        const ESM::Race* race = MWBase::Environment::get().getWorld()->getStore().get<ESM::Race>().search(mCurrentRaceId);
        if (!race)
            return;

        for (int i = 0; i < 8; ++i)
        {
            float base = static_cast<float>(race->mData.mAttributeValues[i].getValue(mGenderIndex == 0));
            if (!mClassResult.mName.empty()
                && (mClassResult.mData.mAttribute[0] == i || mClassResult.mData.mAttribute[1] == i))
                base += 10.f;

            MWMechanics::AttributeValue value;
            value.setBase(base);
            mStartAttributes[i]->setAttributeId(i);
            mStartAttributes[i]->setAttributeValue(value);
        }
        updateRaceName();
    }

    void RaceDialog::resetPreviewView(bool headFocus)
    {
        // Keep one stable bust framing for Race / Sex / Face / Hair changes.
        // The automatic view always returns to the whole head, shoulders, both arms
        // and upper torso; the wheel remains the explicit way to inspect details.
        (void)headFocus;
        // Default CharGen bust framing: keep the whole head, shoulders, both arms
        // and the upper torso visible for every playable race. Any manual pan/zoom is
        // intentionally discarded after Race / Sex / Face / Hair / Height changes.
        mViewZoom = 1.58f;
        mViewOffsetX = 0.f;
        mViewOffsetZ = 10.f;
        if (mPreview)
        {
            mPreview->setViewZoom(mViewZoom);
            mPreview->setViewOffset(mViewOffsetX, mViewOffsetZ);
        }
    }

    void RaceDialog::updatePreview()
    {
        if (!mPreview || mCurrentRaceId.empty())
            return;

        ESM::NPC record = mPreview->getPrototype();
        record.mRace = mCurrentRaceId;
        record.setIsMale(mGenderIndex == 0);
        if (!mAvailableHeads.empty())
            record.mHead = mAvailableHeads[wrap(mFaceIndex, static_cast<int>(mAvailableHeads.size()))];
        if (!mAvailableHairs.empty())
            record.mHair = mAvailableHairs[wrap(mHairIndex, static_cast<int>(mAvailableHairs.size()))];

        try
        {
            mPreview->setPrototype(record);
            mPreview->setUserScale(mPlayerScale);
            mPreview->setViewZoom(mViewZoom);
            mPreview->setViewOffset(mViewOffsetX, mViewOffsetZ);
            mPreview->setAngle(mCurrentAngle);
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Error creating full-body chargen preview: " << e.what();
        }
        updateRaceName();
    }

    void RaceDialog::onSelectPreviousRace(MyGUI::Widget*)
    {
        if (mRaceIds.empty())
            return;
        mRaceIndex = wrap(mRaceIndex - 1, static_cast<int>(mRaceIds.size()));
        mCurrentRaceId = mRaceIds[mRaceIndex];
        mFaceIndex = 0;
        mHairIndex = 0;
        recountParts();
        updateAppearanceStats();
        resetPreviewView(false);
        updatePreview();
    }

    void RaceDialog::onSelectNextRace(MyGUI::Widget*)
    {
        if (mRaceIds.empty())
            return;
        mRaceIndex = wrap(mRaceIndex + 1, static_cast<int>(mRaceIds.size()));
        mCurrentRaceId = mRaceIds[mRaceIndex];
        mFaceIndex = 0;
        mHairIndex = 0;
        recountParts();
        updateAppearanceStats();
        resetPreviewView(false);
        updatePreview();
    }

    void RaceDialog::onSelectPreviousGender(MyGUI::Widget*)
    {
        mGenderIndex = wrap(mGenderIndex - 1, 2);
        mFaceIndex = 0;
        mHairIndex = 0;
        recountParts();
        updateAppearanceStats();
        resetPreviewView(false);
        updatePreview();
    }

    void RaceDialog::onSelectNextGender(MyGUI::Widget*)
    {
        mGenderIndex = wrap(mGenderIndex + 1, 2);
        mFaceIndex = 0;
        mHairIndex = 0;
        recountParts();
        updateAppearanceStats();
        resetPreviewView(false);
        updatePreview();
    }

    void RaceDialog::onSelectPreviousFace(MyGUI::Widget*)
    {
        mFaceIndex = wrap(mFaceIndex - 1, static_cast<int>(mAvailableHeads.size()));
        resetPreviewView(false);
        updatePreview();
    }

    void RaceDialog::onSelectNextFace(MyGUI::Widget*)
    {
        mFaceIndex = wrap(mFaceIndex + 1, static_cast<int>(mAvailableHeads.size()));
        resetPreviewView(false);
        updatePreview();
    }

    void RaceDialog::onSelectPreviousHair(MyGUI::Widget*)
    {
        mHairIndex = wrap(mHairIndex - 1, static_cast<int>(mAvailableHairs.size()));
        resetPreviewView(false);
        updatePreview();
    }

    void RaceDialog::onSelectNextHair(MyGUI::Widget*)
    {
        mHairIndex = wrap(mHairIndex + 1, static_cast<int>(mAvailableHairs.size()));
        resetPreviewView(false);
        updatePreview();
    }

    void RaceDialog::updateClasses()
    {
        if (!mClassList)
            return;
        mClassList->removeAllItems();

        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        std::vector<std::pair<std::string, std::string>> items;
        for (const ESM::Class& classInfo : store.get<ESM::Class>())
        {
            if (!classInfo.mData.mIsPlayable || store.get<ESM::Class>().isDynamic(classInfo.mId))
                continue;
            items.emplace_back(classInfo.mId, classInfo.mName);
        }
        std::sort(items.begin(), items.end(), sortBySecond);

        size_t selected = MyGUI::ITEM_NONE;
        for (const auto& item : items)
        {
            const size_t index = mClassList->getItemCount();
            mClassList->addItem(item.second, item.first);
            if (!mCustomClass && !mCurrentClassId.empty() && Misc::StringUtils::ciEqual(item.first, mCurrentClassId))
                selected = index;
        }

        if (!mCustomClass && selected == MyGUI::ITEM_NONE && mClassList->getItemCount())
        {
            selected = 0;
            const std::string* id = mClassList->getItemDataAt<std::string>(0);
            if (id)
                mCurrentClassId = *id;
        }
        mClassList->setIndexSelected(selected);
        if (!mCustomClass)
            loadCurrentClass();
    }

    void RaceDialog::loadCurrentClass()
    {
        if (mCurrentClassId.empty())
            return;
        const ESM::Class* klass
            = MWBase::Environment::get().getWorld()->getStore().get<ESM::Class>().search(mCurrentClassId);
        if (!klass)
            return;
        mClassResult = *klass;
        mClassImageId = klass->mId;
        mCustomClass = false;
        updateClassStats();
        updateAppearanceStats();
    }

    void RaceDialog::onSelectClass(MyGUI::ListBox*, size_t index)
    {
        if (index == MyGUI::ITEM_NONE)
            return;
        const std::string* id = mClassList->getItemDataAt<std::string>(index);
        if (!id)
            return;
        mCurrentClassId = *id;
        loadCurrentClass();
    }

    void RaceDialog::randomizeClass(ESM::Class::Specialization specialization)
    {
        // ArenaMW archetype generator. These buttons intentionally do NOT select a
        // premade class. They build a fresh Adventurer with a coherent but random
        // combination of favored attributes, Major skills and Minor skills.
        std::vector<int> primarySkills;
        std::vector<int> supportSkills;
        std::vector<int> favoredAttributes;
        const char* preferredImageClass = nullptr;

        switch (specialization)
        {
            case ESM::Class::Combat:
                primarySkills = { ESM::Skill::Block, ESM::Skill::Armorer, ESM::Skill::MediumArmor,
                    ESM::Skill::HeavyArmor, ESM::Skill::BluntWeapon, ESM::Skill::LongBlade,
                    ESM::Skill::Axe, ESM::Skill::Spear, ESM::Skill::Athletics };
                supportSkills = { ESM::Skill::Marksman, ESM::Skill::HandToHand, ESM::Skill::Acrobatics,
                    ESM::Skill::Restoration, ESM::Skill::Alchemy, ESM::Skill::Security,
                    ESM::Skill::Mercantile, ESM::Skill::Speechcraft, ESM::Skill::Unarmored };
                favoredAttributes = { ESM::Attribute::Strength, ESM::Attribute::Endurance,
                    ESM::Attribute::Agility, ESM::Attribute::Speed, ESM::Attribute::Luck };
                preferredImageClass = "Warrior";
                break;

            case ESM::Class::Magic:
                primarySkills = { ESM::Skill::Enchant, ESM::Skill::Destruction, ESM::Skill::Alteration,
                    ESM::Skill::Illusion, ESM::Skill::Conjuration, ESM::Skill::Mysticism,
                    ESM::Skill::Restoration, ESM::Skill::Alchemy, ESM::Skill::Unarmored };
                supportSkills = { ESM::Skill::BluntWeapon, ESM::Skill::ShortBlade, ESM::Skill::Athletics,
                    ESM::Skill::Speechcraft, ESM::Skill::Mercantile, ESM::Skill::LightArmor,
                    ESM::Skill::Sneak, ESM::Skill::Security, ESM::Skill::HandToHand };
                favoredAttributes = { ESM::Attribute::Intelligence, ESM::Attribute::Willpower,
                    ESM::Attribute::Personality, ESM::Attribute::Luck, ESM::Attribute::Endurance };
                preferredImageClass = "Mage";
                break;

            case ESM::Class::Stealth:
                primarySkills = { ESM::Skill::Security, ESM::Skill::Sneak, ESM::Skill::Acrobatics,
                    ESM::Skill::LightArmor, ESM::Skill::ShortBlade, ESM::Skill::Marksman,
                    ESM::Skill::Mercantile, ESM::Skill::Speechcraft, ESM::Skill::HandToHand };
                supportSkills = { ESM::Skill::Athletics, ESM::Skill::Alchemy, ESM::Skill::Illusion,
                    ESM::Skill::Mysticism, ESM::Skill::Restoration, ESM::Skill::LongBlade,
                    ESM::Skill::Spear, ESM::Skill::Unarmored, ESM::Skill::Enchant };
                favoredAttributes = { ESM::Attribute::Agility, ESM::Attribute::Speed,
                    ESM::Attribute::Personality, ESM::Attribute::Luck, ESM::Attribute::Strength };
                preferredImageClass = "Thief";
                break;
        }

        auto takeRandom = [](std::vector<int>& pool, std::vector<int>& target, int count)
        {
            for (int i = 0; i < count && !pool.empty(); ++i)
            {
                const int index = Misc::Rng::rollDice(static_cast<int>(pool.size()));
                target.push_back(pool[static_cast<std::size_t>(index)]);
                pool.erase(pool.begin() + index);
            }
        };

        auto shuffleSkills = [](std::vector<int>& values)
        {
            for (std::size_t i = values.size(); i > 1; --i)
            {
                const std::size_t other = static_cast<std::size_t>(Misc::Rng::rollDice(static_cast<int>(i)));
                std::swap(values[i - 1], values[other]);
            }
        };

        std::vector<int> majorSkills;
        std::vector<int> minorSkills;
        takeRandom(primarySkills, majorSkills, 4);
        takeRandom(supportSkills, majorSkills, 1);
        takeRandom(primarySkills, minorSkills, 3);
        takeRandom(supportSkills, minorSkills, 2);
        shuffleSkills(majorSkills);
        shuffleSkills(minorSkills);

        if (majorSkills.size() != 5 || minorSkills.size() != 5 || favoredAttributes.size() < 2)
            return;

        const int firstAttributeIndex = Misc::Rng::rollDice(static_cast<int>(favoredAttributes.size()));
        const int firstAttribute = favoredAttributes[static_cast<std::size_t>(firstAttributeIndex)];
        favoredAttributes.erase(favoredAttributes.begin() + firstAttributeIndex);
        const int secondAttribute
            = favoredAttributes[static_cast<std::size_t>(Misc::Rng::rollDice(static_cast<int>(favoredAttributes.size())))];

        mClassResult.blank();
        mClassResult.mId.clear();
        mClassResult.mName = MWBase::Environment::get().getWindowManager()->getGameSettingString(
            "sCustomClassName", "Adventurer");
        mClassResult.mData.mSpecialization = specialization;
        mClassResult.mData.mAttribute[0] = firstAttribute;
        mClassResult.mData.mAttribute[1] = secondAttribute;
        mClassResult.mData.mIsPlayable = 1;

        for (int i = 0; i < 5; ++i)
        {
            mClassResult.mData.mSkills[i][1] = majorSkills[static_cast<std::size_t>(i)];
            mClassResult.mData.mSkills[i][0] = minorSkills[static_cast<std::size_t>(i)];
        }

        mCurrentClassId.clear();
        mCustomClass = true;
        if (mClassList)
            mClassList->setIndexSelected(MyGUI::ITEM_NONE);

        // Keep an archetype illustration as visual context only. No data is read
        // from this class into the generated Adventurer.
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const ESM::Class* imageClass = preferredImageClass
            ? store.get<ESM::Class>().search(preferredImageClass)
            : nullptr;
        if (imageClass)
            mClassImageId = imageClass->mId;
        else
        {
            for (const ESM::Class& klass : store.get<ESM::Class>())
            {
                if (klass.mData.mIsPlayable && !store.get<ESM::Class>().isDynamic(klass.mId)
                    && klass.mData.mSpecialization == specialization)
                {
                    mClassImageId = klass.mId;
                    break;
                }
            }
        }

        updateClassStats();
        updateAppearanceStats();
    }

    void RaceDialog::onRandomCombatClass(MyGUI::Widget*)
    {
        randomizeClass(ESM::Class::Combat);
    }

    void RaceDialog::onRandomMagicClass(MyGUI::Widget*)
    {
        randomizeClass(ESM::Class::Magic);
    }

    void RaceDialog::onRandomStealthClass(MyGUI::Widget*)
    {
        randomizeClass(ESM::Class::Stealth);
    }

    void RaceDialog::onRandomAnyClass(MyGUI::Widget*)
    {
        const int roll = Misc::Rng::rollDice(3);
        randomizeClass(roll == 0 ? ESM::Class::Combat
            : (roll == 1 ? ESM::Class::Magic : ESM::Class::Stealth));
    }

    void RaceDialog::updateClassStats()
    {
        if (!mSpecializationName || mClassResult.mName.empty())
            return;

        const int specialization = std::max(0, std::min(2, mClassResult.mData.mSpecialization));
        static const char* specIds[] = { "sSpecializationCombat", "sSpecializationMagic", "sSpecializationStealth" };
        const std::string specName = MWBase::Environment::get().getWindowManager()->getGameSettingString(
            specIds[specialization], specIds[specialization]);
        mSpecializationName->setCaption(specName);
        ToolTips::createSpecializationToolTip(mSpecializationName, specName,
            static_cast<ESM::Class::Specialization>(specialization));

        mClassName->setCaption(mClassResult.mName);
        // Manual editing changes the logical profile to Adventurer, but the last
        // selected class illustration is deliberately kept as visual context.
        if (!mClassImageId.empty())
            setClassImage(mClassImage, mClassImageId);
        else
            mClassImage->setImageTexture("");

        for (int i = 0; i < 2; ++i)
        {
            mFavoriteAttribute[i]->setAttributeId(mClassResult.mData.mAttribute[i]);
            ToolTips::createAttributeToolTip(mFavoriteAttribute[i], mClassResult.mData.mAttribute[i]);
        }
        for (int i = 0; i < 5; ++i)
        {
            mMinorSkill[i]->setSkillNumber(mClassResult.mData.mSkills[i][0]);
            mMajorSkill[i]->setSkillNumber(mClassResult.mData.mSkills[i][1]);
            ToolTips::createSkillToolTip(mMinorSkill[i], mClassResult.mData.mSkills[i][0]);
            ToolTips::createSkillToolTip(mMajorSkill[i], mClassResult.mData.mSkills[i][1]);
        }
    }

    void RaceDialog::promoteToCustomClass()
    {
        if (!mCustomClass)
        {
            mCustomClass = true;
            mCurrentClassId.clear();
            mClassResult.mId.clear();
            mClassResult.mName = MWBase::Environment::get().getWindowManager()->getGameSettingString(
                "sCustomClassName", "Adventurer");
            mClassResult.mData.mIsPlayable = 1;
            mClassList->setIndexSelected(MyGUI::ITEM_NONE);
        }
        updateClassStats();
        updateAppearanceStats();
    }

    void RaceDialog::onSpecializationClicked(MyGUI::Widget*)
    {
        if (mSpecDialog)
            return;
        mSpecDialog = new SelectSpecializationDialog();
        mSpecDialog->eventCancel += MyGUI::newDelegate(this, &RaceDialog::onSubDialogCancel);
        mSpecDialog->eventItemSelected += MyGUI::newDelegate(this, &RaceDialog::onSpecializationSelected);
        mSpecDialog->setVisible(true);
    }

    void RaceDialog::onSpecializationSelected()
    {
        mClassResult.mData.mSpecialization = mSpecDialog->getSpecializationId();
        MWBase::Environment::get().getWindowManager()->removeDialog(mSpecDialog);
        mSpecDialog = nullptr;
        promoteToCustomClass();
    }

    void RaceDialog::onAttributeClicked(Widgets::MWAttributePtr sender)
    {
        if (mAttribDialog)
            return;
        mAffectedAttribute = sender;
        mAttribDialog = new SelectAttributeDialog();
        mAttribDialog->eventCancel += MyGUI::newDelegate(this, &RaceDialog::onSubDialogCancel);
        mAttribDialog->eventItemSelected += MyGUI::newDelegate(this, &RaceDialog::onAttributeSelected);
        mAttribDialog->setVisible(true);
    }

    void RaceDialog::onAttributeSelected()
    {
        const int id = mAttribDialog->getAttributeId();
        const int index = mAffectedAttribute == mFavoriteAttribute[1] ? 1 : 0;
        const int other = 1 - index;
        if (mClassResult.mData.mAttribute[other] == id)
            std::swap(mClassResult.mData.mAttribute[index], mClassResult.mData.mAttribute[other]);
        else
            mClassResult.mData.mAttribute[index] = id;

        MWBase::Environment::get().getWindowManager()->removeDialog(mAttribDialog);
        mAttribDialog = nullptr;
        mAffectedAttribute = nullptr;
        promoteToCustomClass();
    }

    void RaceDialog::onSkillClicked(Widgets::MWSkillPtr sender)
    {
        if (mSkillDialog)
            return;
        mAffectedSkill = sender;
        mSkillDialog = new SelectSkillDialog();
        mSkillDialog->eventCancel += MyGUI::newDelegate(this, &RaceDialog::onSubDialogCancel);
        mSkillDialog->eventItemSelected += MyGUI::newDelegate(this, &RaceDialog::onSkillSelected);
        mSkillDialog->setVisible(true);
    }

    void RaceDialog::onSkillSelected()
    {
        const int newId = mSkillDialog->getSkillId();
        int* affected = nullptr;
        int oldId = -1;
        for (int i = 0; i < 5; ++i)
        {
            if (mAffectedSkill == mMinorSkill[i])
                affected = &mClassResult.mData.mSkills[i][0];
            else if (mAffectedSkill == mMajorSkill[i])
                affected = &mClassResult.mData.mSkills[i][1];
        }
        if (affected)
        {
            oldId = *affected;
            for (int i = 0; i < 5; ++i)
            {
                for (int column = 0; column < 2; ++column)
                {
                    int* slot = &mClassResult.mData.mSkills[i][column];
                    if (slot != affected && *slot == newId)
                    {
                        *slot = oldId;
                        break;
                    }
                }
            }
            *affected = newId;
        }

        MWBase::Environment::get().getWindowManager()->removeDialog(mSkillDialog);
        mSkillDialog = nullptr;
        mAffectedSkill = nullptr;
        promoteToCustomClass();
    }

    void RaceDialog::onSubDialogCancel()
    {
        if (mSpecDialog)
        {
            MWBase::Environment::get().getWindowManager()->removeDialog(mSpecDialog);
            mSpecDialog = nullptr;
        }
        if (mAttribDialog)
        {
            MWBase::Environment::get().getWindowManager()->removeDialog(mAttribDialog);
            mAttribDialog = nullptr;
            mAffectedAttribute = nullptr;
        }
        if (mSkillDialog)
        {
            MWBase::Environment::get().getWindowManager()->removeDialog(mSkillDialog);
            mSkillDialog = nullptr;
            mAffectedSkill = nullptr;
        }
    }

    void RaceDialog::updateBirths()
    {
        if (!mBirthList)
            return;
        const std::string old = mCurrentBirthId;
        mBirthList->removeAllItems();

        std::vector<std::pair<std::string, const ESM::BirthSign*>> signs;
        for (const ESM::BirthSign& sign : MWBase::Environment::get().getWorld()->getStore().get<ESM::BirthSign>())
            signs.emplace_back(sign.mId, &sign);
        std::sort(signs.begin(), signs.end(), sortBirthSigns);

        size_t selected = MyGUI::ITEM_NONE;
        for (const auto& sign : signs)
        {
            const size_t index = mBirthList->getItemCount();
            mBirthList->addItem(sign.second->mName, sign.first);
            if (!old.empty() && Misc::StringUtils::ciEqual(old, sign.first))
                selected = index;
        }
        if (selected == MyGUI::ITEM_NONE && mBirthList->getItemCount())
        {
            selected = 0;
            const std::string* id = mBirthList->getItemDataAt<std::string>(0);
            if (id)
                mCurrentBirthId = *id;
        }
        mBirthList->setIndexSelected(selected);
        updateBirthSpells();
    }

    void RaceDialog::onSelectBirth(MyGUI::ListBox*, size_t index)
    {
        if (index == MyGUI::ITEM_NONE)
            return;
        const std::string* id = mBirthList->getItemDataAt<std::string>(index);
        if (!id)
            return;
        mCurrentBirthId = *id;
        updateBirthSpells();
    }

    void RaceDialog::updateBirthSpells()
    {
        if (!mSpellArea)
            return;
        for (MyGUI::Widget* widget : mSpellItems)
            MyGUI::Gui::getInstance().destroyWidget(widget);
        mSpellItems.clear();

        if (mCurrentBirthId.empty())
            return;
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const ESM::BirthSign* birth = store.get<ESM::BirthSign>().search(mCurrentBirthId);
        if (!birth)
            return;

        mBirthImage->setImageTexture(MWBase::Environment::get().getWindowManager()->correctTexturePath(birth->mTexture));

        std::vector<std::string> abilities;
        std::vector<std::string> powers;
        std::vector<std::string> spells;
        for (const std::string& spellId : birth->mPowers.mList)
        {
            const ESM::Spell* spell = store.get<ESM::Spell>().search(spellId);
            if (!spell)
                continue;
            const ESM::Spell::SpellType type = static_cast<ESM::Spell::SpellType>(spell->mData.mType);
            if (type == ESM::Spell::ST_Ability)
                abilities.push_back(spellId);
            else if (type == ESM::Spell::ST_Power)
                powers.push_back(spellId);
            else if (type == ESM::Spell::ST_Spell)
                spells.push_back(spellId);
        }

        MyGUI::IntCoord coord(0, 0, mSpellArea->getWidth(), 18);
        int widgetIndex = 0;
        struct Category
        {
            const std::vector<std::string>* mSpells;
            const char* mLabel;
            bool mConstant;
        };
        const Category categories[] = {
            { &abilities, "sBirthsignmenu1", true },
            { &powers, "sPowers", false },
            { &spells, "sBirthsignmenu2", false }
        };

        for (const Category& category : categories)
        {
            if (category.mSpells->empty())
                continue;
            MyGUI::TextBox* label = mSpellArea->createWidget<MyGUI::TextBox>(
                "SandBrightText", coord, MyGUI::Align::Default, "BirthCategory" + MyGUI::utility::toString(widgetIndex++));
            label->setCaption(MWBase::Environment::get().getWindowManager()->getGameSettingString(category.mLabel, category.mLabel));
            mSpellItems.push_back(label);
            coord.top += 20;

            for (const std::string& spellId : *category.mSpells)
            {
                Widgets::MWSpellPtr spellWidget = mSpellArea->createWidget<Widgets::MWSpell>(
                    "MW_StatName", coord, MyGUI::Align::Default, "BirthSpell" + MyGUI::utility::toString(widgetIndex++));
                spellWidget->setSpellId(spellId);
                mSpellItems.push_back(spellWidget);
                coord.top += 18;
                MyGUI::IntCoord effectCoord = coord;
                effectCoord.height = 24;
                spellWidget->createEffectWidgets(mSpellItems, mSpellArea, effectCoord,
                    category.mConstant ? Widgets::MWEffectList::EF_Constant : 0);
                coord.top = effectCoord.top;
            }
        }

        mSpellArea->setVisibleVScroll(false);
        mSpellArea->setCanvasSize(MyGUI::IntSize(mSpellArea->getWidth(), std::max(mSpellArea->getHeight(), coord.top)));
        mSpellArea->setVisibleVScroll(true);
        mSpellArea->setViewOffset(MyGUI::IntPoint(0, 0));
    }

    const ESM::NPC& RaceDialog::getResult() const
    {
        return mPreview->getPrototype();
    }
}
