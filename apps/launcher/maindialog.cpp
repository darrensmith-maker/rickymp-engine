#include "maindialog.hpp"

#include <components/version/version.hpp>
#include <components/misc/helpviewer.hpp>
#include <components/config/buildmanifest.hpp>
#include <components/config/contentorder.hpp>

#include <QDate>
#include <QMessageBox>
#include <QFontDatabase>
#include <QInputDialog>
#include <QFileDialog>
#include <QCloseEvent>
#include <QTextCodec>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <boost/crc.hpp>
#include <QResizeEvent>
#include <QByteArray>
#include <QTimer>


#include "playpage.hpp"
#include "graphicspage.hpp"
#include <QTextStream>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include "datafilespage.hpp"
#include "settingspage.hpp"
#include "advancedpage.hpp"
#include "serverdialog.hpp"

using namespace Process;

void cfgError(const QString& title, const QString& msg) {
    QMessageBox msgBox;
    msgBox.setWindowTitle(title);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setText(msg);
    msgBox.exec();
}

namespace
{
    constexpr int sLauncherWidth = 1024;
    constexpr int sLauncherHeight = 650;

    bool containsGameContent(const QDir& dir)
    {
        if (!dir.exists())
            return false;

        const QStringList files = dir.entryList(
            QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QString& fileName : files)
        {
            if (fileName.endsWith(QLatin1String(".esm"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".esp"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".omwgame"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".omwaddon"), Qt::CaseInsensitive))
                return true;
        }
        return false;
    }

    QString resolveDataFilesDirectory(const QString& selectedPath)
    {
        if (selectedPath.trimmed().isEmpty())
            return QString();

        const QString cleanPath = QDir::cleanPath(selectedPath);
        const QDir selectedDir(cleanPath);
        if (!selectedDir.exists())
            return QString();
        if (containsGameContent(selectedDir))
            return cleanPath;

        const QStringList childDirectories = selectedDir.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QString& childName : childDirectories)
        {
            if (childName.compare(QLatin1String("Data Files"), Qt::CaseInsensitive) != 0)
                continue;

            const QString childPath = QDir::cleanPath(selectedDir.filePath(childName));
            if (containsGameContent(QDir(childPath)))
                return childPath;
        }
        return QString();
    }
}

Launcher::MainDialog::MainDialog(QWidget *parent)
    : QMainWindow(parent)
    , mPlayPage(nullptr)
    , mGraphicsPage(nullptr)
    , mDataFilesPage(nullptr)
    , mSettingsPage(nullptr)
    , mAdvancedPage(nullptr)
    , mGameInvoker(nullptr)
    , mWizardInvoker(nullptr)
    , mServerDialog(nullptr)
    , mWatermarkLabel(nullptr)
    , mBuildManifestLoaded(false)
    , mBuildName(QStringLiteral("ArenaMP"))
    , mBuildServerAddress(QStringLiteral("127.0.0.1"))
    , mBuildServerPort(QStringLiteral("25565"))
    , mBuildVanillaServerCompatibility(false)
    , mBuildServerAddressSpecified(false)
    , mBuildServerPortSpecified(false)
    , mBuildComplete(false)
    , mPendingVanillaServerCompatibility(false)
    , mGameSettings (mCfgMgr)
{
    setupUi(this);
    setFixedSize(sLauncherWidth, sLauncherHeight);

    mGameInvoker = new ProcessInvoker();
    mWizardInvoker = new ProcessInvoker();
    mServerDialog = new ServerDialog(this);
    mWatermarkLabel = new QLabel(centralwidget);
    const QByteArray watermarkEncoded = QByteArray("VEVTM01QIDAuOC4xIFplcjBDdXN0b20=");
    const QString watermarkText = QString::fromUtf8(QByteArray::fromBase64(watermarkEncoded));
    mWatermarkLabel->setText(watermarkText);
    mWatermarkLabel->setObjectName(QStringLiteral("zer0customWatermark"));
    mWatermarkLabel->setProperty("wm_b64", QString::fromUtf8(watermarkEncoded));
    mWatermarkLabel->setProperty("wm_guard", QString::number(qHash(QString::fromUtf8(watermarkEncoded))));
    mWatermarkLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    mWatermarkLabel->setStyleSheet(QStringLiteral("QLabel#zer0customWatermark { color: rgba(255, 255, 255, 88); font-size: 16px; font-weight: 600; background: transparent; }"));
    mWatermarkLabel->adjustSize();

    connect(mWizardInvoker->getProcess(), SIGNAL(started()),
            this, SLOT(wizardStarted()));

    connect(mWizardInvoker->getProcess(), SIGNAL(finished(int,QProcess::ExitStatus)),
            this, SLOT(wizardFinished(int,QProcess::ExitStatus)));

    iconWidget->setViewMode(QListView::IconMode);
    iconWidget->setWrapping(false);
    iconWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Just to be sure
    iconWidget->setIconSize(QSize(48, 48));
    iconWidget->setMovement(QListView::Static);

    iconWidget->setSpacing(4);
    iconWidget->setCurrentRow(0);
    iconWidget->setFlow(QListView::LeftToRight);

    QPushButton *helpButton = new QPushButton(tr("Help"));
    QPushButton *playButton = new QPushButton(tr("Play"));
    QPushButton *serverButton = new QPushButton(tr("Run Server"));
    buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));
    buttonBox->addButton(helpButton, QDialogButtonBox::HelpRole);
    buttonBox->addButton(serverButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(playButton, QDialogButtonBox::AcceptRole);

    connect(buttonBox, SIGNAL(rejected()), this, SLOT(close()));
    connect(buttonBox, SIGNAL(accepted()), this, SLOT(play()));
    connect(serverButton, SIGNAL(clicked()), this, SLOT(runServer()));
    connect(buttonBox, SIGNAL(helpRequested()), this, SLOT(help()));

    // Remove what's this? button
    setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    createIcons();
    updateWatermarkPosition();
}

Launcher::MainDialog::~MainDialog()
{
    delete mGameInvoker;
    delete mWizardInvoker;
}

void Launcher::MainDialog::createIcons()
{
    if (!QIcon::hasThemeIcon("document-new"))
        QIcon::setThemeName("tango");

    QListWidgetItem *playButton = new QListWidgetItem(iconWidget);
    playButton->setIcon(QIcon(":/images/openmw.png"));
    playButton->setText(tr("Play"));
    playButton->setTextAlignment(Qt::AlignCenter);
    playButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *dataFilesButton = new QListWidgetItem(iconWidget);
    dataFilesButton->setIcon(QIcon(":/images/openmw-plugin.png"));
    dataFilesButton->setText(tr("Data Files"));
    dataFilesButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    dataFilesButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *graphicsButton = new QListWidgetItem(iconWidget);
    graphicsButton->setIcon(QIcon(":/images/preferences-video.png"));
    graphicsButton->setText(tr("Graphics"));
    graphicsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom | Qt::AlignAbsolute);
    graphicsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *settingsButton = new QListWidgetItem(iconWidget);
    settingsButton->setIcon(QIcon(":/images/preferences.png"));
    settingsButton->setText(tr("Settings"));
    settingsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    settingsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QListWidgetItem *advancedButton = new QListWidgetItem(iconWidget);
    advancedButton->setIcon(QIcon(":/images/preferences-advanced.png"));
    advancedButton->setText(tr("Advanced"));
    advancedButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    advancedButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    connect(iconWidget,
            SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)),
            this, SLOT(changePage(QListWidgetItem*,QListWidgetItem*)));

}

