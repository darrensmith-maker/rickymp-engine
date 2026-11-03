#ifndef MWGUI_SETTINGS_H
#define MWGUI_SETTINGS_H

#include "windowbase.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace MyGUI
{
    class EditBox;
    class TextBox;
}

namespace MWGui
{
    class SettingsWindow : public WindowBase
    {
        public:
            SettingsWindow();

            void onOpen() override;

            void updateControlsBox();

            void updateLightSettings();

            void onResChange(int, int) override { center(); }

    protected:
            MyGUI::TabControl* mSettingsTab;
            MyGUI::ListBox* mSectionList;
            MyGUI::ScrollView* mInterfaceScroll;
            MyGUI::ScrollView* mHudScroll;
            MyGUI::ScrollView* mDisplayScroll;
            MyGUI::ScrollView* mWaterScroll;
            MyGUI::ScrollView* mPostProcessingScroll;
            MyGUI::EditBox* mPostEffectFilter;
            MyGUI::ListBox* mPostInactiveEffects;
            MyGUI::ListBox* mPostActiveEffects;
            MyGUI::Button* mPostEffectActivate;
            MyGUI::Button* mPostEffectDeactivate;
            MyGUI::Button* mPostEffectUp;
            MyGUI::Button* mPostEffectDown;
            MyGUI::ScrollView* mPostEffectConfigScroll;
            MyGUI::Widget* mPostEffectConfigArea;
            MyGUI::Button* mOkButton;

            // graphics
            MyGUI::ListBox* mResolutionList;
            MyGUI::Button* mFullscreenButton;
            MyGUI::Button* mWindowBorderButton;
            MyGUI::ComboBox* mTextureFilteringButton;
            MyGUI::Widget* mAnisotropyBox;

            MyGUI::ComboBox* mWaterShaderMode;
            MyGUI::ComboBox* mWaterTextureSize;
            MyGUI::ComboBox* mWaterReflectionDetail;
            MyGUI::Button* mWaterResetButton;

            MyGUI::ComboBox* mMaxLights;
            MyGUI::ComboBox* mLightingMethodButton;
            std::vector<std::string> mLightingMethodValues;
            MyGUI::Button* mLightsResetButton;

            MyGUI::ComboBox* mWeaponSpellBoxMode;
            MyGUI::ComboBox* mResourceBarMode;
            MyGUI::ComboBox* mNpcBarMode;
            MyGUI::ComboBox* mChatMode;
            MyGUI::ComboBox* mQuickLootMode;
            MyGUI::ComboBox* mTerrainPreset;
            MyGUI::ComboBox* mMaterialQuality;
            MyGUI::ComboBox* mLandOptimizationMode;
            MyGUI::ScrollBar* mOptimizedRenderDistanceSlider;
            MyGUI::Widget* mManualRenderDistanceBox;
            MyGUI::Widget* mOptimizedRenderDistanceBox;
            MyGUI::ComboBox* mShadowPreset;
            MyGUI::ComboBox* mShadowMapQuality;

            struct PostUniformSliderBinding
            {
                std::string mEffect;
                std::string mUniform;
                std::string mCaption;
                int mComponent = 0;
                int mComponents = 1;
                float mMin = 0.f;
                float mMax = 1.f;
                float mStep = 0.01f;
                bool mInteger = false;
                std::array<float, 4> mDefault{{0.f, 0.f, 0.f, 0.f}};
                MyGUI::TextBox* mLabel = nullptr;
            };

            struct PostUniformBoolBinding
            {
                std::string mEffect;
                std::string mUniform;
                std::string mCaption;
                bool mDefault = false;
                MyGUI::TextBox* mLabel = nullptr;
            };

            struct PostUniformChoiceBinding
            {
                std::string mEffect;
                std::string mUniform;
                int mComponents = 1;
                bool mInteger = false;
                std::array<float, 4> mDefault{{0.f, 0.f, 0.f, 0.f}};
                std::vector<std::array<float, 4>> mValues;
            };

