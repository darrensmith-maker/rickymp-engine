#ifndef MAINWIZARD_HPP
#define MAINWIZARD_HPP

#include <QWizard>

#include <components/process/processinvoker.hpp>

#ifndef Q_MOC_RUN
#include <components/files/configurationmanager.hpp>

#include <components/config/gamesettings.hpp>
#include <components/config/launchersettings.hpp>
#endif

namespace Wizard
{
    class MainWizard : public QWizard
    {
        Q_OBJECT

    public:
        struct Installation {
            bool hasMorrowind;
            bool hasTribunal;
            bool hasBloodmoon;

            QString iniPath;
        };

        enum {
            Page_Intro,
            Page_MethodSelection,
            Page_LanguageSelection,
            Page_ExistingInstallation,
            Page_InstallationTarget,
            Page_ComponentSelection,
            Page_Installation,
            Page_Import,
            Page_Conclusion
        };

        MainWizard(QWidget *parent = nullptr);
        ~MainWizard();

        bool findFiles(const QString &name, const QString &path);
        void addInstallation(const QString &path, bool configureSelected = true);
        void configureDataFiles(const QString& path);
        void runSettingsImporter();

        QMap<QString, Installation> mInstallations;

        Files::ConfigurationManager mCfgMgr;

        Process::ProcessInvoker *mImporterInvoker;

        bool mError;

    public slots:
        void addLogText(const QString &text);

    private:
        /// convert boost::filesystem::path to QString
        QString toQString(const boost::filesystem::path& path) const;

        void setupLog();
        void setupGameSettings();
        void setupLauncherSettings();
        void setupInstallations();
        void setupPages();

        bool loadBuildManifest(const QString& dataFilesPath);
        bool writeBuildManifest(const QString& dataFilesPath);
        void readClientEndpoint(QString& address, QString& port) const;
        void writeClientEndpoint(const QString& address, const QString& port) const;
        void initializeNativeDisplaySettings();
        void writeSettings();

        Config::GameSettings mGameSettings;
        Config::LauncherSettings mLauncherSettings;

        bool mBuildManifestLoaded;
        QString mBuildManifestPath;
        QString mBuildName;
        QString mBuildDataPath;
        QString mBuildServerAddress;
        QString mBuildServerPort;
        QString mBuildLanguage;
        bool mBuildLanguageLocked;

        QString mLogError;

    private slots:

        void importerStarted();
        void importerFinished(int exitCode, QProcess::ExitStatus exitStatus);

        void accept() override;
        void reject() override;

    };

}

#endif // MAINWIZARD_HPP