void Launcher::MainDialog::createPages()
{
    // Avoid creating the widgets twice
    if (pagesWidget->count() != 0)
        return;

    mPlayPage = new PlayPage(this);
    mPlayPage->setBuildName(mBuildName);
    mDataFilesPage = new DataFilesPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mGraphicsPage = new GraphicsPage(mLauncherSettings, this);
    mSettingsPage = new SettingsPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mAdvancedPage = new AdvancedPage(mGameSettings, this);
    mPlayPage->setServerConsoleWidget(mServerDialog);

    auto readLauncherBool = [this](const QString& key, const QString& defaultValue) -> bool
    {
        if (mLauncherSettings.getSettings().contains(key))
            return mLauncherSettings.value(key).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        return defaultValue.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    };

    const QString localServerDefault = mBuildManifestLoaded && mBuildServerAddressSpecified
        ? QStringLiteral("false") : QStringLiteral("true");
    const bool autoStartServer = readLauncherBool(QStringLiteral("General/Server/autoStart"), localServerDefault);
    const bool autoRestartServer = readLauncherBool(QStringLiteral("General/Server/autoRestart"), localServerDefault);

    // build.ini only supplies the initial default. Once the user changes a
    // main-page checkbox, the launcher setting is preserved across restarts.
    const QString vanillaDefault = mBuildManifestLoaded && mBuildVanillaServerCompatibility
        ? QStringLiteral("true") : QStringLiteral("false");
    const bool vanillaServerCompatibility = mBuildComplete
        ? mBuildVanillaServerCompatibility
        : readLauncherBool(QStringLiteral("General/Server/vanillaBuild"), vanillaDefault);
    const bool hideChatHistory = readLauncherBool(QStringLiteral("General/Chat/hideHistory"), QStringLiteral("false"));
    mPlayPage->setAutoStartServer(autoStartServer);
    mPlayPage->setAutoRestartServer(autoRestartServer);
    mPlayPage->setVanillaServerCompatibility(vanillaServerCompatibility);
    mPlayPage->setHideChatHistory(hideChatHistory);
    mPlayPage->setHostBindAddress(mLauncherSettings.value(
        QStringLiteral("General/Server/bindAddress"), QStringLiteral("0.0.0.0")));

    mServerDialog->setAutoRestartEnabled(autoRestartServer);

    {
        QString addr = mBuildManifestLoaded ? mBuildServerAddress : QStringLiteral("localhost");
        QString port = mBuildManifestLoaded ? mBuildServerPort : QStringLiteral("25565");

        if (!mBuildManifestLoaded)
        {
            const QString cfgPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str())
                + QStringLiteral("/tes3mp-client-default.cfg");
            QFile cfgFile(cfgPath);
            if (cfgFile.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                QTextStream in(&cfgFile);
                while (!in.atEnd())
                {
                    const QString line = in.readLine().trimmed();
                    if (line.startsWith(QLatin1String("destinationAddress")))
                        addr = line.section(QLatin1Char('='), 1).trimmed();
                    else if (line.startsWith(QLatin1String("port")) && !line.startsWith(QLatin1String("password")))
                        port = line.section(QLatin1Char('='), 1).trimmed();
                }
            }
        }

        if (autoStartServer)
        {
            // The editable address is the endpoint advertised/shared with other
            // players. Do not overwrite it with a LAN adapter merely because
            // Host mode is enabled. The local host client uses a separate
            // loopback/bind endpoint when launching.
            port = mServerDialog->configuredPort();
        }

        mPlayPage->setServerAddress(addr);
        mPlayPage->setServerPort(port);

        const bool managedServer = mServerDialog->isRunning();
        const bool reachableServer = managedServer
            || (autoStartServer && mServerDialog->isServerReachable(120));
        mPlayPage->setServerRunning(reachableServer, addr, port, managedServer);
        if (reachableServer)
        {
            versionLabel->setText(tr("Online server - %1:%2").arg(addr, port));
            versionLabel->setStyleSheet(QStringLiteral("color: #188a3b; font-weight: 600;"));
        }
    }

    // Add the pages to the stacked widget
    pagesWidget->addWidget(mPlayPage);
    pagesWidget->addWidget(mDataFilesPage);
    pagesWidget->addWidget(mGraphicsPage);
    pagesWidget->addWidget(mSettingsPage);
    pagesWidget->addWidget(mAdvancedPage);

    applyBuildManifestRestrictions();

    // Select the first page
    iconWidget->setCurrentItem(iconWidget->item(0), QItemSelectionModel::Select);

    connect(mPlayPage, SIGNAL(playButtonClicked()), this, SLOT(play()));
    connect(mPlayPage, SIGNAL(serverButtonClicked()), this, SLOT(runServer()));
    connect(mPlayPage, SIGNAL(stopServerButtonClicked()), this, SLOT(stopServer()));
    connect(mPlayPage, SIGNAL(autoStartServerChanged(bool)), this, SLOT(autoStartServerChanged(bool)));
    connect(mPlayPage, SIGNAL(autoRestartServerChanged(bool)), this, SLOT(autoRestartServerChanged(bool)));
    connect(mPlayPage, SIGNAL(vanillaServerCompatibilityChanged(bool)), this, SLOT(vanillaServerCompatibilityChanged(bool)));
    connect(mPlayPage, SIGNAL(hideChatHistoryChanged(bool)), this, SLOT(hideChatHistoryChanged(bool)));
    connect(mPlayPage, SIGNAL(updateHashesRequested()), this, SLOT(updateServerDataFileHashes()));
    connect(mPlayPage, SIGNAL(clearServerCellsRequested()), this, SLOT(clearServerCells()));
    connect(mPlayPage, SIGNAL(resetServerDataRequested()), this, SLOT(resetServerData()));
    connect(mServerDialog, SIGNAL(runningChanged(bool,QString,QString)),
            this, SLOT(serverRunningChanged(bool,QString,QString)));
    connect(mServerDialog, SIGNAL(autoRestartChanged(bool)),
            this, SLOT(autoRestartServerChanged(bool)));

    // Using Qt::QueuedConnection because signal is emitted in a subthread and slot is in the main thread
    connect(mDataFilesPage, SIGNAL(signalLoadedCellsChanged(QStringList)), mAdvancedPage, SLOT(slotLoadedCellsChanged(QStringList)), Qt::QueuedConnection);

}

Launcher::FirstRunDialogResult Launcher::MainDialog::showFirstRunDialog()
{
    if (!setupLauncherSettings())
        return FirstRunDialogResultFailure;

    if (mLauncherSettings.value(QString("General/firstrun"), QString("true")) == QLatin1String("true"))
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("First run"));
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(tr("<html><head/><body><p><b>Welcome to OpenMW!</b></p> \
                          <p>It is recommended to run the Installation Wizard.</p> \
                          <p>The Wizard will let you select an existing Morrowind installation, \
                          or install Morrowind for OpenMW to use.</p></body></html>"));

        QAbstractButton *wizardButton =
                msgBox.addButton(tr("Run &Installation Wizard"), QMessageBox::AcceptRole); // ActionRole doesn't work?!
        QAbstractButton *skipButton =
                msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return FirstRunDialogResultWizard;
        }
        else if (msgBox.clickedButton() == skipButton)
        {
            // Don't bother setting up absent game data.
            if (setup())
                return FirstRunDialogResultContinue;
        }
        return FirstRunDialogResultFailure;
    }

    if (!setup() || !setupGameData()) {
        return FirstRunDialogResultFailure;
    }
    return FirstRunDialogResultContinue;
}

