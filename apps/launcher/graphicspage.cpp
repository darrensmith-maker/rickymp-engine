#include "graphicspage.hpp"

#include <QDesktopWidget>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScreen>
#include <QThread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#endif

#ifdef MAC_OS_X_VERSION_MIN_REQUIRED
#undef MAC_OS_X_VERSION_MIN_REQUIRED
// We need to do this because of Qt: https://bugreports.qt-project.org/browse/QTBUG-22154
#define MAC_OS_X_VERSION_MIN_REQUIRED __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#endif // MAC_OS_X_VERSION_MIN_REQUIRED

#include <SDL_video.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <numeric>

#include <components/config/launchersettings.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/settings/parser.hpp>

QString getAspect(int x, int y)
{
    int gcd = std::gcd (x, y);
    if (gcd == 0)
        return QString();

    int xaspect = x / gcd;
    int yaspect = y / gcd;
    // special case: 8 : 5 is usually referred to as 16:10
    if (xaspect == 8 && yaspect == 5)
        return QString("16:10");

    return QString(QString::number(xaspect) + ":" + QString::number(yaspect));
}

Launcher::GraphicsPage::GraphicsPage(Config::LauncherSettings& launcherSettings, QWidget *parent)
    : QWidget(parent)
    , mLauncherSettings(launcherSettings)
    , mRecommendedQuality(2)
    , mInitializingQuality(false)
{
    setObjectName ("GraphicsPage");
    setupUi(this);

    // Set the maximum res we can set in windowed mode
    QRect res = getMaximumResolution();
    customWidthSpinBox->setMaximum(res.width());
    customHeightSpinBox->setMaximum(res.height());

    connect(fullScreenCheckBox, SIGNAL(stateChanged(int)), this, SLOT(slotFullScreenChanged(int)));
    connect(standardRadioButton, SIGNAL(toggled(bool)), this, SLOT(slotStandardToggled(bool)));
    connect(screenComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(screenChanged(int)));
    connect(framerateLimitCheckBox, SIGNAL(toggled(bool)), this, SLOT(slotFramerateLimitToggled(bool)));
    connect(shadowDistanceCheckBox, SIGNAL(toggled(bool)), this, SLOT(slotShadowDistLimitToggled(bool)));
    connect(qualityPresetComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(slotQualityPresetChanged(int)));
    connect(terrainDetailComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(slotTerrainDetailChanged(int)));
    connect(pbrQualityComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(slotPbrQualityChanged(int)));
    connect(applyQualityButton, SIGNAL(clicked()), this, SLOT(slotApplyQualityPreset()));
    connect(detectHardwareButton, SIGNAL(clicked()), this, SLOT(slotDetectHardware()));

}

bool Launcher::GraphicsPage::setupSDL()
{
    bool sdlConnectSuccessful = initSDL();
    if (!sdlConnectSuccessful)
    {
        return false;
    }

    int displays = SDL_GetNumVideoDisplays();

    if (displays < 0)
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error receiving number of screens"));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(tr("<br><b>SDL_GetNumVideoDisplays failed:</b><br><br>") + QString::fromUtf8(SDL_GetError()) + "<br>");
        msgBox.exec();
        return false;
    }

    screenComboBox->clear();
    mResolutionsPerScreen.clear();
    for (int i = 0; i < displays; i++)
    {
        mResolutionsPerScreen.append(getAvailableResolutions(i));
        screenComboBox->addItem(QString(tr("Screen ")) + QString::number(i + 1));
    }

    // The Wizard owns one-time native display initialization. Merely opening
    // the Launcher must never write resolution, fullscreen or GUI scaling
    // values back to settings.cfg.
    screenChanged(0);

    // Disconnect from SDL processes
    quitSDL();

    return true;
}

bool Launcher::GraphicsPage::loadSettings()
{
    if (!setupSDL())
        return false;

    initializeQualityPage();
    syncGraphicsControls();
    return true;
}

void Launcher::GraphicsPage::syncGraphicsControls()
{
    // Display settings
    vSyncCheckBox->setChecked(Settings::Manager::getBool("vsync", "Video"));
    fullScreenCheckBox->setChecked(Settings::Manager::getBool("fullscreen", "Video"));
    windowBorderCheckBox->setChecked(Settings::Manager::getBool("window border", "Video"));

    const int aaValue = Settings::Manager::getInt("antialiasing", "Video");
    const int aaIndex = antiAliasingComboBox->findText(QString::number(aaValue));
    if (aaIndex != -1)
        antiAliasingComboBox->setCurrentIndex(aaIndex);

    const int width = Settings::Manager::getInt("resolution x", "Video");
    const int height = Settings::Manager::getInt("resolution y", "Video");
    const QString resolution = QString::number(width) + QStringLiteral(" x ") + QString::number(height);
    screenComboBox->setCurrentIndex(Settings::Manager::getInt("screen", "Video"));

    const int resIndex = resolutionComboBox->findText(resolution, Qt::MatchStartsWith);
    if (resIndex != -1)
    {
        standardRadioButton->setChecked(true);
        resolutionComboBox->setCurrentIndex(resIndex);
    }
    else
    {
        customRadioButton->setChecked(true);
        customWidthSpinBox->setValue(width);
        customHeightSpinBox->setValue(height);
    }

    const float fpsLimit = Settings::Manager::getFloat("framerate limit", "Video");
    framerateLimitCheckBox->setChecked(fpsLimit != 0.f);
    if (fpsLimit != 0.f)
        framerateLimitSpinBox->setValue(fpsLimit);

    // ArenaMP terrain and PBR controls. Maximum PBR is represented only with
    // High/Ultra terrain, including profiles created by older builds.
    int terrainIndex = terrainDetailIndexFromSettings();
    const int pbrIndex = pbrQualityIndexFromSettings();
    if (pbrIndex == 4 && terrainIndex < 4)
        terrainIndex = 4;
    terrainDetailComboBox->setCurrentIndex(terrainIndex);
    pbrQualityComboBox->setCurrentIndex(pbrIndex);

    // Lighting
    int lightingMethod = 1;
    if (Settings::Manager::getString("lighting method", "Shaders") == "legacy")
        lightingMethod = 0;
    else if (Settings::Manager::getString("lighting method", "Shaders") == "shaders")
        lightingMethod = 2;
    if (pbrIndex > 0 && lightingMethod == 0)
        lightingMethod = 1;
    lightingMethodComboBox->setCurrentIndex(lightingMethod);

    // Shadows
    actorShadowsCheckBox->setChecked(Settings::Manager::getBool("actor shadows", "Shadows"));
    playerShadowsCheckBox->setChecked(Settings::Manager::getBool("player shadows", "Shadows"));
    terrainShadowsCheckBox->setChecked(Settings::Manager::getBool("terrain shadows", "Shadows"));
    objectShadowsCheckBox->setChecked(Settings::Manager::getBool("object shadows", "Shadows"));
    indoorShadowsCheckBox->setChecked(Settings::Manager::getBool("enable indoor shadows", "Shadows"));

    const QString computeBounds = QString::fromStdString(
        Settings::Manager::getString("compute scene bounds", "Shadows"));
    const int computeIndex = shadowComputeSceneBoundsComboBox->findText(computeBounds);
    if (computeIndex != -1)
        shadowComputeSceneBoundsComboBox->setCurrentIndex(computeIndex);

    const int shadowDistLimit = Settings::Manager::getInt("maximum shadow map distance", "Shadows");
    shadowDistanceCheckBox->setChecked(shadowDistLimit > 0);
    if (shadowDistLimit > 0)
        shadowDistanceSpinBox->setValue(shadowDistLimit);

    const float shadowFadeStart = Settings::Manager::getFloat("shadow fade start", "Shadows");
    if (shadowFadeStart != 0.f)
        fadeStartSpinBox->setValue(shadowFadeStart);

    const int shadowRes = Settings::Manager::getInt("shadow map resolution", "Shadows");
    const int shadowResIndex = shadowResolutionComboBox->findText(QString::number(shadowRes));
    if (shadowResIndex != -1)
        shadowResolutionComboBox->setCurrentIndex(shadowResIndex);

    slotFullScreenChanged(fullScreenCheckBox->checkState());
    slotStandardToggled(standardRadioButton->isChecked());
    slotFramerateLimitToggled(framerateLimitCheckBox->isChecked());
    slotShadowDistLimitToggled(shadowDistanceCheckBox->isChecked());
}

