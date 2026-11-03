#ifndef GRAPHICSPAGE_H
#define GRAPHICSPAGE_H

#include "ui_graphicspage.h"

#include <components/settings/settings.hpp>

#include "sdlinit.hpp"

namespace Config { class LauncherSettings; }
namespace Files { struct ConfigurationManager; }

namespace Launcher
{
    class GraphicsSettings;

    class GraphicsPage : public QWidget, private Ui::GraphicsPage
    {
        Q_OBJECT

    public:
        explicit GraphicsPage(Config::LauncherSettings& launcherSettings, QWidget *parent = nullptr);

        void saveSettings();
        bool loadSettings();

    public slots:
        void screenChanged(int screen);

    private slots:
        void slotFullScreenChanged(int state);
        void slotStandardToggled(bool checked);
        void slotFramerateLimitToggled(bool checked);
        void slotShadowDistLimitToggled(bool checked);
        void slotQualityPresetChanged(int index);
        void slotTerrainDetailChanged(int index);
        void slotPbrQualityChanged(int index);
        void slotApplyQualityPreset();
        void slotDetectHardware();

    private:
        struct HardwareInfo
        {
            QString vendor;
            QString renderer;
            quint64 dedicatedVramMb = 0;
            int logicalCores = 1;
            int displayMegapixelsTimes100 = 0;
            bool softwareRenderer = false;
            bool integrated = false;

            QString signature() const;
        };

        QVector<QStringList> mResolutionsPerScreen;
        Config::LauncherSettings& mLauncherSettings;
        HardwareInfo mHardwareInfo;
        int mRecommendedQuality;
        bool mInitializingQuality;

        static QStringList getAvailableResolutions(int screen);
        static QRect getMaximumResolution();

        bool setupSDL();
        void syncGraphicsControls();
        void initializeQualityPage();
        HardwareInfo detectHardware() const;
        int recommendQuality(const HardwareInfo& info) const;
        void applyQualityLevel(int level);
        void applyTerrainDetail(int index);
        void applyPbrQuality(int index);
        int terrainDetailIndexFromSettings() const;
        int pbrQualityIndexFromSettings() const;
        void applyVendorOptimizations(int level);
        void updateQualityDescription();
        void updateHardwareLabels();
        bool reloadUserSettingsFromDisk();
        bool saveUserSettingsToDisk();
        void storeLauncherValue(const QString& key, const QString& value);
        QString qualityName(int level) const;
        QString qualityDescription(int level) const;
    };
}
#endif