void Launcher::MainDialog::setVersionLabel()
{
    versionLabel->setText(tr("Server stopped"));
    versionLabel->setStyleSheet(QStringLiteral("color: #777777; font-weight: 600;"));
}

bool Launcher::MainDialog::setup()
{
    if (!setupGameSettings())
        return false;

    loadBuildManifest();
    setVersionLabel();

    // An existing build.ini is authoritative. Its exact content list was
    // already copied into the named launcher profile by loadBuildManifest().
    // Re-synchronising from merged openmw.cfg here could select stale plugins
    // that are installed but are not present in build.ini.
    if (!mBuildManifestLoaded)
        mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    // Now create the pages as they need the settings
    createPages();

    // Call this so we can exit on SDL errors before mainwindow is shown
    if (!mGraphicsPage->loadSettings())
        return false;

    loadSettings();

    return true;
}

bool Launcher::MainDialog::reloadSettings()
{
    if (!setupLauncherSettings())
        return false;

    if (!setupGameSettings())
        return false;

    loadBuildManifest();
    applyBuildManifestRestrictions();
    if (!mBuildManifestLoaded)
        mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    if (!mSettingsPage->loadSettings())
        return false;

    if (!mDataFilesPage->loadSettings())
        return false;

    if (!mGraphicsPage->loadSettings())
        return false;

    if (!mAdvancedPage->loadSettings())
        return false;

    // Refresh top-level launcher widgets as well, so a build selected in the
    // Wizard (new build.ini, new build name, new endpoint, new host-mode
    // defaults) is immediately reflected without restarting the launcher.
    loadSettings();
    applyBuildManifestRestrictions();

    return true;
}

void Launcher::MainDialog::changePage(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (!current)
        current = previous;

    int currentIndex = iconWidget->row(current);
    pagesWidget->setCurrentIndex(currentIndex);
    mSettingsPage->resetProgressBar();
}

bool Launcher::MainDialog::setupLauncherSettings()
{
    mLauncherSettings.clear();

    mLauncherSettings.setMultiValueEnabled(true);

    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QDir userDir(userPath);

    QStringList paths;
    paths.append(QString(Config::LauncherSettings::sLauncherConfigFileName));
    paths.append(userDir.filePath(QString(Config::LauncherSettings::sLauncherConfigFileName)));

    for (const QString &path : paths)
    {
        qDebug() << "Loading config file:" << path.toUtf8().constData();
        QFile file(path);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                cfgError(tr("Error opening OpenMW configuration file"),
                         tr("<br><b>Could not open %0 for reading</b><br><br> \
                             Please make sure you have the right permissions \
                             and try again.<br>").arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mLauncherSettings.readFile(stream);
        }
        file.close();
    }

    return true;
}

bool Launcher::MainDialog::setupGameSettings()
{
    mGameSettings.clear();

    QString localPath = QString::fromUtf8(mCfgMgr.getLocalPath().string().c_str());
    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QString globalPath = QString::fromUtf8(mCfgMgr.getGlobalPath().string().c_str());
    QDir localDir(localPath);
    QDir userDir(userPath);
    QDir globalDir(globalPath);

    // Load the user config file first, separately
    // So we can write it properly, uncontaminated
    QString path = userDir.filePath(QLatin1String("openmw.cfg"));
    QFile file(path);

    qDebug() << "Loading config file:" << path.toUtf8().constData();

    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            cfgError(tr("Error opening OpenMW configuration file"),
                     tr("<br><b>Could not open %0 for reading</b><br><br> \
                         Please make sure you have the right permissions \
                         and try again.<br>").arg(file.fileName()));
            return false;
        }
        QTextStream stream(&file);
        stream.setCodec(QTextCodec::codecForName("UTF-8"));

        mGameSettings.readUserFile(stream);
        file.close();
    }

    // Now the rest - priority: user > local > global
    QStringList paths;
    paths.append(globalDir.filePath(QString("openmw.cfg")));
    paths.append(localDir.filePath(QString("openmw.cfg")));
    paths.append(userDir.filePath(QString("openmw.cfg")));

    for (const QString &path2 : paths)
    {
        qDebug() << "Loading config file:" << path2.toUtf8().constData();

        file.setFileName(path2);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                cfgError(tr("Error opening OpenMW configuration file"),
                         tr("<br><b>Could not open %0 for reading</b><br><br> \
                             Please make sure you have the right permissions \
                             and try again.<br>").arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mGameSettings.readFile(stream);
            file.close();
        }
    }

    // Normalize legacy data= entries before loadBuildManifest() runs. Older
    // Wizard builds could store the Morrowind root instead of Data Files.
    const QStringList configuredDataDirs = mGameSettings.getDataDirs();
    for (const QString& configuredPath : configuredDataDirs)
    {
        const QString resolvedPath = resolveDataFilesDirectory(configuredPath);
        if (!resolvedPath.isEmpty()
            && !mGameSettings.getDataDirs().contains(resolvedPath, Qt::CaseInsensitive))
        {
            mGameSettings.setMultiValue(QLatin1String("data"), resolvedPath);
            mGameSettings.addDataDir(resolvedPath);
        }
    }

    return true;
}

bool Launcher::MainDialog::setupGameData()
{
    QStringList candidates = mGameSettings.getDataDirs();
    if (!mBuildDataPath.isEmpty())
        candidates.prepend(mBuildDataPath);

    const QString localPath = QString::fromUtf8(mCfgMgr.getLocalPath().string().c_str());
    if (!localPath.isEmpty())
    {
        candidates.append(QDir(localPath).filePath(QLatin1String("Data Files")));
        candidates.append(localPath);
    }

    QStringList dataDirs;
    for (const QString& candidate : candidates)
    {
        const QString resolvedPath = resolveDataFilesDirectory(candidate);
        if (resolvedPath.isEmpty()
            || dataDirs.contains(resolvedPath, Qt::CaseInsensitive))
            continue;

        dataDirs.append(resolvedPath);
        if (!mGameSettings.getDataDirs().contains(resolvedPath, Qt::CaseInsensitive))
        {
            mGameSettings.setMultiValue(QLatin1String("data"), resolvedPath);
            mGameSettings.addDataDir(resolvedPath);
        }
    }

    if (dataDirs.isEmpty())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error detecting Morrowind installation"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(tr("<br><b>Could not find the Data Files location</b><br><br> \
                                   The directory containing the data files was not found."));

        QAbstractButton *wizardButton =
                msgBox.addButton(tr("Run &Installation Wizard..."), QMessageBox::ActionRole);
        QAbstractButton *skipButton =
                msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        Q_UNUSED(skipButton); // Suppress compiler unused warning

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (!mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return false;
        }
    }

    return true;
}