void Launcher::GraphicsPage::saveSettings()
{
    // Visuals

    // Ensure we only set the new settings if they changed. This is to avoid cluttering the
    // user settings file (which by definition should only contain settings the user has touched)
    bool cVSync = vSyncCheckBox->checkState();
    if (cVSync != Settings::Manager::getBool("vsync", "Video"))
        Settings::Manager::setBool("vsync", "Video", cVSync);

    bool cFullScreen = fullScreenCheckBox->checkState();
    if (cFullScreen != Settings::Manager::getBool("fullscreen", "Video"))
        Settings::Manager::setBool("fullscreen", "Video", cFullScreen);

    bool cWindowBorder = windowBorderCheckBox->checkState();
    if (cWindowBorder != Settings::Manager::getBool("window border", "Video"))
        Settings::Manager::setBool("window border", "Video", cWindowBorder);

    int cAAValue = antiAliasingComboBox->currentText().toInt();
    if (cAAValue != Settings::Manager::getInt("antialiasing", "Video"))
        Settings::Manager::setInt("antialiasing", "Video", cAAValue);

    int cWidth = 0;
    int cHeight = 0;
    if (standardRadioButton->isChecked()) {
        QRegExp resolutionRe(QString("(\\d+) x (\\d+).*"));
        if (resolutionRe.exactMatch(resolutionComboBox->currentText().simplified())) {
            cWidth = resolutionRe.cap(1).toInt();
            cHeight = resolutionRe.cap(2).toInt();
        }
    } else {
        cWidth = customWidthSpinBox->value();
        cHeight = customHeightSpinBox->value();
    }

    if (cWidth != Settings::Manager::getInt("resolution x", "Video"))
        Settings::Manager::setInt("resolution x", "Video", cWidth);

    if (cHeight != Settings::Manager::getInt("resolution y", "Video"))
        Settings::Manager::setInt("resolution y", "Video", cHeight);

    int cScreen = screenComboBox->currentIndex();
    if (cScreen != Settings::Manager::getInt("screen", "Video"))
        Settings::Manager::setInt("screen", "Video", cScreen);

    if (framerateLimitCheckBox->checkState() != Qt::Unchecked)
    {
        float cFpsLimit = framerateLimitSpinBox->value();
        if (cFpsLimit != Settings::Manager::getFloat("framerate limit", "Video"))
            Settings::Manager::setFloat("framerate limit", "Video", cFpsLimit);
    }
    else if (Settings::Manager::getFloat("framerate limit", "Video") != 0)
    {
        Settings::Manager::setFloat("framerate limit", "Video", 0);
    }

    int terrainIndex = std::max(0, std::min(5, terrainDetailComboBox->currentIndex()));
    int pbrIndex = std::max(0, std::min(4, pbrQualityComboBox->currentIndex()));
    if (pbrIndex == 4 && terrainIndex < 4)
    {
        terrainIndex = 4;
        terrainDetailComboBox->setCurrentIndex(terrainIndex);
    }
    applyTerrainDetail(terrainIndex);
    applyPbrQuality(pbrIndex);

    // Lighting. PBR material maps require the shader-compatible backend.
    static std::array<std::string, 3> lightingMethodMap = {"legacy", "shaders compatibility", "shaders"};
    int lightingMethodIndex = lightingMethodComboBox->currentIndex();
    if (pbrIndex > 0 && lightingMethodIndex == 0)
    {
        lightingMethodIndex = 1;
        lightingMethodComboBox->setCurrentIndex(lightingMethodIndex);
    }
    Settings::Manager::setString("lighting method", "Shaders", lightingMethodMap[lightingMethodIndex]);

    // Shadows
    int cShadowDist = shadowDistanceCheckBox->checkState() != Qt::Unchecked ? shadowDistanceSpinBox->value() : 0;
    if (Settings::Manager::getInt("maximum shadow map distance", "Shadows") != cShadowDist)
        Settings::Manager::setInt("maximum shadow map distance", "Shadows", cShadowDist);
    float cFadeStart = fadeStartSpinBox->value();
    if (cShadowDist > 0 && Settings::Manager::getFloat("shadow fade start", "Shadows") != cFadeStart)
        Settings::Manager::setFloat("shadow fade start", "Shadows", cFadeStart);

    bool cActorShadows = actorShadowsCheckBox->checkState();
    bool cObjectShadows = objectShadowsCheckBox->checkState();
    bool cTerrainShadows = terrainShadowsCheckBox->checkState();
    bool cPlayerShadows = playerShadowsCheckBox->checkState();
    if (cActorShadows || cObjectShadows || cTerrainShadows || cPlayerShadows)
    {
        if (!Settings::Manager::getBool("enable shadows", "Shadows"))
            Settings::Manager::setBool("enable shadows", "Shadows", true);
        if (Settings::Manager::getBool("actor shadows", "Shadows") != cActorShadows)
            Settings::Manager::setBool("actor shadows", "Shadows", cActorShadows);
        if (Settings::Manager::getBool("player shadows", "Shadows") != cPlayerShadows)
            Settings::Manager::setBool("player shadows", "Shadows", cPlayerShadows);
        if (Settings::Manager::getBool("object shadows", "Shadows") != cObjectShadows)
            Settings::Manager::setBool("object shadows", "Shadows", cObjectShadows);
        if (Settings::Manager::getBool("terrain shadows", "Shadows") != cTerrainShadows)
            Settings::Manager::setBool("terrain shadows", "Shadows", cTerrainShadows);
    }
    else
    {
        if (Settings::Manager::getBool("enable shadows", "Shadows"))
            Settings::Manager::setBool("enable shadows", "Shadows", false);
        if (Settings::Manager::getBool("actor shadows", "Shadows"))
            Settings::Manager::setBool("actor shadows", "Shadows", false);
        if (Settings::Manager::getBool("player shadows", "Shadows"))
            Settings::Manager::setBool("player shadows", "Shadows", false);
        if (Settings::Manager::getBool("object shadows", "Shadows"))
            Settings::Manager::setBool("object shadows", "Shadows", false);
        if (Settings::Manager::getBool("terrain shadows", "Shadows"))
            Settings::Manager::setBool("terrain shadows", "Shadows", false);
    }

    bool cIndoorShadows = indoorShadowsCheckBox->checkState();
    if (Settings::Manager::getBool("enable indoor shadows", "Shadows") != cIndoorShadows)
        Settings::Manager::setBool("enable indoor shadows", "Shadows", cIndoorShadows);

    int cShadowRes = shadowResolutionComboBox->currentText().toInt();
    if (cShadowRes != Settings::Manager::getInt("shadow map resolution", "Shadows"))
        Settings::Manager::setInt("shadow map resolution", "Shadows", cShadowRes);

    auto cComputeSceneBounds = shadowComputeSceneBoundsComboBox->currentText().toStdString();
    if (cComputeSceneBounds != Settings::Manager::getString("compute scene bounds", "Shadows"))
        Settings::Manager::setString("compute scene bounds", "Shadows", cComputeSceneBounds);

    storeLauncherValue(QStringLiteral("General/Graphics/autoSelect"),
        autoSelectQualityCheckBox->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    storeLauncherValue(QStringLiteral("General/Graphics/vendorOptimizations"),
        vendorOptimizationsCheckBox->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
}

int Launcher::GraphicsPage::terrainDetailIndexFromSettings() const
{
    static const std::array<float, 6> terrainLod = { 0.40f, 0.50f, 0.65f, 0.80f, 1.00f, 1.25f };
    const float current = Settings::Manager::getFloat("lod factor", "Terrain");
    int best = 0;
    float bestDistance = std::abs(current - terrainLod[0]);
    for (int i = 1; i < static_cast<int>(terrainLod.size()); ++i)
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

int Launcher::GraphicsPage::pbrQualityIndexFromSettings() const
{
    std::string mode = Settings::Manager::getString("material quality", "Shaders");
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return std::tolower(c); });
    if (mode == "none") return 0;
    if (mode == "simple") return 1;
    if (mode == "quality") return 3;
    if (mode == "ultra") return 4;
    return 2;
}

void Launcher::GraphicsPage::applyTerrainDetail(int requestedIndex)
{
    const int index = std::max(0, std::min(5, requestedIndex));
    static const float terrainLod[] = { 0.40f, 0.50f, 0.65f, 0.80f, 1.00f, 1.25f };
    static const int vertexLod[] = { -2, -2, -1, -1, 0, 1 };
    static const int compositeLevel[] = { -3, -3, -2, -2, -1, 0 };
    static const int compositeResolution[] = { 1024, 1024, 1024, 2048, 2048, 4096 };
    static const float maxCompositeGeometrySize[] = { 4.f, 4.f, 6.f, 8.f, 12.f, 16.f };
    static const float objectPagingMergeFactor[] = { 100000.f, 75000.f, 50000.f, 30000.f, 15000.f, 8000.f };
    static const float objectPagingMinSize[] = { 1.f, 0.85f, 0.65f, 0.50f, 0.35f, 0.25f };

    Settings::Manager::setBool("distant terrain", "Terrain", true);
    Settings::Manager::setFloat("lod factor", "Terrain", terrainLod[index]);
    Settings::Manager::setInt("vertex lod mod", "Terrain", vertexLod[index]);
    Settings::Manager::setInt("composite map level", "Terrain", compositeLevel[index]);
    Settings::Manager::setInt("composite map resolution", "Terrain", compositeResolution[index]);
    Settings::Manager::setFloat("max composite geometry size", "Terrain", maxCompositeGeometrySize[index]);
    Settings::Manager::setBool("object paging", "Terrain", true);
    Settings::Manager::setBool("object paging active grid", "Terrain", true);
    Settings::Manager::setFloat("object paging merge factor", "Terrain", objectPagingMergeFactor[index]);
    Settings::Manager::setFloat("object paging min size", "Terrain", objectPagingMinSize[index]);
}

void Launcher::GraphicsPage::applyPbrQuality(int requestedIndex)
{
    const int index = std::max(0, std::min(4, requestedIndex));
    static const std::array<const char*, 5> modes = { "none", "simple", "balanced", "quality", "ultra" };
    Settings::Manager::setString("material quality", "Shaders", modes[index]);

    const bool normalMaps = index >= 1;
    const bool specularMaps = index >= 2;
    Settings::Manager::setBool("auto use object normal maps", "Shaders", normalMaps);
    Settings::Manager::setBool("auto use terrain normal maps", "Shaders", normalMaps);
    Settings::Manager::setBool("auto use object specular maps", "Shaders", specularMaps);
    Settings::Manager::setBool("auto use terrain specular maps", "Shaders", specularMaps);
    if (index > 0)
    {
        Settings::Manager::setBool("force shaders", "Shaders", true);
        Settings::Manager::setBool("force per pixel lighting", "Shaders", index >= 2);
        Settings::Manager::setString("lighting method", "Shaders", "shaders compatibility");
    }
}

QString Launcher::GraphicsPage::HardwareInfo::signature() const
{
    return vendor + QLatin1Char('|') + renderer + QLatin1Char('|')
        + QString::number(dedicatedVramMb) + QLatin1Char('|')
        + QString::number(logicalCores) + QLatin1Char('|')
        + QString::number(displayMegapixelsTimes100);
}

bool Launcher::GraphicsPage::reloadUserSettingsFromDisk()
{
    const std::string path = Settings::Manager::mUserSettingsPath;
    Settings::Manager::mUserSettings.clear();
    Settings::Manager::mChangedSettings.clear();

    if (path.empty())
        return true;

    const QFileInfo fileInfo(QString::fromUtf8(path.c_str()));
    if (!fileInfo.exists())
        return true;

    try
    {
        Settings::SettingsFileParser parser;
        parser.loadSettingsFile(path, Settings::Manager::mUserSettings);
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, tr("Error reading settings.cfg"),
            tr("Could not reload the current game settings before applying the preset:\n\n%1")
                .arg(QString::fromUtf8(e.what())));
        return false;
    }

    return true;
}

