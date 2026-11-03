#include "settingswindow.hpp"

#include <MyGUI_ScrollBar.h>
#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Window.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_Gui.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_LanguageManager.h>

#include <SDL_video.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <numeric>
#include <utility>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_set>

#include <components/debug/debuglog.hpp>
#include <components/fx/technique.hpp>
#include <components/misc/stringops.hpp>
#include <components/misc/constants.hpp>
#include <components/widgets/sharedstatebutton.hpp>
#include <components/settings/settings.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/lightmanager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "confirmationdialog.hpp"
#include "../mwrender/postprocessor.hpp"

namespace
{
    constexpr int settingsWindowWidth = 640;
    constexpr int settingsWindowHeight = 500;

    std::string arenaText(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    std::string lowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string joinPostChain(const std::vector<std::string>& chain)
    {
        std::ostringstream stream;
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            if (i != 0)
                stream << ", ";
            stream << chain[i];
        }
        return stream.str();
    }

    std::string localizePostFxText(const std::string& source, const std::string& fallback = std::string())
    {
        const std::string prefix = "#{OMWShaders:";
        if (source.compare(0, prefix.size(), prefix) == 0 && !source.empty() && source.back() == '}')
        {
            const std::string key = source.substr(prefix.size(), source.size() - prefix.size() - 1);
            const std::string localized = arenaText("postfx." + key);
            // The custom ArenaMP language provider returns the replacement unchanged
            // when no key exists. Do not expose an unresolved tag to the player.
            if (!localized.empty() && localized.find("#{arenamp=") == std::string::npos)
                return localized;
        }
        if (!source.empty() && source.compare(0, 2, "#{") != 0)
            return MyGUI::LanguageManager::getInstance().replaceTags(source);
        return fallback;
    }

    std::array<float, 4> postUniformValue(const std::string& effect, const Fx::UniformSpec& uniform)
    {
        std::array<float, 4> value = uniform.mDefault;
        const auto key = std::make_pair(std::string("Post Processing ") + effect, uniform.mName);
        std::string text;
        auto it = Settings::Manager::mUserSettings.find(key);
        if (it != Settings::Manager::mUserSettings.end())
            text = it->second;
        else
        {
            it = Settings::Manager::mDefaultSettings.find(key);
            if (it != Settings::Manager::mDefaultSettings.end())
                text = it->second;
        }

        if (text.empty())
            return value;

        std::istringstream stream(text);
        if (uniform.mType == Fx::UniformType::Bool)
        {
            std::string token;
            stream >> token;
            token = lowerAscii(token);
            value[0] = (token == "true" || token == "1" || token == "on") ? 1.f : 0.f;
            return value;
        }

        for (int i = 0; i < uniform.components(); ++i)
        {
            float component = value[static_cast<std::size_t>(i)];
            if (!(stream >> component))
                break;
            value[static_cast<std::size_t>(i)] = component;
        }
        return value;
    }

    std::array<float, 4> postStoredUniformValue(const std::string& effect, const std::string& uniform,
        const std::array<float, 4>& fallback, int components)
    {
        std::array<float, 4> value = fallback;
        const auto key = std::make_pair(std::string("Post Processing ") + effect, uniform);
        std::string text;
        auto it = Settings::Manager::mUserSettings.find(key);
        if (it != Settings::Manager::mUserSettings.end())
            text = it->second;
        else
        {
            it = Settings::Manager::mDefaultSettings.find(key);
            if (it != Settings::Manager::mDefaultSettings.end())
                text = it->second;
        }
        if (text.empty())
            return value;
        std::istringstream stream(text);
        for (int i = 0; i < components; ++i)
        {
            float component = value[static_cast<std::size_t>(i)];
            if (!(stream >> component))
                break;
            value[static_cast<std::size_t>(i)] = component;
        }
        return value;
    }

    std::string serializePostUniform(const std::array<float, 4>& value, int components, bool integer)
    {
        std::ostringstream stream;
        stream << std::setprecision(7);
        for (int i = 0; i < components; ++i)
        {
            if (i != 0)
                stream << ' ';
            if (integer)
                stream << static_cast<int>(std::lround(value[static_cast<std::size_t>(i)]));
            else
                stream << value[static_cast<std::size_t>(i)];
        }
        return stream.str();
    }

    std::string formatPostValue(float value, bool integer)
    {
        if (integer)
            return MyGUI::utility::toString(static_cast<int>(std::lround(value)));
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << value;
        return stream.str();
    }

    std::string textureMipmappingToStr(const std::string& val)
    {
        if (val == "linear")  return arenaText("value.trilinear");
        if (val == "nearest") return arenaText("value.bilinear");
        if (val != "none")
            Log(Debug::Warning) << "Warning: Invalid texture mipmap option: "<< val;

        return arenaText("value.other");
    }

    void parseResolution (int &x, int &y, const std::string& str)
    {
        std::vector<std::string> split;
        Misc::StringUtils::split (str, split, "@(x");
        assert (split.size() >= 2);
        Misc::StringUtils::trim(split[0]);
        Misc::StringUtils::trim(split[1]);
        x = MyGUI::utility::parseInt (split[0]);
        y = MyGUI::utility::parseInt (split[1]);
    }

    bool sortResolutions (std::pair<int, int> left, std::pair<int, int> right)
    {
        if (left.first == right.first)
            return left.second > right.second;
        return left.first > right.first;
    }

    std::string getAspect (int x, int y)
    {
        int gcd = std::gcd (x, y);
        if (gcd == 0)
            return std::string();

        int xaspect = x / gcd;
        int yaspect = y / gcd;
        // special case: 8 : 5 is usually referred to as 16:10
        if (xaspect == 8 && yaspect == 5)
            return "16 : 10";
        return MyGUI::utility::toString(xaspect) + " : " + MyGUI::utility::toString(yaspect);
    }

    const char* checkButtonType = "CheckButton";
    const char* sliderType = "Slider";

    std::string getSettingType(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingType");
    }

    std::string getSettingName(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingName");
    }

    std::string getSettingCategory(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingCategory");
    }

    std::string getSettingValueType(MyGUI::Widget* widget)
    {
        return widget->getUserString("SettingValueType");
    }

    void getSettingMinMax(MyGUI::Widget* widget, float& min, float& max)
    {
        const char* settingMin = "SettingMin";
        const char* settingMax = "SettingMax";
        min = 0.f;
        max = 1.f;
        if (!widget->getUserString(settingMin).empty())
            min = MyGUI::utility::parseFloat(widget->getUserString(settingMin));
        if (!widget->getUserString(settingMax).empty())
            max = MyGUI::utility::parseFloat(widget->getUserString(settingMax));
    }

    void updateMaxLightsComboBox(MyGUI::ComboBox* box)
    {
        constexpr int min = 8;
        constexpr int max = 32;
        constexpr int increment = 8;
        int maxLights = Settings::Manager::getInt("max lights", "Shaders");
        // show increments of 8 in dropdown
        if (maxLights >= min && maxLights <= max && !(maxLights % increment))
            box->setIndexSelected((maxLights / increment)-1);
        else
            box->setIndexSelected(MyGUI::ITEM_NONE);
    }

    constexpr std::array<int, 5> shadowMapResolutions = { 512, 1024, 2048, 4096, 8192 };
    constexpr std::array<const char*, 5> shadowMapQualityNames =
        { "value.balanced", "value.high", "value.very_high", "value.detailed", "value.maximum" };
    constexpr std::array<const char*, 6> shadowPresetNames =
        { "value.disabled", "value.actor", "value.npc", "value.object", "value.terrain", "value.indoor" };

    constexpr std::array<const char*, 3> weaponSpellBoxModeNames =
        { "value.hidden", "value.transparent", "value.visible" };
    constexpr std::array<const char*, 3> weaponSpellBoxModes =
        { "hidden", "transparent", "visible" };

    constexpr std::array<const char*, 3> resourceBarModeNames =
        { "value.always", "value.automatic", "value.never" };
    constexpr std::array<const char*, 3> resourceBarModes =
        { "always", "automatic", "hidden" };

    constexpr std::array<const char*, 4> npcBarModeNames =
        { "value.disabled", "hud.npc_bar.combat", "hud.npc_bar.hover", "hud.npc_bar.both" };
    constexpr std::array<const char*, 4> npcBarModes =
        { "off", "combat", "hover", "both" };

    constexpr std::array<const char*, 5> chatModeNames =
        { "chat.mode.visible", "chat.mode.opacity_30", "chat.mode.opacity_60",
          "chat.mode.autohide", "chat.mode.hidden" };
    constexpr std::array<const char*, 5> chatModes =
        { "visible", "transparent30", "transparent60", "autohide", "hidden" };

    std::string getWeaponSpellBoxMode()
    {
        const auto modeKey = std::make_pair(std::string("GUI"), std::string("weapon spell box mode"));
        const auto legacyKey = std::make_pair(std::string("GUI"), std::string("persistent weapon spell boxes"));
        if (Settings::Manager::mUserSettings.find(modeKey) == Settings::Manager::mUserSettings.end())
        {
            const auto legacyIt = Settings::Manager::mUserSettings.find(legacyKey);
            if (legacyIt != Settings::Manager::mUserSettings.end())
                return (legacyIt->second == "false" || legacyIt->second == "0") ? "hidden" : "transparent";
        }

        const std::string mode = Settings::Manager::getString("weapon spell box mode", "GUI");
        if (mode == "hidden" || mode == "transparent" || mode == "visible")
            return mode;
        return Settings::Manager::getBool("persistent weapon spell boxes", "GUI")
            ? "transparent" : "hidden";
    }

    constexpr std::array<const char*, 4> landOptimizationModeNames =
        { "value.off", "value.balance", "value.performance", "value.aggressive" };
    constexpr std::array<const char*, 4> landOptimizationModes =
        { "off", "balance", "performance", "aggressive" };

    constexpr float landOptimizedDistanceMin = 4000.f;
    constexpr float landOptimizedDistanceMax = 40000.f;
    void clampOptimizedLandDistance(float distance)
    {
        distance = std::clamp(distance, landOptimizedDistanceMin, landOptimizedDistanceMax);
        Settings::Manager::setFloat("viewing distance", "Camera", distance);
    }