QString Launcher::MainDialog::primaryDataDirectory() const
{
    const QString dataLocal = mGameSettings.getDataLocal();
    if (!dataLocal.isEmpty() && QFileInfo(dataLocal).isDir())
        return QDir::cleanPath(dataLocal);

    const QStringList dataDirs = mGameSettings.getDataDirs();
    for (auto it = dataDirs.crbegin(); it != dataDirs.crend(); ++it)
    {
        if (!Config::BuildManifest::findForDataDir(*it).isEmpty())
            return QDir::cleanPath(*it);
    }

    const QStringList filters = {
        QStringLiteral("*.esm"), QStringLiteral("*.esp"),
        QStringLiteral("*.omwgame"), QStringLiteral("*.omwaddon")
    };
    for (auto it = dataDirs.crbegin(); it != dataDirs.crend(); ++it)
    {
        QDir dir(*it);
        if (dir.exists() && !dir.entryList(filters, QDir::Files | QDir::Readable).isEmpty())
            return QDir::cleanPath(*it);
    }

    return dataDirs.isEmpty() ? QString() : QDir::cleanPath(dataDirs.last());
}

bool Launcher::MainDialog::loadBuildManifest()
{
    mBuildManifestLoaded = false;
    mBuildManifestPath.clear();
    mBuildName = mLauncherSettings.value(QStringLiteral("General/Build/name"), QStringLiteral("ArenaMP"));
    mBuildDataPath = primaryDataDirectory();
    mBuildServerAddress = QStringLiteral("127.0.0.1");
    mBuildServerPort = QStringLiteral("25565");
    mBuildVanillaServerCompatibility = false;
    mBuildServerAddressSpecified = false;
    mBuildServerPortSpecified = false;
    mBuildComplete = false;

    if (mBuildDataPath.isEmpty())
        return false;

    const QString manifestPath = Config::BuildManifest::findForDataDir(mBuildDataPath);
    if (manifestPath.isEmpty())
    {
        // No saved build order exists yet. Apply the recommended order once,
        // then the first Launcher save creates build.ini and all later starts
        // preserve the user's exact order from that file.
        const QStringList orderedContent = Config::applyCanonicalContentOrder(
            mGameSettings.getContentList(), QDir(mBuildDataPath));
        if (!orderedContent.isEmpty())
        {
            mGameSettings.setContentList(orderedContent);
            QString profileName = mLauncherSettings.getCurrentContentListName();
            if (profileName.isEmpty())
                profileName = QStringLiteral("Default");
            QStringList profileFiles = orderedContent;
            const QStringList groundcover = mGameSettings.getGroundcoverList();
            profileFiles.append(groundcover);
            mLauncherSettings.setContentList(profileName, profileFiles, groundcover,
                !groundcover.isEmpty());
            mLauncherSettings.setCurrentContentListName(profileName);
        }
        return false;
    }

    Config::BuildManifest manifest;
    QString error;
    if (!manifest.read(manifestPath, &error))
    {
        qWarning() << "Could not read build manifest" << manifestPath << error;
        return false;
    }

    const QString resolvedDataPath = manifest.resolvedDataPath(manifestPath);
    if (QFileInfo(resolvedDataPath).isDir())
    {
        mBuildDataPath = QDir::cleanPath(resolvedDataPath);
        if (!mGameSettings.getDataDirs().contains(mBuildDataPath))
        {
            mGameSettings.setMultiValue(QStringLiteral("data"), mBuildDataPath);
            mGameSettings.addDataDir(mBuildDataPath);
        }
    }

    const QString effectiveBuildName = manifest.buildName.trimmed().isEmpty()
        ? QStringLiteral("ArenaMP") : manifest.buildName.trimmed();
    // A present build.ini always owns the enabled plug-in list, including an
    // intentionally empty one. Never merge it with launcher.cfg/openmw.cfg or
    // auto-enable other plug-ins merely because they exist in Data Files.
    const QStringList orderedContent = manifest.contentFiles;
    mGameSettings.setContentList(orderedContent);
    mGameSettings.setGroundcoverList(manifest.groundcoverFiles);

    QStringList profileFiles = orderedContent;
    profileFiles.append(manifest.groundcoverFiles);
    mLauncherSettings.setContentList(effectiveBuildName, profileFiles,
        manifest.groundcoverFiles, !manifest.groundcoverFiles.isEmpty());
    mLauncherSettings.setCurrentContentListName(effectiveBuildName);

    if (!manifest.archives.isEmpty())
    {
        mGameSettings.remove(QStringLiteral("fallback-archive"));
        for (const QString& archive : manifest.archives)
            mGameSettings.setMultiValue(QStringLiteral("fallback-archive"), archive);
    }

    // Do not manufacture English when an older manifest has no language field.
    // When the field is present, build.ini is authoritative and the translated
    // launcher UI must not replace its canonical value.
    if (manifest.languageSpecified)
    {
        const QString manifestLanguage = Config::BuildManifest::canonicalLanguage(manifest.language);
        mLauncherSettings.remove(QStringLiteral("Settings/language"));
        mLauncherSettings.setValue(QStringLiteral("Settings/language"), manifestLanguage);
        if (manifestLanguage == QLatin1String("Polish"))
            mGameSettings.setValue(QStringLiteral("encoding"), QStringLiteral("win1250"));
        else if (manifestLanguage == QLatin1String("Russian"))
            mGameSettings.setValue(QStringLiteral("encoding"), QStringLiteral("win1251"));
        else
            mGameSettings.setValue(QStringLiteral("encoding"), QStringLiteral("win1252"));
    }

    mBuildManifestLoaded = true;
    mBuildManifestPath = manifestPath;
    mBuildName = effectiveBuildName;
    mBuildServerAddress = manifest.serverAddress.trimmed().isEmpty()
        ? QStringLiteral("127.0.0.1") : manifest.serverAddress.trimmed();
    mBuildServerPort = manifest.serverPort.trimmed().isEmpty()
        ? QStringLiteral("25565") : manifest.serverPort.trimmed();
    mBuildVanillaServerCompatibility = manifest.vanillaServerCompatibility;
    mBuildServerAddressSpecified = manifest.serverAddressSpecified;
    mBuildServerPortSpecified = manifest.serverPortSpecified;
    mBuildComplete = manifest.complete;
    writeClientEndpoint(mBuildServerAddress, mBuildServerPort);

    if (mPlayPage != nullptr)
    {
        mPlayPage->setBuildName(mBuildName);
        mPlayPage->setServerAddress(mBuildServerAddress);
        mPlayPage->setServerPort(mBuildServerPort);
        mPlayPage->setVanillaServerCompatibility(mBuildVanillaServerCompatibility);
        mPlayPage->setBuildManifestComplete(mBuildComplete);
        if (mBuildServerAddressSpecified)
        {
            mPlayPage->setAutoStartServer(false);
            mPlayPage->setAutoRestartServer(false);
            if (mServerDialog != nullptr)
                mServerDialog->setAutoRestartEnabled(false);
        }
    }

    qDebug() << "Loaded ArenaMP build manifest:" << manifestPath;
    return true;
}