bool Launcher::GraphicsPage::saveUserSettingsToDisk()
{
    if (Settings::Manager::mUserSettingsPath.empty())
        return false;

    try
    {
        Settings::Manager::saveUser();
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, tr("Error writing settings.cfg"),
            tr("Could not save the selected graphics preset:\n\n%1")
                .arg(QString::fromUtf8(e.what())));
        return false;
    }

    return true;
}

void Launcher::GraphicsPage::storeLauncherValue(const QString& key, const QString& value)
{
    mLauncherSettings.remove(key);
    mLauncherSettings.setValue(key, value);
}

QString Launcher::GraphicsPage::qualityName(int level) const
{
    static const std::array<const char*, 6> names = {
        "Minimum", "Low", "Balanced", "Medium", "High", "Ultra"
    };
    return tr(names[std::max(0, std::min(5, level))]);
}

QString Launcher::GraphicsPage::qualityDescription(int level) const
{
    static const std::array<const char*, 6> descriptions = {
        "Maximum performance for software renderers and very old integrated graphics. Distant land stays enabled with conservative terrain LOD, shadows and grass.",
        "For older integrated GPUs. Uses simple lighting, modest view distance and lightweight grass without expensive object shadows.",
        "A stable default for entry-level hardware. Enables distant terrain, normal maps, actor shadows and conservative paging.",
        "Balanced visual quality for modern integrated graphics and mainstream discrete GPUs. Enables object shadows, improved water and denser grass.",
        "For powerful discrete GPUs. Uses longer view distance, detailed terrain, terrain shadows, higher anisotropy and larger shadow maps.",
        "Maximum detail for high-end GPUs. Uses the longest draw distances, dense groundcover, high light counts and very large shadow maps."
    };
    return tr(descriptions[std::max(0, std::min(5, level))]);
}