            std::string mPostSelectedEffect;
            std::unordered_map<MyGUI::ScrollBar*, PostUniformSliderBinding> mPostUniformSliders;
            std::unordered_map<MyGUI::Button*, PostUniformBoolBinding> mPostUniformBools;
            std::unordered_map<MyGUI::ComboBox*, PostUniformChoiceBinding> mPostUniformChoices;

            // controls
            MyGUI::ScrollView* mControlsBox;
            MyGUI::Button* mResetControlsButton;
            MyGUI::Button* mKeyboardSwitch;
            MyGUI::Button* mControllerSwitch;
            bool mKeyboardMode; //if true, setting up the keyboard. Otherwise, it's controller

            void onTabChanged(MyGUI::TabControl* _sender, size_t index);
            void onSectionSelected(MyGUI::ListBox* _sender, size_t index);
            void onOkButtonClicked(MyGUI::Widget* _sender);
            void onTextureFilteringChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onSliderChangePosition(MyGUI::ScrollBar* scroller, size_t pos);
            void onButtonToggled(MyGUI::Widget* _sender);
            void onResolutionSelected(MyGUI::ListBox* _sender, size_t index);
            void onResolutionAccept();
            void onResolutionCancel();
            void highlightCurrentResolution();

            void onWaterShaderModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onWaterTextureSizeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onWaterReflectionDetailChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onWaterResetButtonClicked(MyGUI::Widget* _sender);

            void onLightingMethodButtonChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onLightsResetButtonClicked(MyGUI::Widget* _sender);
            void onMaxLightsChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onWeaponSpellBoxModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onResourceBarModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onNpcBarModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onChatModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onQuickLootModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onTerrainPresetChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onMaterialQualityChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onLandOptimizationModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onShadowPresetChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onShadowMapQualityChanged(MyGUI::ComboBox* _sender, size_t pos);
            void updateWeaponSpellBoxModeCombo();
            void updateResourceBarModeCombo();
            void updateNpcBarModeCombo();
            void updateChatModeCombo();
            void updateQuickLootModeCombo();
            void updateTerrainPresetCombo();
            void updateMaterialQualityCombo();
            void updateLandOptimizationModeCombo();
            void updateLandDistanceControls();
            void updateShadowCombos();
            void updateWaterShaderModeCombo();
            void selectLightingMethod(const std::string& value);

            void refreshPostProcessingLists();
            void rebuildPostEffectConfig();
            void setPostProcessingChain(const std::vector<std::string>& chain);
            void onPostEffectFilterChanged(MyGUI::EditBox* sender);
            void onPostEffectListChanged(MyGUI::ListBox* sender, size_t index);
            void onPostEffectActivateClicked(MyGUI::Widget* sender);
            void onPostEffectDeactivateClicked(MyGUI::Widget* sender);
            void onPostEffectUpClicked(MyGUI::Widget* sender);
            void onPostEffectDownClicked(MyGUI::Widget* sender);
            void onPostUniformSliderChanged(MyGUI::ScrollBar* sender, size_t pos);
            void onPostUniformBoolClicked(MyGUI::Widget* sender);
            void onPostUniformChoiceChanged(MyGUI::ComboBox* sender, size_t pos);
            void onPostEffectResetClicked(MyGUI::Widget* sender);

            void onRebindAction(MyGUI::Widget* _sender);
            void onInputTabMouseWheel(MyGUI::Widget* _sender, int _rel);
            void onResetDefaultBindings(MyGUI::Widget* _sender);
            void onResetDefaultBindingsAccept ();
            void onKeyboardSwitchClicked(MyGUI::Widget* _sender);
            void onControllerSwitchClicked(MyGUI::Widget* _sender);

            void onWindowResize(MyGUI::Window* _sender);

            void apply();

            void configureWidgets(MyGUI::Widget* widget, bool init);
            void updateSliderLabel(MyGUI::ScrollBar* scroller, const std::string& value);

            void layoutControlsBox();
        
        private:
            void resetScrollbars();
    };
}

#endif