bool Launcher::MainDialog::writeBuildManifest()
{
    QString dataDir = mBuildDataPath;
    if (dataDir.isEmpty() || !QFileInfo(dataDir).isDir())
        dataDir = primaryDataDirectory();
    if (dataDir.isEmpty() || !QFileInfo(dataDir).isDir())
        return true;

    // Always save the active manifest beside the selected Data Files. If an
    // older patch loaded a fallback copy beside the executable, read it as the
    // source once and migrate its values to the canonical location.
    const QString sourceManifestPath = mBuildManifestPath;
    const QString manifestPath = Config::BuildManifest::canonicalPathForDataDir(dataDir);

    Config::BuildManifest manifest;
    bool existingManifestRead = false;
    QString storedServerAddress;
    bool storedServerAddressSpecified = false;
    QString storedServerPort;
    bool storedServerPortSpecified = false;
    bool storedVanillaCompatibility = false;
    QString existingManifestPath = manifestPath;
    if (!sourceManifestPath.isEmpty() && QFileInfo::exists(sourceManifestPath))
        existingManifestPath = sourceManifestPath;

    if (QFileInfo::exists(existingManifestPath))
    {
        existingManifestRead = manifest.read(existingManifestPath);
        if (existingManifestRead)
        {
            storedServerAddress = manifest.serverAddress;
            storedServerAddressSpecified = manifest.serverAddressSpecified;
            storedServerPort = manifest.serverPort;
            storedServerPortSpecified = manifest.serverPortSpecified;
            storedVanillaCompatibility = manifest.vanillaServerCompatibility;
        }

    }

    manifest.formatVersion = 1;
    manifest.buildName = mPlayPage != nullptr ? mPlayPage->buildName() : mBuildName;
    manifest.dataPath = Config::BuildManifest::portableDataPath(manifestPath, dataDir);
    manifest.language = mLauncherSettings.value(QStringLiteral("Settings/language"), QStringLiteral("English"));
    const bool localServerModeSelected = mPlayPage != nullptr
        && mPlayPage->autoStartServer();

    if (localServerModeSelected && existingManifestRead)
    {
        // Host mode is a launcher choice. Do not replace the distributed remote
        // endpoint in build.ini with this machine's LAN address and local port.
        manifest.serverAddress = storedServerAddress;
        manifest.serverAddressSpecified = storedServerAddressSpecified;
        manifest.serverPort = storedServerPort;
        manifest.serverPortSpecified = storedServerPortSpecified;
        manifest.vanillaServerCompatibility = storedVanillaCompatibility;
    }
    else if (localServerModeSelected)
    {
        // A newly generated local-host profile intentionally has no distributed
        // endpoint. This allows Host mode to stay the default until an address
        // is explicitly added to build.ini.
        manifest.serverAddress = QStringLiteral("127.0.0.1");
        manifest.serverAddressSpecified = false;
        manifest.serverPort = QStringLiteral("25565");
        manifest.serverPortSpecified = false;
        manifest.vanillaServerCompatibility = false;
    }
    else
    {
        manifest.serverAddress = mPlayPage != nullptr ? mPlayPage->serverAddress() : mBuildServerAddress;
        manifest.serverAddressSpecified = !manifest.serverAddress.trimmed().isEmpty();
        manifest.serverPort = mPlayPage != nullptr ? mPlayPage->serverPort() : mBuildServerPort;
        manifest.serverPortSpecified = !manifest.serverPort.trimmed().isEmpty();
        manifest.vanillaServerCompatibility = mPlayPage != nullptr
            ? mPlayPage->vanillaServerCompatibility() : mBuildVanillaServerCompatibility;
    }
    manifest.complete = mBuildComplete;
    manifest.contentFiles = mGameSettings.getContentList();
    manifest.groundcoverFiles = mGameSettings.getGroundcoverList();
    manifest.archives = Config::LauncherSettings::reverse(
        mGameSettings.values(QStringLiteral("fallback-archive")));

    QString error;
    if (!manifest.write(manifestPath, &error))
    {
        cfgError(tr("Error writing ArenaMP build manifest"),
            tr("<br><b>Could not write %1</b><br><br>%2").arg(manifestPath, error));
        return false;
    }

    mBuildManifestLoaded = true;
    mBuildManifestPath = manifestPath;
    mBuildName = manifest.buildName;
    mBuildDataPath = dataDir;
    mBuildServerAddress = manifest.serverAddress;
    mBuildServerPort = manifest.serverPort;
    mBuildVanillaServerCompatibility = manifest.vanillaServerCompatibility;
    mBuildServerAddressSpecified = manifest.serverAddressSpecified;
    mBuildServerPortSpecified = manifest.serverPortSpecified;
    mBuildComplete = manifest.complete;
    writeClientEndpoint(mBuildServerAddress, mBuildServerPort);
    return true;
}

void Launcher::MainDialog::applyBuildManifestRestrictions()
{
    if (mPlayPage != nullptr)
    {
        mPlayPage->setBuildManifestComplete(mBuildComplete);
    }

    // build.ini stores the current user order; keep Data Files available so
    // deliberate Launcher changes can be written back to the manifest.
    if (iconWidget != nullptr && iconWidget->count() > 1)
        iconWidget->item(1)->setHidden(false);

    if (mAdvancedPage != nullptr)
        mAdvancedPage->setGameMechanicsVisible(!mBuildComplete);

}

bool Launcher::MainDialog::isLocalServerAddress(const QString& address) const
{
    const QString normalized = address.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QLatin1String("localhost")
        || normalized == QLatin1String("127.0.0.1") || normalized == QLatin1String("::1")
        || normalized == QLatin1String("0.0.0.0"))
        return true;

    return mServerDialog != nullptr
        && normalized == mServerDialog->displayAddress().trimmed().toLower();
}

void Launcher::MainDialog::writeClientEndpoint(const QString& address, const QString& port) const
{
    QDir userDir(QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str()));
    if (!userDir.exists())
        userDir.mkpath(QStringLiteral("."));

    QFile cfgFile(userDir.filePath(QStringLiteral("tes3mp-client-default.cfg")));
    QStringList lines;
    bool foundAddress = false;
    bool foundPort = false;

    if (cfgFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream input(&cfgFile);
        input.setCodec("UTF-8");
        while (!input.atEnd())
            lines.append(input.readLine());
        cfgFile.close();
    }

    for (QString& line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("destinationAddress")))
        {
            line = QStringLiteral("destinationAddress = ") + address;
            foundAddress = true;
        }
        else if (trimmed.startsWith(QLatin1String("port")) && !trimmed.startsWith(QLatin1String("password")))
        {
            line = QStringLiteral("port = ") + port;
            foundPort = true;
        }
    }

    if (!foundAddress)
        lines.append(QStringLiteral("destinationAddress = ") + address);
    if (!foundPort)
        lines.append(QStringLiteral("port = ") + port);

    if (cfgFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QTextStream output(&cfgFile);
        output.setCodec("UTF-8");
        for (const QString& line : lines)
            output << line << '\n';
    }
}