Launcher::GraphicsPage::HardwareInfo Launcher::GraphicsPage::detectHardware() const
{
    HardwareInfo result;
    result.vendor = QStringLiteral("unknown");
    result.renderer = tr("Unknown graphics adapter");
    result.logicalCores = std::max(1, QThread::idealThreadCount());

    if (QScreen* screen = QGuiApplication::primaryScreen())
    {
        const QSize logicalSize = screen->size();
        const qreal ratio = screen->devicePixelRatio();
        const qint64 pixels = static_cast<qint64>(logicalSize.width() * ratio)
            * static_cast<qint64>(logicalSize.height() * ratio);
        result.displayMegapixelsTimes100 = static_cast<int>(pixels / 10000);
    }

#ifdef _WIN32
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
    {
        quint64 bestVram = 0;
        bool foundHardware = false;
        IDXGIAdapter1* adapter = nullptr;
        for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index)
        {
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(adapter->GetDesc1(&desc)))
            {
                const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
                const quint64 vram = static_cast<quint64>(desc.DedicatedVideoMemory / (1024 * 1024));
                if ((!software && !foundHardware) || (!software && vram >= bestVram)
                    || (software && !foundHardware && result.renderer.startsWith(tr("Unknown"))))
                {
                    foundHardware = !software;
                    bestVram = vram;
                    result.dedicatedVramMb = vram;
                    result.renderer = QString::fromWCharArray(desc.Description).trimmed();
                    result.softwareRenderer = software;
                    switch (desc.VendorId)
                    {
                        case 0x10DE: result.vendor = QStringLiteral("nvidia"); break;
                        case 0x1002:
                        case 0x1022: result.vendor = QStringLiteral("amd"); break;
                        case 0x8086: result.vendor = QStringLiteral("intel"); break;
                        case 0x1414: result.vendor = QStringLiteral("microsoft"); break;
                        default: result.vendor = QStringLiteral("unknown"); break;
                    }
                }
            }
            adapter->Release();
            adapter = nullptr;
        }
        factory->Release();
    }
#endif

    // OpenGL is also queried because it identifies the adapter actually selected by the driver
    // on Linux, macOS and hybrid-GPU systems where platform enumeration is ambiguous.
    QOffscreenSurface surface;
    surface.create();
    QOpenGLContext context;
    context.setFormat(surface.requestedFormat());
    if (context.create() && context.makeCurrent(&surface))
    {
        QOpenGLFunctions* functions = context.functions();
        functions->initializeOpenGLFunctions();
        const char* glVendor = reinterpret_cast<const char*>(functions->glGetString(GL_VENDOR));
        const char* glRenderer = reinterpret_cast<const char*>(functions->glGetString(GL_RENDERER));
        const QString vendorText = QString::fromLatin1(glVendor ? glVendor : "");
        const QString rendererText = QString::fromLatin1(glRenderer ? glRenderer : "");
        const QString combined = (vendorText + QLatin1Char(' ') + rendererText).toLower();

        if (result.renderer.startsWith(tr("Unknown")) && !rendererText.isEmpty())
            result.renderer = rendererText.trimmed();
        if (result.vendor == QLatin1String("unknown"))
        {
            if (combined.contains(QLatin1String("nvidia")) || combined.contains(QLatin1String("geforce")))
                result.vendor = QStringLiteral("nvidia");
            else if (combined.contains(QLatin1String("amd")) || combined.contains(QLatin1String("ati"))
                || combined.contains(QLatin1String("radeon")))
                result.vendor = QStringLiteral("amd");
            else if (combined.contains(QLatin1String("intel")))
                result.vendor = QStringLiteral("intel");
            else if (combined.contains(QLatin1String("apple")))
                result.vendor = QStringLiteral("apple");
            else if (combined.contains(QLatin1String("llvmpipe")) || combined.contains(QLatin1String("softpipe"))
                || combined.contains(QLatin1String("software")) || combined.contains(QLatin1String("gdi generic")))
                result.vendor = QStringLiteral("microsoft");
        }
        result.softwareRenderer = result.softwareRenderer
            || combined.contains(QLatin1String("llvmpipe"))
            || combined.contains(QLatin1String("softpipe"))
            || combined.contains(QLatin1String("software"))
            || combined.contains(QLatin1String("gdi generic"));
        context.doneCurrent();
    }

#ifdef Q_OS_LINUX
    // Mesa exposes dedicated VRAM through sysfs for AMD and many discrete Intel GPUs.
    QDir drmDir(QStringLiteral("/sys/class/drm"));
    const QFileInfoList cards = drmDir.entryInfoList(QStringList() << QStringLiteral("card[0-9]*"),
        QDir::Dirs | QDir::NoDotAndDotDot);
    quint64 bestLinuxVram = result.dedicatedVramMb;
    for (const QFileInfo& card : cards)
    {
        QFile memoryFile(card.absoluteFilePath() + QStringLiteral("/device/mem_info_vram_total"));
        if (!memoryFile.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        bool ok = false;
        const quint64 bytes = memoryFile.readAll().trimmed().toULongLong(&ok);
        if (ok && bytes / (1024 * 1024) > bestLinuxVram)
            bestLinuxVram = bytes / (1024 * 1024);
    }
    result.dedicatedVramMb = bestLinuxVram;
#endif

    const QString rendererLower = result.renderer.toLower();
    const bool intelArc = rendererLower.contains(QLatin1String(" arc "))
        || rendererLower.startsWith(QLatin1String("arc "));
    result.integrated = (result.vendor == QLatin1String("intel") && !intelArc)
        || rendererLower.contains(QLatin1String("iris"))
        || rendererLower.contains(QLatin1String("uhd"))
        || rendererLower.contains(QLatin1String("integrated"));
    return result;
}