    std::string getLandOptimizationMode()
    {
        std::string mode = Settings::Manager::getString("optimization land", "Camera");
        Misc::StringUtils::lowerCaseInPlace(mode);

        // Migrate the previous boolean setting without changing old profiles.
        if (mode == "true" || mode == "1" || mode == "on")
            return "balance";
        if (mode == "false" || mode == "0" || mode == "disabled")
            return "off";
        for (const char* candidate : landOptimizationModes)
        {
            if (mode == candidate)
                return mode;
        }
        return "balance";
    }

    constexpr std::array<const char*, 2> waterShaderModeNames =
        { "value.off", "value.water_simple" };
    constexpr std::array<const char*, 2> waterShaderModes =
        { "off", "simple" };

    std::string getWaterShaderMode()
    {
        const auto modeKey = std::make_pair(std::string("Water"), std::string("shader mode"));
        auto modeIt = Settings::Manager::mUserSettings.find(modeKey);
        if (modeIt != Settings::Manager::mUserSettings.end())
        {
            std::string mode = modeIt->second;
            Misc::StringUtils::lowerCaseInPlace(mode);
            for (const char* candidate : waterShaderModes)
            {
                if (mode == candidate)
                    return mode;
            }
        }

        // Migrate old ArenaMP profiles. The custom PBR water path is retired;
        // shader-enabled profiles now use the compatibility shader until the
        // OpenMW 0.52 water renderer is transplanted.
        return Settings::Manager::getBool("shader", "Water") ? "simple" : "off";
    }

    constexpr std::array<const char*, 5> materialQualityNames =
        { "value.none", "value.simple", "value.balanced", "value.quality", "value.ultra" };
    constexpr std::array<const char*, 5> materialQualityModes =
        { "none", "simple", "balanced", "quality", "ultra" };

    std::string getMaterialQualityMode()
    {
        std::string mode = Settings::Manager::getString("material quality", "Shaders");
        Misc::StringUtils::lowerCaseInPlace(mode);
        for (const char* candidate : materialQualityModes)
        {
            if (mode == candidate)
                return mode;
        }

        // Migrate older configurations that only exposed the individual map toggles.
        const bool normals = Settings::Manager::getBool("auto use object normal maps", "Shaders")
            || Settings::Manager::getBool("auto use terrain normal maps", "Shaders");
        const bool specular = Settings::Manager::getBool("auto use object specular maps", "Shaders")
            || Settings::Manager::getBool("auto use terrain specular maps", "Shaders");
        if (!normals)
            return "none";
        if (!specular)
            return "simple";
        return "balanced";
    }

    constexpr std::array<const char*, 6> terrainPresetNames =
        { "value.minimum", "value.low", "value.balanced", "value.medium", "value.high", "value.ultra" };
    constexpr std::array<float, 6> terrainLod =
        { 0.40f, 0.50f, 0.65f, 0.80f, 1.00f, 1.25f };
    constexpr std::array<int, 6> vertexLod =
        { -2, -2, -1, -1, 0, 1 };
    constexpr std::array<int, 6> compositeLevel =
        { -3, -3, -2, -2, -1, 0 };
    constexpr std::array<int, 6> compositeResolution =
        { 1024, 1024, 1024, 2048, 2048, 4096 };
    constexpr std::array<float, 6> maxCompositeGeometrySize =
        { 4.f, 4.f, 6.f, 8.f, 12.f, 16.f };
    constexpr std::array<float, 6> objectPagingMergeFactor =
        { 100000.f, 75000.f, 50000.f, 30000.f, 15000.f, 8000.f };
    constexpr std::array<float, 6> objectPagingMinSize =
        { 1.f, 0.85f, 0.65f, 0.50f, 0.35f, 0.25f };

    constexpr size_t highTerrainPresetIndex = 4;
    constexpr size_t ultraMaterialQualityIndex = 4;
    constexpr size_t qualityMaterialQualityIndex = 3;

    void applyTerrainPresetSettings(size_t pos)
    {
        pos = std::min(pos, terrainPresetNames.size() - 1);
        Settings::Manager::setBool("distant terrain", "Terrain", true);
        Settings::Manager::setFloat("lod factor", "Terrain", terrainLod[pos]);
        Settings::Manager::setInt("vertex lod mod", "Terrain", vertexLod[pos]);
        Settings::Manager::setInt("composite map level", "Terrain", compositeLevel[pos]);
        Settings::Manager::setInt("composite map resolution", "Terrain", compositeResolution[pos]);
        Settings::Manager::setFloat("max composite geometry size", "Terrain", maxCompositeGeometrySize[pos]);
        Settings::Manager::setBool("object paging", "Terrain", true);
        Settings::Manager::setBool("object paging active grid", "Terrain", true);
        Settings::Manager::setFloat("object paging merge factor", "Terrain", objectPagingMergeFactor[pos]);
        Settings::Manager::setFloat("object paging min size", "Terrain", objectPagingMinSize[pos]);
    }

    void applyMaterialQualitySettings(size_t pos)
    {
        pos = std::min(pos, materialQualityModes.size() - 1);
        Settings::Manager::setString("material quality", "Shaders", materialQualityModes[pos]);

        const bool normalMaps = pos >= 1;
        const bool specularMaps = pos >= 2;
        Settings::Manager::setBool("auto use object normal maps", "Shaders", normalMaps);
        Settings::Manager::setBool("auto use terrain normal maps", "Shaders", normalMaps);
        Settings::Manager::setBool("auto use object specular maps", "Shaders", specularMaps);
        Settings::Manager::setBool("auto use terrain specular maps", "Shaders", specularMaps);
    }

    size_t getTerrainPresetPosition()
    {
        const float current = Settings::Manager::getFloat("lod factor", "Terrain");
        size_t best = 0;
        float bestDistance = std::abs(current - terrainLod[0]);
        for (size_t i = 1; i < terrainLod.size(); ++i)
        {
            const float distance = std::abs(current - terrainLod[i]);
            if (distance < bestDistance)
            {
                best = i;
                bestDistance = distance;
            }
        }
        return best;
    }

    std::string getQuickLootMode()
    {
        const auto modeKey = std::make_pair(std::string("GUI"), std::string("quick loot mode"));
        const auto legacyKey = std::make_pair(std::string("GUI"), std::string("quick loot"));
        if (Settings::Manager::mUserSettings.find(modeKey) == Settings::Manager::mUserSettings.end())
        {
            const auto legacyIt = Settings::Manager::mUserSettings.find(legacyKey);
            if (legacyIt != Settings::Manager::mUserSettings.end()
                && (legacyIt->second == "false" || legacyIt->second == "0"))
                return "disabled";
        }

        const std::string mode = Settings::Manager::getString("quick loot mode", "GUI");
        if (mode == "disabled" || mode == "container" || mode == "item")
            return mode;
        return Settings::Manager::getBool("quick loot", "GUI") ? "item" : "disabled";
    }

    size_t getShadowPresetPosition()
    {
        if (!Settings::Manager::getBool("enable shadows", "Shadows"))
            return 0;
        if (Settings::Manager::getBool("enable indoor shadows", "Shadows"))
            return 5;
        if (Settings::Manager::getBool("terrain shadows", "Shadows"))
            return 4;
        if (Settings::Manager::getBool("object shadows", "Shadows"))
            return 3;
        if (Settings::Manager::getBool("actor shadows", "Shadows"))
            return 2;
        return 1;
    }

    size_t getShadowMapQualityPosition()
    {
        const int resolution = Settings::Manager::getInt("shadow map resolution", "Shadows");
        size_t best = 0;
        int bestDistance = std::abs(resolution - shadowMapResolutions[0]);
        for (size_t i = 1; i < shadowMapResolutions.size(); ++i)
        {
            const int distance = std::abs(resolution - shadowMapResolutions[i]);
            if (distance < bestDistance)
            {
                best = i;
                bestDistance = distance;
            }
        }
        return best;
    }

    std::string getShadowMapQualityLabel(size_t position)
    {
        position = std::min(position, shadowMapResolutions.size() - 1);
        return arenaText(shadowMapQualityNames[position]) + " - "
            + MyGUI::utility::toString(shadowMapResolutions[position]);
    }
}

namespace MWGui
{
    void SettingsWindow::configureWidgets(MyGUI::Widget* widget, bool init)
    {
        MyGUI::EnumeratorWidgetPtr widgets = widget->getEnumerator();
        while (widgets.next())
        {
            MyGUI::Widget* current = widgets.current();

            std::string type = getSettingType(current);
            if (type == checkButtonType)
            {
                std::string initialValue = Settings::Manager::getBool(getSettingName(current),
                                                                      getSettingCategory(current))
                        ? "#{sOn}" : "#{sOff}";
                current->castType<MyGUI::Button>()->setCaptionWithReplacing(initialValue);
                if (init)
                    current->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onButtonToggled);
            }
            if (type == sliderType)
            {
                MyGUI::ScrollBar* scroll = current->castType<MyGUI::ScrollBar>();
                std::string valueStr;
                std::string valueType = getSettingValueType(current);
                if (valueType == "Float" || valueType == "Integer" || valueType == "Cell")
                {
                    // TODO: ScrollBar isn't meant for this. should probably use a dedicated FloatSlider widget
                    float min,max;
                    getSettingMinMax(scroll, min, max);
                    float value = Settings::Manager::getFloat(getSettingName(current), getSettingCategory(current));

                    if (valueType == "Cell")
                    {
                        std::stringstream ss;
                        ss << std::fixed << std::setprecision(2) << value/Constants::CellSizeInUnits;
                        valueStr = ss.str();
                    }
                    else if (valueType == "Float")
                    {
                        std::stringstream ss;
                        ss << std::fixed << std::setprecision(2) << value;
                        valueStr = ss.str();
                    }
                    else
                        valueStr = MyGUI::utility::toString(int(value));

                    value = std::max(min, std::min(value, max));
                    value = (value-min)/(max-min);

                    scroll->setScrollPosition(static_cast<size_t>(value * (scroll->getScrollRange() - 1)));
                }
                else
                {
                    int value = Settings::Manager::getInt(getSettingName(current), getSettingCategory(current));
                    valueStr = MyGUI::utility::toString(value);
                    scroll->setScrollPosition(value);
                }
                if (init)
                    scroll->eventScrollChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onSliderChangePosition);
                if (scroll->getVisible())
                    updateSliderLabel(scroll, valueStr);
            }