bool Launcher::MainDialog::setupGraphicsSettings()
{
    // This method is almost a copy of OMW::Engine::loadSettings().  They should definitely
    // remain consistent, and possibly be merged into a shared component.  At the very least
    // the filenames should be in the CfgMgr component.

    // Ensure to clear previous settings in case we had already loaded settings.
    mEngineSettings.clear();
    // ArenaMP uses settings-default.cfg as the canonical preset. Prefer the
    // text file so newly added fork settings are available immediately; keep
    // defaults.bin only as a compatibility fallback for incomplete packages.
    const std::string localDefault = (mCfgMgr.getLocalPath() / "defaults.bin").string();
    const std::string globalDefault = (mCfgMgr.getGlobalPath() / "defaults.bin").string();
    const std::string localDefaultCfg = (mCfgMgr.getLocalPath() / "settings-default.cfg").string();
    const std::string globalDefaultCfg = (mCfgMgr.getGlobalPath() / "settings-default.cfg").string();
    std::string defaultPath;
    bool defaultIsTextCfg = false;

    if (boost::filesystem::exists(localDefaultCfg))
    {
        defaultPath = localDefaultCfg;
        defaultIsTextCfg = true;
    }
    else if (boost::filesystem::exists(globalDefaultCfg))
    {
        defaultPath = globalDefaultCfg;
        defaultIsTextCfg = true;
    }
    else if (boost::filesystem::exists(localDefault))
        defaultPath = localDefault;
    else if (boost::filesystem::exists(globalDefault))
        defaultPath = globalDefault;
    else {
        cfgError(tr("Error reading OpenMW configuration file"),
                 tr("<br><b>Could not find defaults.bin or settings-default.cfg</b><br><br>                      The problem may be due to an incomplete installation of OpenMW.<br>                      Reinstalling OpenMW may resolve the problem."));
        return false;
    }

    try {
        mEngineSettings.loadDefault(defaultPath, !defaultIsTextCfg);
    }
    catch (std::exception& e) {
        std::string msg = std::string("<br><b>Error reading default settings</b><br><br>") + e.what();
        cfgError(tr("Error reading OpenMW configuration file"), tr(msg.c_str()));
        return false;
    }

    // Always configure the canonical user settings path. The explicit preset
    // Apply button may need to create settings.cfg after a clean Wizard run.
    const std::string userPath = mCfgMgr.getPrimarySettingsPath().string();
    mEngineSettings.setUserSettingsPath(userPath);

    // User settings are not required to exist.
    if (!boost::filesystem::exists(userPath))
        return true;

    try {
        mEngineSettings.loadUser(userPath);
    }
    catch (std::exception& e) {
        std::string msg = std::string("<br><b>Error reading settings.cfg</b><br><br>") + e.what();
        cfgError(tr("Error reading OpenMW configuration file"), tr(msg.c_str()));
        return false;
    }

    return true;
}

void Launcher::MainDialog::loadSettings()
{
    int posX = mLauncherSettings.value(QString("General/MainWindow/posx")).toInt();
    int posY = mLauncherSettings.value(QString("General/MainWindow/posy")).toInt();

    // Keep the page layouts stable and ignore stale width/height values.
    setFixedSize(sLauncherWidth, sLauncherHeight);
    move(posX, posY);

    if (mPlayPage != nullptr && mServerDialog != nullptr)
    {
        auto readLauncherBool = [this](const QString& key, const QString& defaultValue) -> bool
        {
            if (mLauncherSettings.getSettings().contains(key))
                return mLauncherSettings.value(key).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
            return defaultValue.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        };

        const QString localServerDefault = mBuildManifestLoaded && mBuildServerAddressSpecified
            ? QStringLiteral("false") : QStringLiteral("true");
        const bool autoStart = readLauncherBool(QStringLiteral("General/Server/autoStart"), localServerDefault);
        const bool autoRestart = readLauncherBool(QStringLiteral("General/Server/autoRestart"), localServerDefault);
        const QString vanillaDefault = mBuildManifestLoaded && mBuildVanillaServerCompatibility
            ? QStringLiteral("true") : QStringLiteral("false");
        const bool vanillaServerCompatibility = mBuildComplete
            ? mBuildVanillaServerCompatibility
            : readLauncherBool(QStringLiteral("General/Server/vanillaBuild"), vanillaDefault);
        const bool hideChatHistory = readLauncherBool(QStringLiteral("General/Chat/hideHistory"), QStringLiteral("false"));
        mPlayPage->setAutoStartServer(autoStart);
        mPlayPage->setAutoRestartServer(autoRestart);
        mPlayPage->setVanillaServerCompatibility(vanillaServerCompatibility);
        mPlayPage->setHideChatHistory(hideChatHistory);
        mPlayPage->setBuildName(mBuildName);

        mServerDialog->setAutoRestartEnabled(autoRestart);
    }
}

void Launcher::MainDialog::saveSettings()
{
    QString width = QString::number(sLauncherWidth);
    QString height = QString::number(sLauncherHeight);

    mLauncherSettings.remove(QString("General/MainWindow/width"));
    mLauncherSettings.remove(QString("General/MainWindow/height"));
    mLauncherSettings.setValue(QString("General/MainWindow/width"), width);
    mLauncherSettings.setValue(QString("General/MainWindow/height"), height);

    QString posX = QString::number(this->pos().x());
    QString posY = QString::number(this->pos().y());

    mLauncherSettings.setValue(QString("General/MainWindow/posx"), posX);
    mLauncherSettings.setValue(QString("General/MainWindow/posy"), posY);

    mLauncherSettings.setValue(QString("General/firstrun"), QString("false"));

    if (mPlayPage != nullptr)
    {
        mLauncherSettings.remove(QStringLiteral("General/Server/autoStart"));
        mLauncherSettings.setValue(QStringLiteral("General/Server/autoStart"),
            mPlayPage->autoStartServer() ? QStringLiteral("true") : QStringLiteral("false"));
        mLauncherSettings.remove(QStringLiteral("General/Server/autoRestart"));
        mLauncherSettings.setValue(QStringLiteral("General/Server/autoRestart"),
            mPlayPage->autoRestartServer() ? QStringLiteral("true") : QStringLiteral("false"));
        mLauncherSettings.remove(QStringLiteral("General/Server/vanillaBuild"));
        mLauncherSettings.setValue(QStringLiteral("General/Server/vanillaBuild"),
            mPlayPage->vanillaServerCompatibility() ? QStringLiteral("true") : QStringLiteral("false"));
        mLauncherSettings.remove(QStringLiteral("General/Server/bindAddress"));
        mLauncherSettings.setValue(QStringLiteral("General/Server/bindAddress"),
            mPlayPage->hostBindAddress());
        mLauncherSettings.remove(QStringLiteral("General/Chat/hideHistory"));
        mLauncherSettings.setValue(QStringLiteral("General/Chat/hideHistory"),
            mPlayPage->hideChatHistory() ? QStringLiteral("true") : QStringLiteral("false"));
        mLauncherSettings.setValue(QStringLiteral("General/Build/name"), mPlayPage->buildName());
    }

}