int Launcher::GraphicsPage::recommendQuality(const HardwareInfo& info) const
{
    if (info.softwareRenderer || info.vendor == QLatin1String("microsoft")
        || info.vendor == QLatin1String("unknown"))
        return 0;

    const QString model = info.renderer.toLower();
    int level = 2;

    if (info.vendor == QLatin1String("intel"))
    {
        if (model.contains(QLatin1String("arc")) || info.dedicatedVramMb >= 4096)
            level = info.logicalCores >= 8 ? 4 : 3;
        else if (model.contains(QLatin1String("iris xe")) || info.dedicatedVramMb >= 1536)
            level = 2;
        else
            level = 1;
    }
    else if (info.vendor == QLatin1String("apple"))
    {
        if (model.contains(QLatin1String("m4")) || model.contains(QLatin1String("m3 max"))
            || model.contains(QLatin1String("m2 max")))
            level = 5;
        else if (model.contains(QLatin1String("m3")) || model.contains(QLatin1String("m2 pro"))
            || model.contains(QLatin1String("m1 max")))
            level = 4;
        else
            level = 3;
    }
    else if (info.dedicatedVramMb > 0)
    {
        if (info.dedicatedVramMb >= 12288 && info.logicalCores >= 8) level = 5;
        else if (info.dedicatedVramMb >= 8192 && info.logicalCores >= 6) level = 4;
        else if (info.dedicatedVramMb >= 6144) level = 4;
        else if (info.dedicatedVramMb >= 4096) level = 3;
        else if (info.dedicatedVramMb >= 2048) level = 2;
        else level = 1;
    }
    else if (info.vendor == QLatin1String("nvidia"))
    {
        if (model.contains(QLatin1String("rtx 50")) || model.contains(QLatin1String("rtx 4090"))
            || model.contains(QLatin1String("rtx 4080"))) level = 5;
        else if (model.contains(QLatin1String("rtx 40")) || model.contains(QLatin1String("rtx 30"))) level = 4;
        else if (model.contains(QLatin1String("rtx")) || model.contains(QLatin1String("gtx 16"))) level = 3;
        else level = info.logicalCores >= 6 ? 2 : 1;
    }
    else if (info.vendor == QLatin1String("amd"))
    {
        if (model.contains(QLatin1String("rx 79")) || model.contains(QLatin1String("rx 78"))
            || model.contains(QLatin1String("rx 69"))) level = 5;
        else if (model.contains(QLatin1String("rx 7")) || model.contains(QLatin1String("rx 6"))) level = 4;
        else if (model.contains(QLatin1String("rx 5"))) level = 3;
        else level = info.logicalCores >= 6 ? 2 : 1;
    }

    if (info.logicalCores <= 2)
        level = std::min(level, 1);
    else if (info.logicalCores <= 4)
        level = std::min(level, 3);

    // Rendering at 4K costs substantially more even when the adapter model is unknown.
    if (info.displayMegapixelsTimes100 >= 800 && info.dedicatedVramMb < 8192
        && info.vendor != QLatin1String("apple"))
        level = std::max(0, level - 1);

    return std::max(0, std::min(5, level));
}

void Launcher::GraphicsPage::updateHardwareLabels()
{
    QString gpuText = QStringLiteral("%1 - %2")
        .arg(mHardwareInfo.vendor.toUpper(), mHardwareInfo.renderer);
    if (mHardwareInfo.dedicatedVramMb > 0)
        gpuText += tr(" (%1 MB dedicated VRAM)").arg(mHardwareInfo.dedicatedVramMb);
    if (mHardwareInfo.integrated)
        gpuText += tr(" - integrated");
    if (mHardwareInfo.softwareRenderer)
        gpuText += tr(" - software renderer");
    qualityGpuLabel->setText(gpuText);

    const double megapixels = mHardwareInfo.displayMegapixelsTimes100 / 100.0;
    qualityCpuLabel->setText(tr("%1 logical threads, %2 MP primary display")
        .arg(mHardwareInfo.logicalCores).arg(megapixels, 0, 'f', 2));
    qualityRecommendationLabel->setText(qualityName(mRecommendedQuality));
}

void Launcher::GraphicsPage::initializeQualityPage()
{
    mInitializingQuality = true;
    mHardwareInfo = detectHardware();
    mRecommendedQuality = recommendQuality(mHardwareInfo);

    const bool autoSelect = mLauncherSettings.value(
        QStringLiteral("General/Graphics/autoSelect"), QStringLiteral("true"))
        .compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    const bool vendorOptimizations = mLauncherSettings.value(
        QStringLiteral("General/Graphics/vendorOptimizations"), QStringLiteral("true"))
        .compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    const QString mode = mLauncherSettings.value(
        QStringLiteral("General/Graphics/qualityMode"), QStringLiteral("auto"));

    autoSelectQualityCheckBox->setChecked(autoSelect);
    vendorOptimizationsCheckBox->setChecked(vendorOptimizations);
    if (mode == QLatin1String("auto"))
        qualityPresetComboBox->setCurrentIndex(0);
    else
    {
        bool ok = false;
        const int level = mode.toInt(&ok);
        qualityPresetComboBox->setCurrentIndex(ok ? std::max(1, std::min(6, level + 1)) : 0);
    }

    updateHardwareLabels();
    updateQualityDescription();

    // A fresh Wizard run requests exactly one initial quality application.
    // After that, opening the Launcher, changing hardware, pressing Play or
    // closing the window must not touch settings.cfg. Further preset changes
    // happen only through the explicit Apply preset button.
    const QString pendingKey = QStringLiteral("General/Graphics/initialQualityPresetPending");
    const bool initialPresetPending = mLauncherSettings.value(
        pendingKey, QStringLiteral("false"))
        .compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;

    if (initialPresetPending && reloadUserSettingsFromDisk())
    {
        applyQualityLevel(mRecommendedQuality);
        if (vendorOptimizations)
            applyVendorOptimizations(mRecommendedQuality);

        if (saveUserSettingsToDisk())
        {
            storeLauncherValue(QStringLiteral("General/Graphics/qualityMode"), QStringLiteral("auto"));
            storeLauncherValue(QStringLiteral("General/Graphics/hardwareSignature"), mHardwareInfo.signature());
            storeLauncherValue(pendingKey, QStringLiteral("false"));
            storeLauncherValue(QStringLiteral("General/Graphics/initialQualityPresetApplied"),
                QStringLiteral("true"));
        }
    }

    mInitializingQuality = false;
}