            configureWidgets(current, init);
        }
    }

    void SettingsWindow::updateSliderLabel(MyGUI::ScrollBar *scroller, const std::string& value)
    {
        std::string labelWidgetName = scroller->getUserString("SettingLabelWidget");
        if (!labelWidgetName.empty())
        {
            MyGUI::TextBox* textBox;
            getWidget(textBox, labelWidgetName);
            std::string labelCaption = MyGUI::LanguageManager::getInstance().replaceTags(
                scroller->getUserString("SettingLabelCaption"));
            labelCaption = Misc::StringUtils::format(labelCaption, value);
            textBox->setCaption(labelCaption);
        }
    }

    SettingsWindow::SettingsWindow() :
        WindowBase("openmw_settings_window.layout"),
        mKeyboardMode(true)
    {
        configureWidgets(mMainWidget, true);

        setTitle("#{sOptions}");

        getWidget(mSettingsTab, "SettingsTab");
        getWidget(mSectionList, "SectionList");
        getWidget(mInterfaceScroll, "InterfaceScroll");
        getWidget(mHudScroll, "HUDScroll");
        getWidget(mDisplayScroll, "DisplayScroll");
        getWidget(mWaterScroll, "WaterScroll");
        getWidget(mPostProcessingScroll, "PostProcessingScroll");
        getWidget(mPostEffectFilter, "PostEffectFilter");
        getWidget(mPostInactiveEffects, "PostInactiveEffects");
        getWidget(mPostActiveEffects, "PostActiveEffects");
        getWidget(mPostEffectActivate, "PostEffectActivate");
        getWidget(mPostEffectDeactivate, "PostEffectDeactivate");
        getWidget(mPostEffectUp, "PostEffectUp");
        getWidget(mPostEffectDown, "PostEffectDown");
        getWidget(mPostEffectConfigScroll, "PostEffectConfigScroll");
        getWidget(mPostEffectConfigArea, "PostEffectConfigArea");
        getWidget(mOkButton, "OkButton");
        getWidget(mResolutionList, "ResolutionList");
        getWidget(mFullscreenButton, "FullscreenButton");
        getWidget(mWindowBorderButton, "WindowBorderButton");
        getWidget(mTextureFilteringButton, "TextureFilteringButton");
        getWidget(mAnisotropyBox, "AnisotropyBox");
        getWidget(mControlsBox, "ControlsBox");
        getWidget(mResetControlsButton, "ResetControlsButton");
        getWidget(mKeyboardSwitch, "KeyboardButton");
        getWidget(mControllerSwitch, "ControllerButton");
        getWidget(mWaterShaderMode, "WaterShaderMode");
        getWidget(mWaterTextureSize, "WaterTextureSize");
        getWidget(mWaterReflectionDetail, "WaterReflectionDetail");
        getWidget(mWaterResetButton, "WaterResetButton");
        getWidget(mLightingMethodButton, "LightingMethodButton");
        getWidget(mLightsResetButton, "LightsResetButton");
        getWidget(mMaxLights, "MaxLights");
        getWidget(mWeaponSpellBoxMode, "WeaponSpellBoxMode");
        getWidget(mResourceBarMode, "ResourceBarMode");
        getWidget(mNpcBarMode, "NpcBarMode");
        getWidget(mChatMode, "ChatMode");
        getWidget(mQuickLootMode, "QuickLootMode");
        getWidget(mTerrainPreset, "TerrainPreset");
        getWidget(mMaterialQuality, "MaterialQuality");
        getWidget(mLandOptimizationMode, "LandOptimizationMode");
        getWidget(mOptimizedRenderDistanceSlider, "OptimizedRenderingDistanceSlider");
        getWidget(mManualRenderDistanceBox, "ManualRenderDistanceBox");
        getWidget(mOptimizedRenderDistanceBox, "OptimizedRenderDistanceBox");
        getWidget(mShadowPreset, "ShadowPreset");
        getWidget(mShadowMapQuality, "ShadowMapQuality");

        mMainWidget->setSize(settingsWindowWidth, settingsWindowHeight);

        // Must match the TabItem order in openmw_settings_window.layout.
        const std::array<const char*, 12> sectionKeys = {
            "settings.section.interface",
            "settings.section.hud",
            "settings.section.controls",
            "settings.subsection.display",
            "settings.subsection.quality",
            "settings.subsection.water",
            "settings.subsection.lighting",
            "settings.subsection.postprocessing",
            "settings.subsection.world",
            "settings.subsection.shadows",
            "settings.section.audio",
            "settings.section.advanced",
        };
        for (const char* key : sectionKeys)
            mSectionList->addItem(arenaText(key));
        mSectionList->setIndexSelected(0);
        mSettingsTab->setIndexSelected(0);

#ifndef WIN32
        // hide gamma controls since it currently does not work under Linux
        MyGUI::ScrollBar *gammaSlider;
        getWidget(gammaSlider, "GammaSlider");
        gammaSlider->setVisible(false);
        MyGUI::TextBox *textBox;
        getWidget(textBox, "GammaText");
        textBox->setVisible(false);
        getWidget(textBox, "GammaTextDark");
        textBox->setVisible(false);
        getWidget(textBox, "GammaTextLight");
        textBox->setVisible(false);
#endif

        mMainWidget->castType<MyGUI::Window>()->eventWindowChangeCoord += MyGUI::newDelegate(this, &SettingsWindow::onWindowResize);

        mSettingsTab->eventTabChangeSelect += MyGUI::newDelegate(this, &SettingsWindow::onTabChanged);
        mSectionList->eventListChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onSectionSelected);
        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onOkButtonClicked);
        mTextureFilteringButton->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onTextureFilteringChanged);
        mResolutionList->eventListChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onResolutionSelected);

        mWaterShaderMode->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onWaterShaderModeChanged);
        mWaterTextureSize->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onWaterTextureSizeChanged);
        mWaterReflectionDetail->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onWaterReflectionDetailChanged);
        mWaterResetButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onWaterResetButtonClicked);

        mLightingMethodButton->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onLightingMethodButtonChanged);
        mLightsResetButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onLightsResetButtonClicked);
        mMaxLights->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onMaxLightsChanged);

        mPostEffectFilter->eventEditTextChange += MyGUI::newDelegate(this, &SettingsWindow::onPostEffectFilterChanged);
        mPostInactiveEffects->eventListChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onPostEffectListChanged);
        mPostActiveEffects->eventListChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onPostEffectListChanged);
        mPostEffectActivate->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onPostEffectActivateClicked);
        mPostEffectDeactivate->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onPostEffectDeactivateClicked);
        mPostEffectUp->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onPostEffectUpClicked);
        mPostEffectDown->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onPostEffectDownClicked);

        mTextureFilteringButton->removeAllItems();
        mTextureFilteringButton->addItem(arenaText("value.bilinear"));
        mTextureFilteringButton->addItem(arenaText("value.trilinear"));

        mWaterShaderMode->removeAllItems();
        for (const char* name : waterShaderModeNames)
            mWaterShaderMode->addItem(arenaText(name));

        mWaterTextureSize->removeAllItems();
        mWaterTextureSize->addItem(arenaText("value.extra_low"));
        mWaterTextureSize->addItem(arenaText("value.low"));
        mWaterTextureSize->addItem(arenaText("value.medium"));
        mWaterTextureSize->addItem(arenaText("value.high"));

        mWaterReflectionDetail->removeAllItems();
        mWaterReflectionDetail->addItem(arenaText("value.off"));
        mWaterReflectionDetail->addItem(arenaText("value.terrain"));
        mWaterReflectionDetail->addItem(arenaText("value.world"));
        mWaterReflectionDetail->addItem(arenaText("value.objects"));
        mWaterReflectionDetail->addItem(arenaText("value.actors"));
        mWaterReflectionDetail->addItem(arenaText("settings.groundcover"));

        for (const char* name : weaponSpellBoxModeNames)
            mWeaponSpellBoxMode->addItem(arenaText(name));
        mWeaponSpellBoxMode->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onWeaponSpellBoxModeChanged);

        for (const char* name : resourceBarModeNames)
            mResourceBarMode->addItem(arenaText(name));
        mResourceBarMode->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onResourceBarModeChanged);

        for (const char* name : npcBarModeNames)
            mNpcBarMode->addItem(arenaText(name));
        mNpcBarMode->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onNpcBarModeChanged);

        for (const char* name : chatModeNames)
            mChatMode->addItem(arenaText(name));
        mChatMode->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onChatModeChanged);

        mQuickLootMode->addItem(arenaText("value.disabled"));
        mQuickLootMode->addItem(arenaText("value.container"));
        mQuickLootMode->addItem(arenaText("value.item"));
        mQuickLootMode->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onQuickLootModeChanged);

        for (const char* name : terrainPresetNames)
            mTerrainPreset->addItem(arenaText(name));
        mTerrainPreset->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onTerrainPresetChanged);

        for (const char* name : materialQualityNames)
            mMaterialQuality->addItem(arenaText(name));
        mMaterialQuality->eventComboChangePosition += MyGUI::newDelegate(
            this, &SettingsWindow::onMaterialQualityChanged);

        for (const char* name : landOptimizationModeNames)
            mLandOptimizationMode->addItem(arenaText(name));
        mLandOptimizationMode->eventComboChangePosition += MyGUI::newDelegate(
            this, &SettingsWindow::onLandOptimizationModeChanged);

        for (const char* name : shadowPresetNames)
            mShadowPreset->addItem(arenaText(name));
        mShadowPreset->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onShadowPresetChanged);

        for (size_t i = 0; i < shadowMapResolutions.size(); ++i)
            mShadowMapQuality->addItem(getShadowMapQualityLabel(i));
        mShadowMapQuality->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onShadowMapQualityChanged);

        mKeyboardSwitch->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onKeyboardSwitchClicked);
        mControllerSwitch->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onControllerSwitchClicked);

        center();

        mResetControlsButton->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onResetDefaultBindings);

        // fill resolution list
        int screen = Settings::Manager::getInt("screen", "Video");
        int numDisplayModes = SDL_GetNumDisplayModes(screen);
        std::vector < std::pair<int, int> > resolutions;
        for (int i = 0; i < numDisplayModes; i++)
        {
            SDL_DisplayMode mode;
            SDL_GetDisplayMode(screen, i, &mode);
            resolutions.emplace_back(mode.w, mode.h);
        }
        std::sort(resolutions.begin(), resolutions.end(), sortResolutions);
        for (std::pair<int, int>& resolution : resolutions)
        {
            std::string str = MyGUI::utility::toString(resolution.first) + " x " + MyGUI::utility::toString(resolution.second);
            std::string aspect = getAspect(resolution.first, resolution.second);
            if (!aspect.empty())
                 str = str + " (" + aspect + ")";

            if (mResolutionList->findItemIndexWith(str) == MyGUI::ITEM_NONE)
                mResolutionList->addItem(str);
        }
        highlightCurrentResolution();

        std::string tmip = Settings::Manager::getString("texture mipmap", "General");
        mTextureFilteringButton->setCaption(textureMipmappingToStr(tmip));

        int waterTextureSize = Settings::Manager::getInt("rtt size", "Water");
        if (waterTextureSize >= 2048)
            mWaterTextureSize->setIndexSelected(3);
        else if (waterTextureSize >= 1024)
            mWaterTextureSize->setIndexSelected(2);
        else if (waterTextureSize >= 512)
            mWaterTextureSize->setIndexSelected(1);
        else
            mWaterTextureSize->setIndexSelected(0);

        int waterReflectionDetail = Settings::Manager::getInt("reflection detail", "Water");
        waterReflectionDetail = std::min(5, std::max(0, waterReflectionDetail));
        mWaterReflectionDetail->setIndexSelected(waterReflectionDetail);

        updateMaxLightsComboBox(mMaxLights);
        updateWeaponSpellBoxModeCombo();
        updateResourceBarModeCombo();
        updateNpcBarModeCombo();
        updateChatModeCombo();
        updateQuickLootModeCombo();
        updateTerrainPresetCombo();
        updateMaterialQualityCombo();
        updateLandOptimizationModeCombo();
        updateLandDistanceControls();
        updateShadowCombos();

        mWindowBorderButton->setEnabled(!Settings::Manager::getBool("fullscreen", "Video"));

        mKeyboardSwitch->setStateSelected(true);
        mControllerSwitch->setStateSelected(false);
    }

    void SettingsWindow::onTabChanged(MyGUI::TabControl* /*_sender*/, size_t index)
    {
        if (index < mSectionList->getItemCount() && mSectionList->getIndexSelected() != index)
            mSectionList->setIndexSelected(index);
        configureWidgets(mMainWidget, false);
        resetScrollbars();
    }
    void SettingsWindow::onSectionSelected(MyGUI::ListBox* /*_sender*/, size_t index)
    {
        if (index == MyGUI::ITEM_NONE || index >= mSectionList->getItemCount())
            return;
        mSettingsTab->setIndexSelected(index);
        configureWidgets(mMainWidget, false);
        resetScrollbars();
    }

    void SettingsWindow::onOkButtonClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Settings);
    }

    void SettingsWindow::onResolutionSelected(MyGUI::ListBox* _sender, size_t index)
    {
        if (index == MyGUI::ITEM_NONE)
            return;

        ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
        dialog->askForConfirmation("#{sNotifyMessage67}");
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &SettingsWindow::onResolutionAccept);
        dialog->eventCancelClicked.clear();
        dialog->eventCancelClicked += MyGUI::newDelegate(this, &SettingsWindow::onResolutionCancel);
    }

    void SettingsWindow::onResolutionAccept()
    {
        std::string resStr = mResolutionList->getItemNameAt(mResolutionList->getIndexSelected());
        int resX, resY;
        parseResolution (resX, resY, resStr);

        Settings::Manager::setInt("resolution x", "Video", resX);
        Settings::Manager::setInt("resolution y", "Video", resY);

        apply();
    }

    void SettingsWindow::onResolutionCancel()
    {
        highlightCurrentResolution();
    }

    void SettingsWindow::highlightCurrentResolution()
    {
        mResolutionList->setIndexSelected(MyGUI::ITEM_NONE);

        int currentX = Settings::Manager::getInt("resolution x", "Video");
        int currentY = Settings::Manager::getInt("resolution y", "Video");

        for (size_t i=0; i<mResolutionList->getItemCount(); ++i)
        {
            int resX, resY;
            parseResolution (resX, resY, mResolutionList->getItemNameAt(i));

            if (resX == currentX && resY == currentY)
            {
                mResolutionList->setIndexSelected(i);
                break;
            }
        }
    }

    void SettingsWindow::onWaterShaderModeChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE || pos >= waterShaderModes.size())
            return;

        Settings::Manager::setString("shader mode", "Water", waterShaderModes[pos]);
        // Keep the legacy boolean synchronized for old configs/tools. Both
        // explicit shader modes use shader-water; only Off maps to false.
        Settings::Manager::setBool("shader", "Water", pos != 0);
        apply();
    }

    void SettingsWindow::onWaterTextureSizeChanged(MyGUI::ComboBox* _sender, size_t pos)
    {
        int size = 256;
        if (pos == 0)
            size = 256;
        else if (pos == 1)
            size = 512;
        else if (pos == 2)
            size = 1024;
        else if (pos == 3)
            size = 2048;
        Settings::Manager::setInt("rtt size", "Water", size);
        apply();
    }

    void SettingsWindow::onWaterReflectionDetailChanged(MyGUI::ComboBox* _sender, size_t pos)
    {
        unsigned int level = std::min((unsigned int)5, (unsigned int)pos);
        Settings::Manager::setInt("reflection detail", "Water", level);
        apply();
    }

    void SettingsWindow::onWaterResetButtonClicked(MyGUI::Widget* _sender)
    {
        std::vector<std::string> buttons = {"#{sYes}", "#{sNo}"};
        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(
            arenaText("settings.reset_water_confirm"), buttons, true);
        const int selectedButton = MWBase::Environment::get().getWindowManager()->readPressedButton();
        if (selectedButton == 1 || selectedButton == -1)
            return;

        constexpr std::array<const char*, 10> settings = {
            "shader",
            "shader mode",
            "refraction",
            "rtt size",
            "reflection detail",
            "caustics intensity",
            "underwater tint",
            "transparency",
            "wave strength",
            "surface roughness",
        };
        for (const char* setting : settings)
            Settings::Manager::setString(setting, "Water", Settings::Manager::mDefaultSettings[{"Water", setting}]);

        updateWaterShaderModeCombo();

        const int waterTextureSize = Settings::Manager::getInt("rtt size", "Water");
        if (waterTextureSize >= 2048)
            mWaterTextureSize->setIndexSelected(3);
        else if (waterTextureSize >= 1024)
            mWaterTextureSize->setIndexSelected(2);
        else if (waterTextureSize >= 512)
            mWaterTextureSize->setIndexSelected(1);
        else
            mWaterTextureSize->setIndexSelected(0);

        int waterReflectionDetail = Settings::Manager::getInt("reflection detail", "Water");
        waterReflectionDetail = std::min(5, std::max(0, waterReflectionDetail));
        mWaterReflectionDetail->setIndexSelected(waterReflectionDetail);

        configureWidgets(mMainWidget, false);
        apply();
    }

    void SettingsWindow::onLightingMethodButtonChanged(MyGUI::ComboBox* _sender, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;

        std::string message = arenaText("settings.restart_required");
        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(message, {"#{sOK}"}, true);

        if (pos >= mLightingMethodValues.size())
            return;
        Settings::Manager::setString("lighting method", "Shaders", mLightingMethodValues[pos]);
        apply();
    }

    void SettingsWindow::onMaxLightsChanged(MyGUI::ComboBox* _sender, size_t pos)
    {
        int count = 8 * (pos + 1);

        Settings::Manager::setInt("max lights", "Shaders", count);
        apply();
        configureWidgets(mMainWidget, false);
    }

    void SettingsWindow::onLightsResetButtonClicked(MyGUI::Widget* _sender)
    {
        std::vector<std::string> buttons = {"#{sYes}", "#{sNo}"};
        std::string message = arenaText("settings.reset_lights_confirm");
        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(message, buttons, true);
        int selectedButton = MWBase::Environment::get().getWindowManager()->readPressedButton();
        if (selectedButton == 1 || selectedButton == -1)
            return;

        constexpr std::array<const char*, 6> settings = {
            "light bounds multiplier",
            "maximum light distance",
            "light fade start",
            "minimum interior brightness",
            "max lights",
            "lighting method",
        };
        for (const auto& setting : settings)
            Settings::Manager::setString(setting, "Shaders", Settings::Manager::mDefaultSettings[{"Shaders", setting}]);

        selectLightingMethod(Settings::Manager::mDefaultSettings[{"Shaders", "lighting method"}]);
        updateMaxLightsComboBox(mMaxLights);

        apply();
        configureWidgets(mMainWidget, false);
    }

    void SettingsWindow::onButtonToggled(MyGUI::Widget* _sender)
    {
        std::string on = MWBase::Environment::get().getWindowManager()->getGameSettingString("sOn", "On");
        std::string off = MWBase::Environment::get().getWindowManager()->getGameSettingString("sOff", "On");
        bool newState;
        if (_sender->castType<MyGUI::Button>()->getCaption() == on)
        {
            _sender->castType<MyGUI::Button>()->setCaption(off);
            newState = false;
        }
        else
        {
            _sender->castType<MyGUI::Button>()->setCaption(on);
            newState = true;
        }

        if (_sender == mFullscreenButton)
        {
            // check if this resolution is supported in fullscreen
            if (mResolutionList->getIndexSelected() != MyGUI::ITEM_NONE)
            {
                std::string resStr = mResolutionList->getItemNameAt(mResolutionList->getIndexSelected());
                int resX, resY;
                parseResolution (resX, resY, resStr);
                Settings::Manager::setInt("resolution x", "Video", resX);
                Settings::Manager::setInt("resolution y", "Video", resY);
            }

            bool supported = false;
            int fallbackX = 0, fallbackY = 0;
            for (unsigned int i=0; i<mResolutionList->getItemCount(); ++i)
            {
                std::string resStr = mResolutionList->getItemNameAt(i);
                int resX, resY;
                parseResolution (resX, resY, resStr);

                if (i == 0)
                {
                    fallbackX = resX;
                    fallbackY = resY;
                }

                if (resX == Settings::Manager::getInt("resolution x", "Video")
                    && resY  == Settings::Manager::getInt("resolution y", "Video"))
                    supported = true;
            }

            if (!supported && mResolutionList->getItemCount())
            {
                if (fallbackX != 0 && fallbackY != 0)
                {
                    Settings::Manager::setInt("resolution x", "Video", fallbackX);
                    Settings::Manager::setInt("resolution y", "Video", fallbackY);
                }
            }

            mWindowBorderButton->setEnabled(!newState);
        }

        if (getSettingType(_sender) == checkButtonType)
        {
            const std::string settingName = getSettingName(_sender);
            const std::string settingCategory = getSettingCategory(_sender);
            Settings::Manager::setBool(settingName, settingCategory, newState);
            apply();

            // Persist this HUD preference immediately. This prevents it from
            // reverting after reconnects, crashes or forced client shutdowns.
            if (settingCategory == "HUD" || settingCategory == "Chat"
                || settingCategory.compare(0, 15, "Post Processing") == 0
                || (settingCategory == "GUI" && settingName == "target info panel"))
                Settings::Manager::saveUser();
            return;
        }
    }

    void SettingsWindow::onTextureFilteringChanged(MyGUI::ComboBox* _sender, size_t pos)
    {
        if(pos == 0)
            Settings::Manager::setString("texture mipmap", "General", "nearest");
        else if(pos == 1)
            Settings::Manager::setString("texture mipmap", "General", "linear");
        else
            Log(Debug::Warning) << "Unexpected option pos " << pos;
        apply();
    }

    void SettingsWindow::updateWeaponSpellBoxModeCombo()
    {
        const std::string mode = getWeaponSpellBoxMode();
        size_t pos = 1;
        if (mode == "hidden")
            pos = 0;
        else if (mode == "visible")
            pos = 2;
        mWeaponSpellBoxMode->setIndexSelected(pos);
    }

    void SettingsWindow::updateResourceBarModeCombo()
    {
        std::string mode = Settings::Manager::getString("resource bars mode", "HUD");
        if (mode != "always" && mode != "automatic" && mode != "hidden")
            mode = Settings::Manager::getBool("auto hide resource bars", "GUI") ? "automatic" : "always";
        const auto it = std::find(resourceBarModes.begin(), resourceBarModes.end(), mode);
        mResourceBarMode->setIndexSelected(it == resourceBarModes.end()
            ? 1 : static_cast<size_t>(std::distance(resourceBarModes.begin(), it)));
    }

    void SettingsWindow::updateNpcBarModeCombo()
    {
        std::string mode = Settings::Manager::getString("npc bar mode", "HUD");
        const auto it = std::find(npcBarModes.begin(), npcBarModes.end(), mode);
        mNpcBarMode->setIndexSelected(it == npcBarModes.end()
            ? (Settings::Manager::getBool("target info panel", "GUI") ? 3 : 1)
            : static_cast<size_t>(std::distance(npcBarModes.begin(), it)));
    }

    void SettingsWindow::updateChatModeCombo()
    {
        const std::string mode = Settings::Manager::getString("mode", "Chat");
        const auto it = std::find(chatModes.begin(), chatModes.end(), mode);
        mChatMode->setIndexSelected(it == chatModes.end()
            ? 1 : static_cast<size_t>(std::distance(chatModes.begin(), it)));
    }

    void SettingsWindow::updateQuickLootModeCombo()
    {
        const std::string mode = getQuickLootMode();
        size_t pos = 2;
        if (mode == "disabled")
            pos = 0;
        else if (mode == "container")
            pos = 1;
        mQuickLootMode->setIndexSelected(pos);
    }

    void SettingsWindow::updateTerrainPresetCombo()
    {
        mTerrainPreset->setIndexSelected(getTerrainPresetPosition());
    }

    void SettingsWindow::updateMaterialQualityCombo()
    {
        const std::string mode = getMaterialQualityMode();
        size_t position = 2;
        for (size_t i = 0; i < materialQualityModes.size(); ++i)
        {
            if (mode == materialQualityModes[i])
            {
                position = i;
                break;
            }
        }
        mMaterialQuality->setIndexSelected(position);
    }

    void SettingsWindow::updateLandOptimizationModeCombo()
    {
        const std::string mode = getLandOptimizationMode();
        size_t position = 1;
        for (size_t i = 0; i < landOptimizationModes.size(); ++i)
        {
            if (mode == landOptimizationModes[i])
            {
                position = i;
                break;
            }
        }
        mLandOptimizationMode->setIndexSelected(position);
    }

    void SettingsWindow::updateLandDistanceControls()
    {
        const bool optimized = getLandOptimizationMode() != "off";
        mManualRenderDistanceBox->setVisible(!optimized);
        mOptimizedRenderDistanceBox->setVisible(optimized);
    }

    void SettingsWindow::updateShadowCombos()
    {
        mShadowPreset->setIndexSelected(getShadowPresetPosition());
        mShadowMapQuality->setIndexSelected(getShadowMapQualityPosition());
    }

    void SettingsWindow::onWeaponSpellBoxModeChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, weaponSpellBoxModes.size() - 1);
        Settings::Manager::setString("weapon spell box mode", "GUI", weaponSpellBoxModes[pos]);
        // Keep the old boolean synchronized for older configs and external tools.
        Settings::Manager::setBool("persistent weapon spell boxes", "GUI", pos != 0);
        Settings::Manager::saveUser();
        apply();
    }

    void SettingsWindow::onResourceBarModeChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, resourceBarModes.size() - 1);
        Settings::Manager::setString("resource bars mode", "HUD", resourceBarModes[pos]);
        Settings::Manager::setBool("auto hide resource bars", "GUI", pos == 1);
        Settings::Manager::saveUser();
        apply();
    }

    void SettingsWindow::onNpcBarModeChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, npcBarModes.size() - 1);
        Settings::Manager::setString("npc bar mode", "HUD", npcBarModes[pos]);
        Settings::Manager::setBool("target info panel", "GUI", pos >= 2);
        Settings::Manager::saveUser();
        apply();
    }

    void SettingsWindow::onChatModeChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, chatModes.size() - 1);
        Settings::Manager::setString("mode", "Chat", chatModes[pos]);
        Settings::Manager::saveUser();
        apply();
    }

    void SettingsWindow::onQuickLootModeChanged(MyGUI::ComboBox*, size_t pos)
    {
        static constexpr std::array<const char*, 3> modes = { "disabled", "container", "item" };
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, modes.size() - 1);
        Settings::Manager::setString("quick loot mode", "GUI", modes[pos]);
        // Keep the legacy boolean synchronized for old configs and external tools.
        Settings::Manager::setBool("quick loot", "GUI", pos != 0);
        apply();
    }

    void SettingsWindow::onTerrainPresetChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, terrainPresetNames.size() - 1);

        // These settings are hot-applied by RenderingManager. Distant land and
        // object paging are intentionally always enabled; the preset only changes
        // their detail/performance balance.
        applyTerrainPresetSettings(pos);

        // Keep the highest material-map mode tied to High/Ultra terrain while
        // the OpenMW 0.52 material pipeline is being transplanted.
        if (pos < highTerrainPresetIndex && getMaterialQualityMode() == "ultra")
        {
            applyMaterialQualitySettings(qualityMaterialQualityIndex);
            mMaterialQuality->setIndexSelected(qualityMaterialQualityIndex);
        }
        apply();
    }

    void SettingsWindow::onMaterialQualityChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, materialQualityModes.size() - 1);

        // The highest compatibility material-map mode still requires High terrain
        // detail until the OpenMW 0.52 terrain/material shader batch replaces it.
        if (pos == ultraMaterialQualityIndex && getTerrainPresetPosition() < highTerrainPresetIndex)
        {
            applyTerrainPresetSettings(highTerrainPresetIndex);
            mTerrainPreset->setIndexSelected(highTerrainPresetIndex);
        }

        applyMaterialQualitySettings(pos);
        apply();
    }

    void SettingsWindow::onLandOptimizationModeChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, landOptimizationModes.size() - 1);
        Settings::Manager::setString("optimization land", "Camera", landOptimizationModes[pos]);
        if (pos != 0)
            clampOptimizedLandDistance(Settings::Manager::getFloat("viewing distance", "Camera"));
        updateLandDistanceControls();
        configureWidgets(mMainWidget, false);
        apply();
    }

    void SettingsWindow::onShadowPresetChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, shadowPresetNames.size() - 1);
        Settings::Manager::setBool("enable shadows", "Shadows", pos >= 1);
        Settings::Manager::setBool("player shadows", "Shadows", pos >= 1);
        Settings::Manager::setBool("actor shadows", "Shadows", pos >= 2);
        Settings::Manager::setBool("object shadows", "Shadows", pos >= 3);
        Settings::Manager::setBool("terrain shadows", "Shadows", pos >= 4);
        Settings::Manager::setBool("enable indoor shadows", "Shadows", pos >= 5);
        apply();
    }

    void SettingsWindow::onShadowMapQualityChanged(MyGUI::ComboBox*, size_t pos)
    {
        if (pos == MyGUI::ITEM_NONE)
            return;
        pos = std::min(pos, shadowMapResolutions.size() - 1);
        Settings::Manager::setInt("shadow map resolution", "Shadows", shadowMapResolutions[pos]);
        apply();
    }

    void SettingsWindow::onSliderChangePosition(MyGUI::ScrollBar* scroller, size_t pos)
    {
        const std::string type = getSettingType(scroller);

        if (type == sliderType)
        {
            std::string valueStr;
            std::string valueType = getSettingValueType(scroller);
            if (valueType == "Float" || valueType == "Integer" || valueType == "Cell")
            {
                float value = pos / float(scroller->getScrollRange()-1);

                float min,max;
                getSettingMinMax(scroller, min, max);
                value = min + (max-min) * value;
                if (valueType == "Float")
                    Settings::Manager::setFloat(getSettingName(scroller), getSettingCategory(scroller), value);
                else
                    Settings::Manager::setInt(getSettingName(scroller), getSettingCategory(scroller), (int)value);

                if (valueType == "Cell")
                {
                    std::stringstream ss;
                    ss << std::fixed << std::setprecision(2) << value/Constants::CellSizeInUnits;
                    valueStr = ss.str();
                }
                else if (valueType == "Float")
                {
                    std::stringstream ss;
                    ss << std::fixed << std::setprecision(2) << value;
                    valueStr = ss.str();
                }
                else
                    valueStr = MyGUI::utility::toString(int(value));
            }
            else
            {
                Settings::Manager::setInt(getSettingName(scroller), getSettingCategory(scroller), pos);
                valueStr = MyGUI::utility::toString(pos);
            }
            if (scroller == mOptimizedRenderDistanceSlider)
            {
                const float distance = Settings::Manager::getFloat("viewing distance", "Camera");
                clampOptimizedLandDistance(distance);
            }

            updateSliderLabel(scroller, valueStr);

            apply();
            const std::string category = getSettingCategory(scroller);
            if (category == "HUD" || category == "Chat"
                || category.compare(0, 15, "Post Processing") == 0)
                Settings::Manager::saveUser();
        }
    }

    void SettingsWindow::apply()
    {
        const Settings::CategorySettingVector changed = Settings::Manager::getPendingChanges();
        MWBase::Environment::get().getWorld()->processChangedSettings(changed);
        MWBase::Environment::get().getSoundManager()->processChangedSettings(changed);
        MWBase::Environment::get().getWindowManager()->processChangedSettings(changed);
        MWBase::Environment::get().getInputManager()->processChangedSettings(changed);
        MWBase::Environment::get().getMechanicsManager()->processChangedSettings(changed);
        Settings::Manager::resetPendingChanges();
    }

    void SettingsWindow::onKeyboardSwitchClicked(MyGUI::Widget* _sender)
    {
        if(mKeyboardMode)
            return;
        mKeyboardMode = true;
        mKeyboardSwitch->setStateSelected(true);
        mControllerSwitch->setStateSelected(false);
        updateControlsBox();
        resetScrollbars();
    }

    void SettingsWindow::onControllerSwitchClicked(MyGUI::Widget* _sender)
    {
        if(!mKeyboardMode)
            return;
        mKeyboardMode = false;
        mKeyboardSwitch->setStateSelected(false);
        mControllerSwitch->setStateSelected(true);
        updateControlsBox();
        resetScrollbars();
    }

    void SettingsWindow::updateControlsBox()
    {
        while (mControlsBox->getChildCount())
            MyGUI::Gui::getInstance().destroyWidget(mControlsBox->getChildAt(0));

        MWBase::Environment::get().getWindowManager()->removeStaticMessageBox();
        std::vector<int> actions;
        if(mKeyboardMode)
            actions = MWBase::Environment::get().getInputManager()->getActionKeySorting();
        else
            actions = MWBase::Environment::get().getInputManager()->getActionControllerSorting();

        for (const int& action : actions)
        {
            std::string desc = MWBase::Environment::get().getInputManager()->getActionDescription (action);
            if (desc == "")
                continue;

            std::string binding;
            if(mKeyboardMode)
                binding = MWBase::Environment::get().getInputManager()->getActionKeyBindingName(action);
            else
                binding = MWBase::Environment::get().getInputManager()->getActionControllerBindingName(action);

            Gui::SharedStateButton* leftText = mControlsBox->createWidget<Gui::SharedStateButton>("SandTextButton", MyGUI::IntCoord(), MyGUI::Align::Default);
            leftText->setCaptionWithReplacing(desc);

            Gui::SharedStateButton* rightText = mControlsBox->createWidget<Gui::SharedStateButton>("SandTextButton", MyGUI::IntCoord(), MyGUI::Align::Default);
            rightText->setCaptionWithReplacing(binding);
            rightText->setTextAlign (MyGUI::Align::Right);
            rightText->setUserData(action); // save the action id for callbacks
            rightText->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onRebindAction);
            rightText->eventMouseWheel += MyGUI::newDelegate(this, &SettingsWindow::onInputTabMouseWheel);

            Gui::ButtonGroup group;
            group.push_back(leftText);
            group.push_back(rightText);
            Gui::SharedStateButton::createButtonGroup(group);
        }

        layoutControlsBox();
    }

    void SettingsWindow::updateWaterShaderModeCombo()
    {
        const std::string mode = getWaterShaderMode();
        for (size_t i = 0; i < waterShaderModes.size(); ++i)
        {
            if (mode == waterShaderModes[i])
            {
                mWaterShaderMode->setIndexSelected(i);
                return;
            }
        }
        mWaterShaderMode->setIndexSelected(2);
    }

    void SettingsWindow::selectLightingMethod(const std::string& value)
    {
        for (size_t i = 0; i < mLightingMethodValues.size(); ++i)
        {
            if (mLightingMethodValues[i] == value)
            {
                mLightingMethodButton->setIndexSelected(i);
                return;
            }
        }
        mLightingMethodButton->setIndexSelected(MyGUI::ITEM_NONE);
    }

    void SettingsWindow::setPostProcessingChain(const std::vector<std::string>& chain)
    {
        Settings::Manager::setString("chain", "Post Processing", joinPostChain(chain));
        apply();
        Settings::Manager::saveUser();
        refreshPostProcessingLists();
    }

    void SettingsWindow::refreshPostProcessingLists()
    {
        const std::string previous = mPostSelectedEffect;
        mPostInactiveEffects->removeAllItems();
        mPostActiveEffects->removeAllItems();

        MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor();
        if (!processor)
        {
            mPostSelectedEffect.clear();
            rebuildPostEffectConfig();
            return;
        }

        const std::vector<std::string>& chain = processor->getChain();
        const std::unordered_set<std::string> active(chain.begin(), chain.end());
        const std::string filter = lowerAscii(mPostEffectFilter->getCaption().asUTF8());

        for (const std::string& effect : processor->getAvailableEffects())
        {
            if (active.find(effect) != active.end())
                continue;
            if (!filter.empty() && lowerAscii(effect).find(filter) == std::string::npos)
                continue;
            mPostInactiveEffects->addItem(effect);
        }

        for (const std::string& effect : chain)
            mPostActiveEffects->addItem(effect);

        auto restoreSelection = [this, &previous](MyGUI::ListBox* list) -> bool
        {
            if (previous.empty())
                return false;
            for (size_t i = 0; i < list->getItemCount(); ++i)
            {
                if (list->getItemNameAt(i).asUTF8() == previous)
                {
                    list->setIndexSelected(i);
                    return true;
                }
            }
            return false;
        };

        bool restored = restoreSelection(mPostActiveEffects);
        if (!restored)
            restored = restoreSelection(mPostInactiveEffects);

        if (!restored)
        {
            if (mPostActiveEffects->getItemCount() > 0)
            {
                mPostActiveEffects->setIndexSelected(0);
                mPostSelectedEffect = mPostActiveEffects->getItemNameAt(0).asUTF8();
            }
            else if (mPostInactiveEffects->getItemCount() > 0)
            {
                mPostInactiveEffects->setIndexSelected(0);
                mPostSelectedEffect = mPostInactiveEffects->getItemNameAt(0).asUTF8();
            }
            else
                mPostSelectedEffect.clear();
        }

        const bool inactiveSelected = mPostInactiveEffects->getIndexSelected() != MyGUI::ITEM_NONE;
        const bool activeSelected = mPostActiveEffects->getIndexSelected() != MyGUI::ITEM_NONE;
        const size_t activeIndex = mPostActiveEffects->getIndexSelected();
        mPostEffectActivate->setEnabled(inactiveSelected);
        mPostEffectDeactivate->setEnabled(activeSelected);
        mPostEffectUp->setEnabled(activeSelected && activeIndex != MyGUI::ITEM_NONE && activeIndex > 0);
        mPostEffectDown->setEnabled(activeSelected && activeIndex != MyGUI::ITEM_NONE
            && activeIndex + 1 < mPostActiveEffects->getItemCount());

        rebuildPostEffectConfig();
    }

    void SettingsWindow::onPostEffectFilterChanged(MyGUI::EditBox* /*sender*/)
    {
        refreshPostProcessingLists();
    }

    void SettingsWindow::onPostEffectListChanged(MyGUI::ListBox* sender, size_t index)
    {
        if (index == MyGUI::ITEM_NONE || index >= sender->getItemCount())
            return;

        if (sender == mPostActiveEffects)
            mPostInactiveEffects->clearIndexSelected();
        else
            mPostActiveEffects->clearIndexSelected();

        mPostSelectedEffect = sender->getItemNameAt(index).asUTF8();
        const bool activeSelected = sender == mPostActiveEffects;
        mPostEffectActivate->setEnabled(!activeSelected);
        mPostEffectDeactivate->setEnabled(activeSelected);
        mPostEffectUp->setEnabled(activeSelected && index > 0);
        mPostEffectDown->setEnabled(activeSelected && index + 1 < sender->getItemCount());
        rebuildPostEffectConfig();
    }

    void SettingsWindow::onPostEffectActivateClicked(MyGUI::Widget* /*sender*/)
    {
        const size_t selected = mPostInactiveEffects->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mPostInactiveEffects->getItemCount())
            return;

        MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor();
        if (!processor)
            return;

        const std::string effect = mPostInactiveEffects->getItemNameAt(selected).asUTF8();
        std::vector<std::string> chain = processor->getChain();
        if (std::find(chain.begin(), chain.end(), effect) == chain.end())
            chain.push_back(effect);
        mPostSelectedEffect = effect;
        setPostProcessingChain(chain);
    }

    void SettingsWindow::onPostEffectDeactivateClicked(MyGUI::Widget* /*sender*/)
    {
        const size_t selected = mPostActiveEffects->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mPostActiveEffects->getItemCount())
            return;

        MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor();
        if (!processor)
            return;

        const std::string effect = mPostActiveEffects->getItemNameAt(selected).asUTF8();
        std::vector<std::string> chain = processor->getChain();
        chain.erase(std::remove(chain.begin(), chain.end(), effect), chain.end());
        mPostSelectedEffect = effect;
        setPostProcessingChain(chain);
    }

    void SettingsWindow::onPostEffectUpClicked(MyGUI::Widget* /*sender*/)
    {
        const size_t selected = mPostActiveEffects->getIndexSelected();
        MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor();
        if (!processor || selected == MyGUI::ITEM_NONE || selected == 0)
            return;
        std::vector<std::string> chain = processor->getChain();
        if (selected >= chain.size())
            return;
        mPostSelectedEffect = chain[selected];
        std::swap(chain[selected], chain[selected - 1]);
        setPostProcessingChain(chain);
    }

    void SettingsWindow::onPostEffectDownClicked(MyGUI::Widget* /*sender*/)
    {
        const size_t selected = mPostActiveEffects->getIndexSelected();
        MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor();
        if (!processor || selected == MyGUI::ITEM_NONE)
            return;
        std::vector<std::string> chain = processor->getChain();
        if (selected + 1 >= chain.size())
            return;
        mPostSelectedEffect = chain[selected];
        std::swap(chain[selected], chain[selected + 1]);
        setPostProcessingChain(chain);
    }

    void SettingsWindow::onPostUniformSliderChanged(MyGUI::ScrollBar* sender, size_t pos)
    {
        auto bindingIt = mPostUniformSliders.find(sender);
        if (bindingIt == mPostUniformSliders.end())
            return;

        const PostUniformSliderBinding& binding = bindingIt->second;
        std::array<float, 4> value = postStoredUniformValue(
            binding.mEffect, binding.mUniform, binding.mDefault, binding.mComponents);
        float component = binding.mMin + static_cast<float>(pos) * binding.mStep;
        component = std::max(binding.mMin, std::min(component, binding.mMax));
        if (binding.mInteger)
            component = static_cast<float>(std::lround(component));
        value[static_cast<std::size_t>(binding.mComponent)] = component;

        Settings::Manager::setString(binding.mUniform, "Post Processing " + binding.mEffect,
            serializePostUniform(value, binding.mComponents, binding.mInteger));
        if (binding.mLabel)
            binding.mLabel->setCaption(binding.mCaption + ": " + formatPostValue(component, binding.mInteger));
        apply();
        Settings::Manager::saveUser();
    }

    void SettingsWindow::onPostUniformChoiceChanged(MyGUI::ComboBox* sender, size_t pos)
    {
        auto bindingIt = mPostUniformChoices.find(sender);
        if (bindingIt == mPostUniformChoices.end())
            return;
        const PostUniformChoiceBinding& binding = bindingIt->second;
        if (pos == MyGUI::ITEM_NONE || pos >= binding.mValues.size())
            return;

        Settings::Manager::setString(binding.mUniform, "Post Processing " + binding.mEffect,
            serializePostUniform(binding.mValues[pos], binding.mComponents, binding.mInteger));
        apply();
        Settings::Manager::saveUser();
    }

    void SettingsWindow::onPostUniformBoolClicked(MyGUI::Widget* sender)
    {
        MyGUI::Button* button = sender->castType<MyGUI::Button>(false);
        if (!button)
            return;
        auto bindingIt = mPostUniformBools.find(button);
        if (bindingIt == mPostUniformBools.end())
            return;

        const PostUniformBoolBinding& binding = bindingIt->second;
        const auto key = std::make_pair(std::string("Post Processing ") + binding.mEffect, binding.mUniform);
        bool current = binding.mDefault;
        auto userIt = Settings::Manager::mUserSettings.find(key);
        if (userIt != Settings::Manager::mUserSettings.end())
            current = Misc::StringUtils::ciEqual(userIt->second, "true");
        else
        {
            auto defaultIt = Settings::Manager::mDefaultSettings.find(key);
            if (defaultIt != Settings::Manager::mDefaultSettings.end())
                current = Misc::StringUtils::ciEqual(defaultIt->second, "true");
        }
        current = !current;
        Settings::Manager::setBool(binding.mUniform, "Post Processing " + binding.mEffect, current);
        button->setCaptionWithReplacing(current ? "#{sOn}" : "#{sOff}");
        apply();
        Settings::Manager::saveUser();
    }

    void SettingsWindow::onPostEffectResetClicked(MyGUI::Widget* /*sender*/)
    {
        if (mPostSelectedEffect.empty())
            return;
        MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor();
        if (!processor)
            return;
        std::shared_ptr<Fx::Technique> technique = processor->getTechniqueInfo(mPostSelectedEffect);
        if (!technique)
            return;

        const std::string category = "Post Processing " + mPostSelectedEffect;
        for (const Fx::UniformSpec& uniform : technique->uniforms())
        {
            if (!uniform.mStatic)
                continue;
            if (uniform.mType == Fx::UniformType::Bool)
                Settings::Manager::setBool(uniform.mName, category, uniform.mDefault[0] != 0.f);
            else
                Settings::Manager::setString(uniform.mName, category,
                    serializePostUniform(uniform.mDefault, uniform.components(), uniform.mType == Fx::UniformType::Int));
        }
        apply();
        Settings::Manager::saveUser();
        rebuildPostEffectConfig();
    }

    void SettingsWindow::rebuildPostEffectConfig()
    {
        mPostUniformSliders.clear();
        mPostUniformBools.clear();
        mPostUniformChoices.clear();
        while (mPostEffectConfigArea->getChildCount() > 0)
            MyGUI::Gui::getInstance().destroyWidget(mPostEffectConfigArea->getChildAt(0));

        const int width = std::max(260, mPostEffectConfigScroll->getWidth() - 24);
        int y = 4;

        auto addMessage = [this, width, &y](const std::string& text, const std::string& skin = "NormalText")
        {
            MyGUI::TextBox* label = mPostEffectConfigArea->createWidget<MyGUI::TextBox>(
                skin, MyGUI::IntCoord(4, y, width - 8, 42), MyGUI::Align::Default);
            label->setCaption(text);
            y += 48;
        };

        MWRender::PostProcessor* processor = MWRender::PostProcessor::getActiveProcessor();
        if (!processor)
        {
            addMessage(arenaText("settings.postfx_not_ready"), "SandText");
            mPostEffectConfigArea->setSize(width, std::max(y, 120));
            mPostEffectConfigScroll->setCanvasSize(width, std::max(y, 120));
            return;
        }
        if (mPostSelectedEffect.empty())
        {
            addMessage(arenaText("settings.postfx_select_hint"), "SandText");
            mPostEffectConfigArea->setSize(width, std::max(y, 120));
            mPostEffectConfigScroll->setCanvasSize(width, std::max(y, 120));
            return;
        }

        std::shared_ptr<Fx::Technique> technique = processor->getTechniqueInfo(mPostSelectedEffect);
        if (!technique)
        {
            addMessage(arenaText("settings.postfx_parse_error"), "SandText");
            mPostEffectConfigArea->setSize(width, std::max(y, 120));
            mPostEffectConfigScroll->setCanvasSize(width, std::max(y, 120));
            return;
        }

        MyGUI::TextBox* title = mPostEffectConfigArea->createWidget<MyGUI::TextBox>(
            "SandText", MyGUI::IntCoord(4, y, width - 8, 24), MyGUI::Align::Default);
        title->setCaption(mPostSelectedEffect);
        y += 26;

        const std::string author = technique->author().empty() ? arenaText("settings.postfx_na") : technique->author();
        const std::string version = technique->version().empty() ? arenaText("settings.postfx_na") : technique->version();
        const std::string description = localizePostFxText(technique->description(), arenaText("settings.postfx_no_description"));

        std::ostringstream info;
        info << arenaText("settings.postfx_author") << ": " << author << "    "
             << arenaText("settings.postfx_version") << ": " << version << '\n'
             << description << '\n';
        const Fx::FlagsType flags = technique->flags();
        info << arenaText("settings.postfx_scope") << ": "
             << ((flags & Fx::Technique::Flag_Disable_Interiors) ? "-" : arenaText("settings.postfx_interiors")) << "  "
             << ((flags & Fx::Technique::Flag_Disable_Exteriors) ? "-" : arenaText("settings.postfx_exteriors")) << "  "
             << ((flags & Fx::Technique::Flag_Disable_Underwater) ? "-" : arenaText("settings.postfx_underwater")) << "  "
             << ((flags & Fx::Technique::Flag_Disable_Abovewater) ? "-" : arenaText("settings.postfx_abovewater"));

        MyGUI::EditBox* infoBox = mPostEffectConfigArea->createWidget<MyGUI::EditBox>(
            "MW_TextBoxEditWithBorder", MyGUI::IntCoord(4, y, width - 8, 94), MyGUI::Align::Default);
        infoBox->setEditReadOnly(true);
        infoBox->setEditMultiLine(true);
        infoBox->setEditWordWrap(true);
        infoBox->setNeedMouseFocus(false);
        infoBox->setCaption(info.str());
        y += 102;

        MyGUI::Button* reset = mPostEffectConfigArea->createWidget<MyGUI::Button>(
            "MW_Button", MyGUI::IntCoord(4, y, 170, 26), MyGUI::Align::Default);
        reset->setCaption(arenaText("settings.postfx_reset"));
        reset->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onPostEffectResetClicked);
        y += 36;

        int visibleUniforms = 0;
        for (const Fx::UniformSpec& uniform : technique->uniforms())
        {
            if (!uniform.mStatic)
                continue;
            ++visibleUniforms;

            if (!uniform.mHeader.empty())
            {
                MyGUI::TextBox* header = mPostEffectConfigArea->createWidget<MyGUI::TextBox>(
                    "SandText", MyGUI::IntCoord(4, y, width - 8, 22), MyGUI::Align::Default);
                header->setCaption(localizePostFxText(uniform.mHeader, uniform.mHeader));
                y += 26;
            }

            const std::string caption = localizePostFxText(uniform.mDisplayName, uniform.mName);
            const std::string descriptionText = localizePostFxText(uniform.mDescription, std::string());
            const std::array<float, 4> current = postUniformValue(mPostSelectedEffect, uniform);

            if (!uniform.mChoices.empty())
            {
                MyGUI::TextBox* label = mPostEffectConfigArea->createWidget<MyGUI::TextBox>(
                    "NormalText", MyGUI::IntCoord(4, y, width - 8, 20), MyGUI::Align::Default);
                label->setCaption(caption);
                if (!descriptionText.empty())
                {
                    label->setUserString("ToolTipType", "Layout");
                    label->setUserString("ToolTipLayout", "TextToolTip");
                    label->setUserString("Caption_Text", descriptionText);
                }
                y += 22;

                MyGUI::ComboBox* choices = mPostEffectConfigArea->createWidget<MyGUI::ComboBox>(
                    "MW_ComboBox", MyGUI::IntCoord(4, y, width - 8, 24), MyGUI::Align::Default);
                PostUniformChoiceBinding binding;
                binding.mEffect = mPostSelectedEffect;
                binding.mUniform = uniform.mName;
                binding.mComponents = uniform.components();
                binding.mInteger = uniform.mType == Fx::UniformType::Int;
                binding.mDefault = uniform.mDefault;

                size_t selectedChoice = MyGUI::ITEM_NONE;
                for (const Fx::UniformChoice& choice : uniform.mChoices)
                {
                    choices->addItem(localizePostFxText(choice.mLabel, choice.mLabel));
                    binding.mValues.push_back(choice.mValue);
                    bool matches = true;
                    for (int component = 0; component < binding.mComponents; ++component)
                    {
                        if (std::abs(choice.mValue[static_cast<std::size_t>(component)]
                                - current[static_cast<std::size_t>(component)]) > 0.0001f)
                        {
                            matches = false;
                            break;
                        }
                    }
                    if (matches)
                        selectedChoice = binding.mValues.size() - 1u;
                }
                if (selectedChoice != MyGUI::ITEM_NONE)
                    choices->setIndexSelected(selectedChoice);
                mPostUniformChoices.emplace(choices, binding);
                choices->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onPostUniformChoiceChanged);
                y += 34;
                continue;
            }

            if (uniform.mType == Fx::UniformType::Bool)
            {
                MyGUI::Button* toggle = mPostEffectConfigArea->createWidget<MyGUI::Button>(
                    "MW_Button", MyGUI::IntCoord(4, y, 54, 24), MyGUI::Align::Default);
                toggle->setCaptionWithReplacing(current[0] != 0.f ? "#{sOn}" : "#{sOff}");
                MyGUI::TextBox* label = mPostEffectConfigArea->createWidget<MyGUI::TextBox>(
                    "NormalText", MyGUI::IntCoord(66, y + 3, width - 72, 20), MyGUI::Align::Default);
                label->setCaption(caption);
                if (!descriptionText.empty())
                {
                    label->setUserString("ToolTipType", "Layout");
                    label->setUserString("ToolTipLayout", "TextToolTip");
                    label->setUserString("Caption_Text", descriptionText);
                }
                PostUniformBoolBinding binding;
                binding.mEffect = mPostSelectedEffect;
                binding.mUniform = uniform.mName;
                binding.mCaption = caption;
                binding.mDefault = uniform.mDefault[0] != 0.f;
                binding.mLabel = label;
                mPostUniformBools.emplace(toggle, binding);
                toggle->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsWindow::onPostUniformBoolClicked);
                y += 32;
                continue;
            }

            const int components = uniform.components();
            static const char* componentNames[4] = { "X", "Y", "Z", "W" };
            for (int component = 0; component < components; ++component)
            {
                float minimum = uniform.mMin[static_cast<std::size_t>(component)];
                float maximum = uniform.mMax[static_cast<std::size_t>(component)];
                const float defaultValue = uniform.mDefault[static_cast<std::size_t>(component)];
                if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum
                    || maximum - minimum > 1000000.f)
                {
                    const float span = std::max(1.f, std::abs(defaultValue) * 2.f);
                    minimum = defaultValue - span;
                    maximum = defaultValue + span;
                }

                const bool integer = uniform.mType == Fx::UniformType::Int;
                float requestedStep = uniform.mStep;
                if (!std::isfinite(requestedStep) || requestedStep <= 0.f)
                    requestedStep = integer ? 1.f : (maximum - minimum) / 100.f;
                if (integer)
                    requestedStep = std::max(1.f, static_cast<float>(std::lround(requestedStep)));

                std::size_t range = static_cast<std::size_t>(std::ceil((maximum - minimum) / requestedStep)) + 1u;
                range = std::max<std::size_t>(2u, std::min<std::size_t>(range, 5001u));
                const float effectiveStep = (maximum - minimum) / static_cast<float>(range - 1u);

                std::string componentCaption = caption;
                if (components > 1)
                    componentCaption += std::string(" [") + componentNames[component] + "]";

                MyGUI::TextBox* label = mPostEffectConfigArea->createWidget<MyGUI::TextBox>(
                    "NormalText", MyGUI::IntCoord(4, y, width - 8, 20), MyGUI::Align::Default);
                const float currentValue = std::max(minimum, std::min(current[static_cast<std::size_t>(component)], maximum));
                label->setCaption(componentCaption + ": " + formatPostValue(currentValue, integer));
                if (!descriptionText.empty())
                {
                    label->setUserString("ToolTipType", "Layout");
                    label->setUserString("ToolTipLayout", "TextToolTip");
                    label->setUserString("Caption_Text", descriptionText);
                }
                y += 22;

                MyGUI::ScrollBar* slider = mPostEffectConfigArea->createWidget<MyGUI::ScrollBar>(
                    "MW_HScroll", MyGUI::IntCoord(4, y, width - 8, 16), MyGUI::Align::Default);
                slider->setScrollRange(range);
                const std::size_t position = static_cast<std::size_t>(std::lround((currentValue - minimum) / effectiveStep));
                slider->setScrollPosition(std::min(position, range - 1u));

                PostUniformSliderBinding binding;
                binding.mEffect = mPostSelectedEffect;
                binding.mUniform = uniform.mName;
                binding.mCaption = componentCaption;
                binding.mComponent = component;
                binding.mComponents = components;
                binding.mMin = minimum;
                binding.mMax = maximum;
                binding.mStep = effectiveStep;
                binding.mInteger = integer;
                binding.mDefault = uniform.mDefault;
                binding.mLabel = label;
                mPostUniformSliders.emplace(slider, binding);
                slider->eventScrollChangePosition += MyGUI::newDelegate(this, &SettingsWindow::onPostUniformSliderChanged);
                y += 30;
            }
        }

        if (visibleUniforms == 0)
            addMessage(arenaText("settings.postfx_no_settings"));

        const int canvasHeight = std::max(y + 8, mPostEffectConfigScroll->getHeight() - 4);
        mPostEffectConfigArea->setSize(width, canvasHeight);
        mPostEffectConfigScroll->setCanvasSize(width, canvasHeight);
        mPostEffectConfigScroll->setViewOffset(MyGUI::IntPoint(0, 0));
    }

    void SettingsWindow::updateLightSettings()
    {
        auto lightingMethod = MWBase::Environment::get().getResourceSystem()->getSceneManager()->getLightingMethod();
        const std::string lightingMethodStr = SceneUtil::LightManager::getLightingMethodString(lightingMethod);

        mLightingMethodButton->removeAllItems();
        mLightingMethodValues.clear();

        struct LightingChoice
        {
            SceneUtil::LightingMethod mMethod;
            const char* mLabelKey;
        };
        const std::array<LightingChoice, 3> methods = {{
            { SceneUtil::LightingMethod::FFP, "value.lighting_legacy" },
            { SceneUtil::LightingMethod::PerObjectUniform, "value.lighting_compatibility" },
            { SceneUtil::LightingMethod::SingleUBO, "value.lighting_shaders" },
        }};

        for (const LightingChoice& choice : methods)
        {
            if (!MWBase::Environment::get().getResourceSystem()->getSceneManager()->isSupportedLightingMethod(choice.mMethod))
                continue;

            mLightingMethodValues.push_back(SceneUtil::LightManager::getLightingMethodString(choice.mMethod));
            mLightingMethodButton->addItem(arenaText(choice.mLabelKey));
        }

        selectLightingMethod(lightingMethodStr);
    }

    void SettingsWindow::layoutControlsBox()
    {
        const int h = 18;
        const int w = mControlsBox->getWidth() - 28;
        const int noWidgetsInRow = 2;
        const int totalH = mControlsBox->getChildCount() / noWidgetsInRow * h;

        for (size_t i = 0; i < mControlsBox->getChildCount(); i++)
        {
            MyGUI::Widget * widget = mControlsBox->getChildAt(i);
            widget->setCoord(0, i / noWidgetsInRow * h, w, h);
        }

        // Canvas size must be expressed with VScroll disabled, otherwise MyGUI would expand the scroll area when the scrollbar is hidden
        mControlsBox->setVisibleVScroll(false);
        mControlsBox->setCanvasSize (mControlsBox->getWidth(), std::max(totalH, mControlsBox->getHeight()));
        mControlsBox->setVisibleVScroll(true);
    }

    void SettingsWindow::onRebindAction(MyGUI::Widget* _sender)
    {
        int actionId = *_sender->getUserData<int>();

        _sender->castType<MyGUI::Button>()->setCaptionWithReplacing("#{sNone}");

        MWBase::Environment::get().getWindowManager ()->staticMessageBox ("#{sControlsMenu3}");
        MWBase::Environment::get().getWindowManager ()->disallowMouse();

        MWBase::Environment::get().getInputManager ()->enableDetectingBindingMode (actionId, mKeyboardMode);

    }

    void SettingsWindow::onInputTabMouseWheel(MyGUI::Widget* _sender, int _rel)
    {
        if (mControlsBox->getViewOffset().top + _rel*0.3f > 0)
            mControlsBox->setViewOffset(MyGUI::IntPoint(0, 0));
        else
            mControlsBox->setViewOffset(MyGUI::IntPoint(0, static_cast<int>(mControlsBox->getViewOffset().top + _rel*0.3f)));
    }

    void SettingsWindow::onResetDefaultBindings(MyGUI::Widget* _sender)
    {
        ConfirmationDialog* dialog = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
        dialog->askForConfirmation("#{sNotifyMessage66}");
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &SettingsWindow::onResetDefaultBindingsAccept);
        dialog->eventCancelClicked.clear();
    }

    void SettingsWindow::onResetDefaultBindingsAccept()
    {
        if(mKeyboardMode)
            MWBase::Environment::get().getInputManager ()->resetToDefaultKeyBindings ();
        else
            MWBase::Environment::get().getInputManager()->resetToDefaultControllerBindings();
        updateControlsBox ();
    }

    void SettingsWindow::onOpen()
    {
        highlightCurrentResolution();
        updateControlsBox();
        updateWaterShaderModeCombo();
        updateLightSettings();
        updateWeaponSpellBoxModeCombo();
        updateResourceBarModeCombo();
        updateNpcBarModeCombo();
        updateChatModeCombo();
        updateQuickLootModeCombo();
        updateTerrainPresetCombo();
        updateMaterialQualityCombo();
        updateLandOptimizationModeCombo();
        updateLandDistanceControls();
        updateShadowCombos();
        configureWidgets(mPostProcessingScroll, false);
        refreshPostProcessingLists();
        resetScrollbars();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mOkButton);
    }

    void SettingsWindow::onWindowResize(MyGUI::Window *_sender)
    {
        if (_sender->getWidth() != settingsWindowWidth || _sender->getHeight() != settingsWindowHeight)
        {
            _sender->setSize(settingsWindowWidth, settingsWindowHeight);
            center();
        }
        layoutControlsBox();
    }

    void SettingsWindow::resetScrollbars()
    {
        mResolutionList->setScrollPosition(0);
        mControlsBox->setViewOffset(MyGUI::IntPoint(0, 0));
        mInterfaceScroll->setViewOffset(MyGUI::IntPoint(0, 0));
        mHudScroll->setViewOffset(MyGUI::IntPoint(0, 0));
        mDisplayScroll->setViewOffset(MyGUI::IntPoint(0, 0));
        mWaterScroll->setViewOffset(MyGUI::IntPoint(0, 0));
        mPostProcessingScroll->setViewOffset(MyGUI::IntPoint(0, 0));
        mPostEffectConfigScroll->setViewOffset(MyGUI::IntPoint(0, 0));
    }
}