bool Launcher::MainDialog::writeSettings()
{
    // Now write all config files
    saveSettings();
    mDataFilesPage->saveSettings();

    // Do not copy the Launcher's in-memory graphics/advanced controls back to
    // settings.cfg here. The game may have changed that file while the
    // Launcher remained open. Graphics quality is written only by the
    // explicit Apply preset button; the Wizard performs its one initial pass.
    mSettingsPage->saveSettings();
    if (!mPlayPage->saveServerSettings())
        return false;

    QString userPath = QString::fromUtf8(mCfgMgr.getUserConfigPath().string().c_str());
    QDir dir(userPath);

    if (!dir.exists()) {
        if (!dir.mkpath(userPath)) {
            cfgError(tr("Error creating OpenMW configuration directory"),
                     tr("<br><b>Could not create %0</b><br><br> \
                         Please make sure you have the right permissions \
                         and try again.<br>").arg(userPath));
            return false;
        }
    }

    // Game settings
    QFile file(dir.filePath(QString("openmw.cfg")));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        // File cannot be opened or created
        cfgError(tr("Error writing OpenMW configuration file"),
                 tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>").arg(file.fileName()));
        return false;
    }


    mGameSettings.writeFileWithComments(file);
    file.close();

    // Launcher settings
    file.setFileName(dir.filePath(QString(Config::LauncherSettings::sLauncherConfigFileName)));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate)) {
        // File cannot be opened or created
        cfgError(tr("Error writing Launcher configuration file"),
                 tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>").arg(file.fileName()));
        return false;
    }

    QTextStream stream(&file);
    stream.setDevice(&file);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));

    mLauncherSettings.writeFile(stream);
    file.close();

    if (!writeBuildManifest())
        return false;

    return true;
}

void Launcher::MainDialog::updateWatermarkPosition()
{
    if (mWatermarkLabel == nullptr)
        return;

    mWatermarkLabel->adjustSize();
    const int margin = 14;
    const QSize size = mWatermarkLabel->sizeHint();
    mWatermarkLabel->move(centralwidget->width() - size.width() - margin,
                          centralwidget->height() - size.height() - margin);
    mWatermarkLabel->raise();
}

void Launcher::MainDialog::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateWatermarkPosition();
}

void Launcher::MainDialog::closeEvent(QCloseEvent *event)
{
    writeSettings();
    event->accept();
}

void Launcher::MainDialog::wizardStarted()
{
    hide();
}

void Launcher::MainDialog::wizardFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus == QProcess::CrashExit)
        return qApp->quit();

    // The Wizard has just replaced openmw.cfg and launcher.cfg. Reload both
    // before validating the selected Data Files directory.
    if (!setup() || !reloadSettings())
        return qApp->quit();

    if (setupGameData())
    {
        show();
        raise();
        activateWindow();
    }
}

void Launcher::MainDialog::play()
{
    if (!writeSettings())
        return qApp->quit();

    if (!mGameSettings.hasMaster())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("No game file selected"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(tr("<br><b>You do not have a game file selected.</b><br><br> "
                          "OpenMW will not start without a game file selected.<br>"));
        msgBox.exec();
        return;
    }

    mPendingClientAddress = mPlayPage->serverAddress();
    mPendingClientPort = mPlayPage->serverPort();
    mPendingVanillaServerCompatibility = mPlayPage->vanillaServerCompatibility();
    mPendingHideChatHistory = mPlayPage->hideChatHistory();

    bool startedNow = false;
    const bool localServerMode = mPlayPage->autoStartServer();
    if (localServerMode)
    {
        // Host mode overrides the remote endpoint and vanilla compatibility for
        // this launch only. The build.ini endpoint remains unchanged.
        mPendingVanillaServerCompatibility = false;
        mServerDialog->setAutoRestartEnabled(mPlayPage->autoRestartServer());

        if (!mServerDialog->isRunning())
        {
            QString bindError;
            if (!mServerDialog->setConfiguredLocalAddress(mPlayPage->hostBindAddress(), &bindError))
            {
                QMessageBox::warning(this, tr("Invalid server interface"), bindError);
                mPendingClientAddress.clear();
                mPendingClientPort.clear();
                return;
            }

            QString portError;
            if (!mServerDialog->setConfiguredPort(mPlayPage->serverPort(), &portError))
            {
                QMessageBox::warning(this, tr("Invalid server port"), portError);
                mPendingClientAddress.clear();
                mPendingClientPort.clear();
                return;
            }
        }

        // The host's own client must use a local endpoint. The value in the
        // Server Address field may intentionally be a public WAN address and
        // may not support NAT loopback on the user's router.
        mPendingClientAddress = mServerDialog->localConnectAddress();
        mPendingClientPort = mServerDialog->configuredPort();
        writeClientEndpoint(mPendingClientAddress, mPendingClientPort);

        if (!mServerDialog->isRunning())
        {
            if (mServerDialog->isServerReachable())
            {
                mPlayPage->setServerRunning(true, mPendingClientAddress,
                    mPendingClientPort, false);
            }
            else
            {
                startedNow = mServerDialog->startServer();
                if (!startedNow)
                {
                    mPendingClientAddress.clear();
                    mPendingClientPort.clear();
                    return;
                }
            }
        }
    }

    if (startedNow)
        QTimer::singleShot(900, this, SLOT(launchClient()));
    else
        launchClient();
}

void Launcher::MainDialog::launchClient()
{
    const QString address = mPendingClientAddress.trimmed().isEmpty()
        ? mPlayPage->serverAddress() : mPendingClientAddress.trimmed();
    const QString port = mPendingClientPort.trimmed().isEmpty()
        ? mPlayPage->serverPort() : mPendingClientPort.trimmed();

    writeClientEndpoint(address, port);

    QStringList arguments;
    arguments.append(QLatin1String("--connect=") + address + QLatin1String(":") + port);
    if (mPendingVanillaServerCompatibility)
        arguments.append(QStringLiteral("--vanilla-build-server"));
    if (mPendingHideChatHistory)
        arguments.append(QStringLiteral("--hide-chat-history"));

    mPendingClientAddress.clear();
    mPendingClientPort.clear();
    mPendingVanillaServerCompatibility = false;
    mPendingHideChatHistory = false;

    if (mGameInvoker->startProcess(QLatin1String("tes3mp"), arguments, true))
    {
        if (mServerDialog != nullptr && mServerDialog->isRunning())
            return;
        qApp->quit();
    }
}

void Launcher::MainDialog::runServer()
{
    if (!writeSettings())
        return;

    mServerDialog->setAutoRestartEnabled(mPlayPage->autoRestartServer());

    if (!mServerDialog->isRunning())
    {
        QString bindError;
        if (!mServerDialog->setConfiguredLocalAddress(mPlayPage->hostBindAddress(), &bindError))
        {
            QMessageBox::warning(this, tr("Invalid server interface"), bindError);
            return;
        }

        QString portError;
        if (!mServerDialog->setConfiguredPort(mPlayPage->serverPort(), &portError))
        {
            QMessageBox::warning(this, tr("Invalid server port"), portError);
            return;
        }
    }

    mPlayPage->switchToServerConsoleTab();
    mServerDialog->startServer();
}

void Launcher::MainDialog::stopServer()
{
    if (mServerDialog != nullptr)
        mServerDialog->stopServer();
}