void Launcher::GraphicsPage::updateQualityDescription()
{
    const int index = qualityPresetComboBox->currentIndex();
    const int level = index == 0 ? mRecommendedQuality : index - 1;
    QString text;
    if (index == 0)
        text = tr("Automatic recommendation: %1. ").arg(qualityName(level));
    text += qualityDescription(level);
    text += tr(" Resolution, screen, fullscreen mode, GUI scale and field of view are not changed.");
    qualityDescriptionLabel->setText(text);
}

void Launcher::GraphicsPage::slotQualityPresetChanged(int)
{
    if (!mInitializingQuality)
        updateQualityDescription();
}

void Launcher::GraphicsPage::slotTerrainDetailChanged(int index)
{
    if (mInitializingQuality)
        return;

    if (index < 4 && pbrQualityComboBox->currentIndex() == 4)
        pbrQualityComboBox->setCurrentIndex(3);
}

void Launcher::GraphicsPage::slotPbrQualityChanged(int index)
{
    if (mInitializingQuality)
        return;

    if (index == 4 && terrainDetailComboBox->currentIndex() < 4)
        terrainDetailComboBox->setCurrentIndex(4);
    if (index > 0 && lightingMethodComboBox->currentIndex() == 0)
        lightingMethodComboBox->setCurrentIndex(1);
}

void Launcher::GraphicsPage::slotDetectHardware()
{
    mHardwareInfo = detectHardware();
    mRecommendedQuality = recommendQuality(mHardwareInfo);
    updateHardwareLabels();
    updateQualityDescription();
}

void Launcher::GraphicsPage::slotApplyQualityPreset()
{
    // The game may have changed settings.cfg while the Launcher remained open.
    // Reload it first and change only the keys owned by the selected preset, so
    // unrelated in-game options such as Quick Loot are preserved.
    if (!reloadUserSettingsFromDisk())
        return;

    const int index = qualityPresetComboBox->currentIndex();
    const int level = index == 0 ? mRecommendedQuality : index - 1;
    applyQualityLevel(level);

    if (vendorOptimizationsCheckBox->isChecked())
        applyVendorOptimizations(level);

    if (!saveUserSettingsToDisk())
        return;

    storeLauncherValue(QStringLiteral("General/Graphics/qualityMode"),
        index == 0 ? QStringLiteral("auto") : QString::number(level));
    storeLauncherValue(QStringLiteral("General/Graphics/hardwareSignature"), mHardwareInfo.signature());
    storeLauncherValue(QStringLiteral("General/Graphics/autoSelect"),
        autoSelectQualityCheckBox->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    storeLauncherValue(QStringLiteral("General/Graphics/vendorOptimizations"),
        vendorOptimizationsCheckBox->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    storeLauncherValue(QStringLiteral("General/Graphics/initialQualityPresetPending"),
        QStringLiteral("false"));

    syncGraphicsControls();
    qualityRecommendationLabel->setText(qualityName(mRecommendedQuality));
}

void Launcher::GraphicsPage::applyQualityLevel(int requestedLevel)
{
    const int level = std::max(0, std::min(5, requestedLevel));

    // Display settings are deliberately not touched here. Reading a value from
    // the default preset and then writing it back through Settings::Manager
    // turns that default into an explicit user override. That was the reason
    // resolution, fullscreen mode and GUI scaling were reset after Play.

    static const int viewDistance[] = { 5000, 6000, 8192, 10000, 12288, 16384 };
    // Conservative cutoffs reduce visible popping of thin geometry and distant details.
    // Lower values cull fewer small projected features.
    static const int cullingPixels[] = { 12, 11, 10, 8, 7, 6 };
    static const int cellCache[] = { 24, 32, 48, 64, 96, 128 };
    static const int targetFps[] = { 20, 25, 30, 45, 60, 75 };
    // Distant land is always enabled. Minimum is the safe baseline requested
    // for every build; higher presets increase terrain and texture detail.
    static const float terrainLod[] = { 0.40f, 0.50f, 0.65f, 0.80f, 1.00f, 1.25f };
    static const int vertexLod[] = { -2, -2, -1, -1, 0, 1 };
    static const int compositeLevel[] = { -3, -3, -2, -2, -1, 0 };
    static const int compositeResolution[] = { 1024, 1024, 1024, 2048, 2048, 4096 };
    static const float maxCompositeGeometrySize[] = { 4.f, 4.f, 6.f, 8.f, 12.f, 16.f };
    static const float objectPagingMergeFactor[] = { 100000.f, 75000.f, 50000.f, 30000.f, 15000.f, 8000.f };
    static const float objectPagingMinSize[] = { 1.f, 0.85f, 0.65f, 0.50f, 0.35f, 0.25f };
    static const int maxLights[] = { 8, 12, 16, 24, 32, 48 };
    static const int anisotropy[] = { 0, 2, 4, 8, 12, 16 };
    static const int antialiasing[] = { 0, 0, 2, 2, 4, 4 };
    static const int waterRtt[] = { 256, 256, 256, 512, 512, 512 };
    // Minimum already reflects world objects. Higher presets progressively add
    // actors and groundcover; the limited 0..5 setting range means adjacent
    // presets intentionally share a detail tier.
    static const int waterReflectionDetail[] = { 3, 3, 4, 4, 5, 5 };
    static const int shadowDistance[] = { 0, 2048, 4096, 6144, 8192, 12288 };
    // Shadow-map sizes below 512 are intentionally not used by any quality preset.
    static const int shadowResolution[] = { 512, 512, 1024, 2048, 4096, 8192 };
    static const float grassDensity[] = { 0.30f, 0.45f, 0.65f, 0.80f, 0.90f, 1.00f };
    static const float grassDistance[] = { 4096.f, 6144.f, 8192.f, 10240.f, 12288.f, 16384.f };
    static const int physicsThreads[] = { 0, 0, 1, 1, 2, 2 };
    // Occlusion presets are intentionally conservative. A slightly denser depth buffer
    // avoids coarse false-positive occlusion on the minimum and low profiles, while the
    // remaining limits control CPU cost by reducing the number and reach of occluders.
    static const int occlusionWidth[] = { 384, 384, 512, 512, 640, 768 };
    static const int occlusionHeight[] = { 192, 192, 256, 256, 320, 384 };
    static const int occlusionTerrainRadius[] = { 4, 4, 6, 8, 10, 12 };
    static const float occlusionMinRadius[] = { 650.f, 600.f, 550.f, 500.f, 450.f, 400.f };
    static const float occlusionMaxRadius[] = { 3200.f, 3600.f, 4000.f, 4400.f, 4800.f, 5000.f };
    static const float occlusionShrinkFactor[] = { 0.70f, 0.72f, 0.74f, 0.76f, 0.78f, 0.80f };
    static const int occlusionMeshResolution[] = { 8, 8, 10, 10, 12, 12 };
    static const int occlusionMaxMeshResolution[] = { 24, 24, 28, 28, 32, 36 };
    static const float occlusionInsideThreshold[] = { 0.96f, 0.95f, 0.94f, 0.93f, 0.92f, 0.91f };
    static const float occlusionMaxDistance[] = { 3072.f, 3584.f, 4096.f, 5120.f, 6144.f, 7168.f };

    int effectiveAa = antialiasing[level];
    int effectiveShadowResolution = shadowResolution[level];
    int effectiveShadowDistance = shadowDistance[level];
    int effectiveMaxLights = maxLights[level];
    static const int materialQuality[] = { 0, 1, 1, 2, 3, 4 };
    int effectiveMaterialQuality = materialQuality[level];

    const QString rendererLower = mHardwareInfo.renderer.toLower();
    const bool intelArc = rendererLower.contains(QLatin1String("arc"));
    if (mHardwareInfo.vendor == QLatin1String("intel") && !intelArc)
    {
        effectiveAa = std::min(effectiveAa, 2);
        effectiveShadowResolution = std::min(effectiveShadowResolution, 1024);
        effectiveShadowDistance = std::min(effectiveShadowDistance, 4096);
        effectiveMaxLights = std::min(effectiveMaxLights, 16);
        effectiveMaterialQuality = std::min(effectiveMaterialQuality, 1);
    }
    else if (mHardwareInfo.vendor == QLatin1String("apple"))
    {
        effectiveShadowResolution = std::min(effectiveShadowResolution, 2048);
        effectiveMaxLights = std::min(effectiveMaxLights, 32);
    }

    Settings::Manager::setInt("viewing distance", "Camera", viewDistance[level]);
    Settings::Manager::setInt("small feature culling pixel size", "Camera", cullingPixels[level]);
    Settings::Manager::setBool("occlusion culling", "Camera", true);
    Settings::Manager::setInt("occlusion buffer width", "Camera", occlusionWidth[level]);
    Settings::Manager::setInt("occlusion buffer height", "Camera", occlusionHeight[level]);
    Settings::Manager::setInt("occlusion terrain radius", "Camera", occlusionTerrainRadius[level]);
    Settings::Manager::setFloat("occlusion occluder min radius", "Camera", occlusionMinRadius[level]);
    Settings::Manager::setFloat("occlusion occluder max radius", "Camera", occlusionMaxRadius[level]);
    Settings::Manager::setFloat("occlusion occluder shrink factor", "Camera", occlusionShrinkFactor[level]);
    Settings::Manager::setInt("occlusion occluder mesh resolution", "Camera", occlusionMeshResolution[level]);
    Settings::Manager::setInt("occlusion occluder max mesh resolution", "Camera", occlusionMaxMeshResolution[level]);
    Settings::Manager::setFloat("occlusion occluder inside threshold", "Camera", occlusionInsideThreshold[level]);
    Settings::Manager::setFloat("occlusion occluder max distance", "Camera",
        std::min(static_cast<float>(viewDistance[level]), occlusionMaxDistance[level]));

    // Preloading must remain enabled for every graphics quality preset.
    // Quality levels may tune thread and cache limits, but must not disable
    // cell, exterior-grid or instance preloading.
    Settings::Manager::setBool("preload enabled", "Cells", true);
    Settings::Manager::setInt("preload num threads", "Cells", level >= 4 ? 2 : 1);
    Settings::Manager::setBool("preload exterior grid", "Cells", true);
    Settings::Manager::setBool("preload instances", "Cells", true);
    Settings::Manager::setInt("preload cell cache max", "Cells", cellCache[level]);
    Settings::Manager::setInt("target framerate", "Cells", targetFps[level]);

    // Distant land is mandatory for ArenaMP. Graphics presets only tune its
    // quality parameters and never provide a disabled variant.
    Settings::Manager::setBool("distant terrain", "Terrain", true);
    Settings::Manager::setFloat("lod factor", "Terrain", terrainLod[level]);
    Settings::Manager::setInt("vertex lod mod", "Terrain", vertexLod[level]);
    Settings::Manager::setInt("composite map level", "Terrain", compositeLevel[level]);
    Settings::Manager::setInt("composite map resolution", "Terrain", compositeResolution[level]);
    Settings::Manager::setFloat("max composite geometry size", "Terrain", maxCompositeGeometrySize[level]);
    Settings::Manager::setBool("object paging", "Terrain", true);
    Settings::Manager::setBool("object paging active grid", "Terrain", true);
    Settings::Manager::setFloat("object paging merge factor", "Terrain", objectPagingMergeFactor[level]);
    Settings::Manager::setFloat("object paging min size", "Terrain", objectPagingMinSize[level]);

    Settings::Manager::setInt("anisotropy", "General", anisotropy[level]);
    Settings::Manager::setInt("antialiasing", "Video", effectiveAa);

    static const std::array<const char*, 5> materialModes = { "none", "simple", "balanced", "quality", "ultra" };
    const bool normalMaps = effectiveMaterialQuality >= 1;
    const bool specularMaps = effectiveMaterialQuality >= 2;
    Settings::Manager::setString("material quality", "Shaders", materialModes[effectiveMaterialQuality]);
    Settings::Manager::setBool("force shaders", "Shaders", true);
    Settings::Manager::setBool("force per pixel lighting", "Shaders", level >= 2);
    Settings::Manager::setBool("clamp lighting", "Shaders", level <= 1);
    Settings::Manager::setBool("auto use object normal maps", "Shaders", normalMaps);
    Settings::Manager::setBool("auto use terrain normal maps", "Shaders", normalMaps);
    Settings::Manager::setBool("auto use object specular maps", "Shaders", specularMaps);
    Settings::Manager::setBool("auto use terrain specular maps", "Shaders", specularMaps);
    Settings::Manager::setInt("max lights", "Shaders", effectiveMaxLights);
    // All ArenaMP presets use the shader-compatible lighting backend. Legacy
    // lighting is never selected by a preset, including the minimum profile.
    Settings::Manager::setString("lighting method", "Shaders", "shaders compatibility");

    // Every preset uses shader water so its reflection/refraction tier is
    // applied even when an older user profile explicitly disabled the shader.
    Settings::Manager::setBool("shader", "Water", true);
    Settings::Manager::setInt("rtt size", "Water", waterRtt[level]);
    // Low and every higher preset use refraction. Minimum keeps it disabled,
    // while still reflecting objects as requested.
    Settings::Manager::setBool("refraction", "Water", level >= 1);
    Settings::Manager::setInt("reflection detail", "Water", waterReflectionDetail[level]);

    Settings::Manager::setBool("enable shadows", "Shadows", level >= 1);
    Settings::Manager::setBool("player shadows", "Shadows", level >= 1);
    Settings::Manager::setBool("actor shadows", "Shadows", level >= 1);
    Settings::Manager::setBool("object shadows", "Shadows", level >= 2);
    Settings::Manager::setBool("terrain shadows", "Shadows", level >= 3);
    Settings::Manager::setBool("enable indoor shadows", "Shadows", level >= 2);
    Settings::Manager::setInt("maximum shadow map distance", "Shadows", effectiveShadowDistance);
    Settings::Manager::setInt("shadow map resolution", "Shadows", effectiveShadowResolution);
    Settings::Manager::setString("compute scene bounds", "Shadows", level >= 5 ? "primitives" : "bounds");

    Settings::Manager::setBool("enabled", "Groundcover", level >= 1);
    Settings::Manager::setFloat("density", "Groundcover", grassDensity[level]);
    Settings::Manager::setFloat("rendering distance", "Groundcover", grassDistance[level]);
    Settings::Manager::setInt("async num threads", "Physics", physicsThreads[level]);

    const char* threadingModel = level <= 1 ? "SingleThreaded"
        : (level >= 5 && mHardwareInfo.vendor != QLatin1String("apple")
            ? "CullDrawThreadPerContext" : "DrawThreadPerContext");
    Settings::Manager::setString("threading model", "OSG", threadingModel);

    // No display-setting restore is required: the preset never modifies those
    // keys. In particular, do not call setInt/setBool/setFloat for them here,
    // because doing so materializes values inherited from settings-default.cfg
    // or defaults.bin inside the user's settings.cfg.
}

void Launcher::GraphicsPage::applyVendorOptimizations(int level)
{
    static const char* databaseThreads[] = { "1", "2", "3", "4", "5", "6" };
    static const char* compileThreads[] = { "0", "0", "1", "1", "2", "3" };
    static const char* maxPagedLod[] = { "2", "4", "6", "8", "12", "16" };
    const int safeLevel = std::max(0, std::min(5, level));

    qputenv("OSG_DATABASE_PAGER_THREADS", databaseThreads[safeLevel]);
    qputenv("OSG_NUM_DATABASE_THREADS", databaseThreads[safeLevel]);
    qputenv("OSG_NUM_COMPILE_THREADS", compileThreads[safeLevel]);
    qputenv("OSG_MAX_PAGEDLOD", maxPagedLod[safeLevel]);

    if (mHardwareInfo.vendor == QLatin1String("nvidia"))
    {
        qputenv("__GL_THREADED_OPTIMIZATIONS", "1");
        qputenv("__GL_SHADER_DISK_CACHE", "1");
    }
#ifdef Q_OS_LINUX
    else if (mHardwareInfo.vendor == QLatin1String("amd")
        || mHardwareInfo.vendor == QLatin1String("intel"))
    {
        qputenv("mesa_glthread", "true");
        qputenv("MESA_SHADER_CACHE_DISABLE", "false");
    }
#endif

#ifdef _WIN32
    // Windows 10/11 hybrid systems honour this per-application preference for
    // both DirectX and vendor OpenGL ICD selection. It is safe on desktop PCs.
    if (!mHardwareInfo.integrated && !mHardwareInfo.softwareRenderer)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, nullptr, 0,
                KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const QString appDir = QCoreApplication::applicationDirPath();
            const QStringList executables = { QStringLiteral("tes3mp.exe"), QStringLiteral("openmw.exe") };
            const wchar_t data[] = L"GpuPreference=2;";
            for (const QString& executable : executables)
            {
                const QString path = QDir::toNativeSeparators(appDir + QLatin1Char('/') + executable);
                if (!QFileInfo::exists(path))
                    continue;
                const std::wstring valueName = path.toStdWString();
                RegSetValueExW(key, valueName.c_str(), 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(data), sizeof(data));
            }
            RegCloseKey(key);
        }
    }
#endif
}