QString Launcher::MainDialog::resolveSelectedDataFilePath(const QString& fileName, const QStringList& selectedPaths) const
{
    for (const QString& path : selectedPaths)
    {
        if (QFileInfo(path).fileName().compare(fileName, Qt::CaseInsensitive) == 0)
            return path;
    }

    QStringList searchDirs;
    if (!mGameSettings.getDataLocal().isEmpty())
        searchDirs << mGameSettings.getDataLocal();
    searchDirs << mGameSettings.getDataDirs();
    for (const QString& dirPath : searchDirs)
    {
        const QString candidate = QDir(dirPath).filePath(fileName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

void Launcher::MainDialog::updateServerDataFileHashes()
{
    if (mPlayPage == nullptr || !mPlayPage->autoStartServer())
        return;

    // Hash generation and strict verification are one operation in Host mode.
    mPlayPage->setEnforceDataFiles(true);
    if (!mPlayPage->saveServerSettings())
        return;

    // Pull the current UI selection into GameSettings first. This guarantees
    // that JSON order is exactly the order visible on the Data Files page.
    mDataFilesPage->saveSettings();
    const QStringList selectedPaths = mDataFilesPage->selectedFilePaths();
    const QStringList contentFiles = mGameSettings.getContentList();
    const QStringList groundcoverFiles = mGameSettings.getGroundcoverList();

    if (contentFiles.isEmpty())
    {
        QMessageBox::warning(this, tr("Update Hash"), tr("No content files are selected."));
        return;
    }

    auto crc32ForPath = [](const QString& path, quint32* value) -> bool
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        boost::crc_32_type crc;
        while (!file.atEnd())
        {
            const QByteArray chunk = file.read(1024 * 1024);
            if (chunk.isEmpty() && file.error() != QFile::NoError)
                return false;
            crc.process_bytes(chunk.constData(), static_cast<std::size_t>(chunk.size()));
        }
        *value = crc.checksum();
        return true;
    };

    auto makeEntry = [&](const QString& fileName, QJsonObject* entry, QString* error) -> bool
    {
        const QString path = resolveSelectedDataFilePath(fileName, selectedPaths);
        if (path.isEmpty())
        {
            *error = tr("Selected data file was not found: %1").arg(fileName);
            return false;
        }
        quint32 crc = 0;
        if (!crc32ForPath(path, &crc))
        {
            *error = tr("Could not calculate CRC32 for %1").arg(QDir::toNativeSeparators(path));
            return false;
        }
        const QString hash = QStringLiteral("0x")
            + QString::number(crc, 16).rightJustified(8, QLatin1Char('0')).toUpper();
        QJsonArray hashes;
        hashes.append(hash);
        entry->insert(fileName, hashes);
        return true;
    };

    QJsonArray contentArray;
    QJsonArray groundcoverArray;
    QString error;
    for (const QString& fileName : contentFiles)
    {
        QJsonObject entry;
        if (!makeEntry(fileName, &entry, &error))
        {
            QMessageBox::critical(this, tr("Update Hash"), error);
            return;
        }
        contentArray.append(entry);
    }
    for (const QString& fileName : groundcoverFiles)
    {
        QJsonObject entry;
        if (!makeEntry(fileName, &entry, &error))
        {
            QMessageBox::critical(this, tr("Update Hash"), error);
            return;
        }
        groundcoverArray.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), 2);
    root.insert(QStringLiteral("content"), contentArray);
    root.insert(QStringLiteral("groundcover"), groundcoverArray);

    const QString path = mServerDialog->requiredDataFilesPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, tr("Update Hash"),
            tr("Could not write %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(json) != json.size() || !output.commit())
    {
        QMessageBox::critical(this, tr("Update Hash"),
            tr("Could not finish writing %1").arg(QDir::toNativeSeparators(path)));
        return;
    }

    // Keep build.ini in the same order as the generated server manifest.
    writeBuildManifest();

    QString status = tr("Server manifest updated.\n\nRequired content: %1\nOptional groundcover: %2\n\n%3")
        .arg(contentFiles.size()).arg(groundcoverFiles.size())
        .arg(QDir::toNativeSeparators(path));
    if (mServerDialog->isRunning())
        status += tr("\n\nRestart the server to apply the new manifest.");
    QMessageBox::information(this, tr("Update Hash"), status);
}

void Launcher::MainDialog::clearServerCells()
{
    if (mServerDialog == nullptr)
        return;
    if (mServerDialog->isRunning())
    {
        QMessageBox::warning(this, tr("Clear server cells"),
            tr("Stop the server before clearing persistent data."));
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::warning(this,
        tr("Clear server cells?"),
        tr("This will delete all saved cell state. Player accounts and world data will be kept. Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!mServerDialog->clearPersistentCells(&error))
    {
        QMessageBox::critical(this, tr("Clear server cells"),
            tr("Server data reset failed: %1").arg(error));
        return;
    }
    QMessageBox::information(this, tr("Clear server cells"), tr("All saved server cells were cleared."));
}

void Launcher::MainDialog::resetServerData()
{
    if (mServerDialog == nullptr)
        return;
    if (mServerDialog->isRunning())
    {
        QMessageBox::warning(this, tr("Full server reset"),
            tr("Stop the server before clearing persistent data."));
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::critical(this,
        tr("Full server reset?"),
        tr("This will delete player accounts, cells, world state, maps, custom data, record stores and the server database. requiredDataFiles.json and banlist.json will be kept. Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!mServerDialog->resetPersistentServerData(&error))
    {
        QMessageBox::critical(this, tr("Full server reset"),
            tr("Server data reset failed: %1").arg(error));
        return;
    }
    QMessageBox::information(this, tr("Full server reset"), tr("Server gameplay data was fully reset."));
}

void Launcher::MainDialog::autoStartServerChanged(bool enabled)
{
    mLauncherSettings.remove(QStringLiteral("General/Server/autoStart"));
    mLauncherSettings.setValue(QStringLiteral("General/Server/autoStart"),
        enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

void Launcher::MainDialog::autoRestartServerChanged(bool enabled)
{
    if (mPlayPage != nullptr && mPlayPage->autoRestartServer() != enabled)
        mPlayPage->setAutoRestartServer(enabled);
    if (mServerDialog != nullptr && mServerDialog->autoRestartEnabled() != enabled)
        mServerDialog->setAutoRestartEnabled(enabled);

    mLauncherSettings.remove(QStringLiteral("General/Server/autoRestart"));
    mLauncherSettings.setValue(QStringLiteral("General/Server/autoRestart"),
        enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

void Launcher::MainDialog::vanillaServerCompatibilityChanged(bool enabled)
{
    mLauncherSettings.remove(QStringLiteral("General/Server/vanillaBuild"));
    mLauncherSettings.setValue(QStringLiteral("General/Server/vanillaBuild"),
        enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

void Launcher::MainDialog::hideChatHistoryChanged(bool enabled)
{
    mLauncherSettings.remove(QStringLiteral("General/Chat/hideHistory"));
    mLauncherSettings.setValue(QStringLiteral("General/Chat/hideHistory"),
        enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

void Launcher::MainDialog::serverRunningChanged(bool running, const QString& address, const QString& port)
{
    if (mPlayPage != nullptr)
    {
        mPlayPage->setServerRunning(running, address, port);
    }

    if (versionLabel != nullptr)
    {
        if (running)
        {
            versionLabel->setText(tr("Online server - %1:%2").arg(address, port));
            versionLabel->setStyleSheet(QStringLiteral("color: #188a3b; font-weight: 600;"));
        }
        else
        {
            versionLabel->setText(tr("Server stopped"));
            versionLabel->setStyleSheet(QStringLiteral("color: #777777; font-weight: 600;"));
        }
    }
}

void Launcher::MainDialog::help()
{
    Misc::HelpViewer::openHelp("reference/index.html");
}