QStringList Launcher::GraphicsPage::getAvailableResolutions(int screen)
{
    QStringList result;
    SDL_DisplayMode mode;
    int modeIndex, modes = SDL_GetNumDisplayModes(screen);

    if (modes < 0)
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error receiving resolutions"));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(tr("<br><b>SDL_GetNumDisplayModes failed:</b><br><br>") + QString::fromUtf8(SDL_GetError()) + "<br>");
        msgBox.exec();
        return result;
    }

    for (modeIndex = 0; modeIndex < modes; modeIndex++)
    {
        if (SDL_GetDisplayMode(screen, modeIndex, &mode) < 0)
        {
            QMessageBox msgBox;
            msgBox.setWindowTitle(tr("Error receiving resolutions"));
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setText(tr("<br><b>SDL_GetDisplayMode failed:</b><br><br>") + QString::fromUtf8(SDL_GetError()) + "<br>");
            msgBox.exec();
            return result;
        }

        QString resolution = QString::number(mode.w) + QString(" x ") + QString::number(mode.h);

        QString aspect = getAspect(mode.w, mode.h);
        if (aspect == QLatin1String("16:9") || aspect == QLatin1String("16:10")) {
            resolution.append(tr("\t(Wide ") + aspect + ")");

        } else if (aspect == QLatin1String("4:3")) {
            resolution.append(tr("\t(Standard 4:3)"));
        }

        result.append(resolution);
    }

    result.removeDuplicates();
    return result;
}

QRect Launcher::GraphicsPage::getMaximumResolution()
{
    QRect max;

    for (QScreen* screen : QGuiApplication::screens())
    {
        QRect res = screen->geometry();
        if (res.width() > max.width())
            max.setWidth(res.width());
        if (res.height() > max.height())
            max.setHeight(res.height());
    }
    return max;
}

void Launcher::GraphicsPage::screenChanged(int screen)
{
    if (screen >= 0) {
        resolutionComboBox->clear();
        resolutionComboBox->addItems(mResolutionsPerScreen[screen]);
    }
}

void Launcher::GraphicsPage::slotFullScreenChanged(int state)
{
    if (state == Qt::Checked) {
        standardRadioButton->toggle();
        customRadioButton->setEnabled(false);
        customWidthSpinBox->setEnabled(false);
        customHeightSpinBox->setEnabled(false);
        windowBorderCheckBox->setEnabled(false);
    } else {
        customRadioButton->setEnabled(true);
        customWidthSpinBox->setEnabled(true);
        customHeightSpinBox->setEnabled(true);
        windowBorderCheckBox->setEnabled(true);
    }
}

void Launcher::GraphicsPage::slotStandardToggled(bool checked)
{
    if (checked) {
        resolutionComboBox->setEnabled(true);
        customWidthSpinBox->setEnabled(false);
        customHeightSpinBox->setEnabled(false);
    } else {
        resolutionComboBox->setEnabled(false);
        customWidthSpinBox->setEnabled(true);
        customHeightSpinBox->setEnabled(true);
    }
}

void Launcher::GraphicsPage::slotFramerateLimitToggled(bool checked)
{
    framerateLimitSpinBox->setEnabled(checked);
}

void Launcher::GraphicsPage::slotShadowDistLimitToggled(bool checked)
{
    shadowDistanceSpinBox->setEnabled(checked);
    fadeStartSpinBox->setEnabled(checked);
}
