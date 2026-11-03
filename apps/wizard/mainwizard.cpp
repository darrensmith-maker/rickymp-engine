#include "mainwizard.hpp"

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QSaveFile>

#include <QTime>
#include <QCloseEvent>
#include <QMessageBox>
#include <QTextCodec>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include <components/config/buildmanifest.hpp>
#include <components/config/contentorder.hpp>
#include <QStringList>
#include <algorithm>

#include "intropage.hpp"
#include "methodselectionpage.hpp"
#include "languageselectionpage.hpp"
#include "existinginstallationpage.hpp"
#include "installationtargetpage.hpp"
#include "componentselectionpage.hpp"
#include "importpage.hpp"
#include "conclusionpage.hpp"

#ifdef OPENMW_USE_UNSHIELD
#include "installationpage.hpp"
#endif

using namespace Process;

namespace
{
    QString findFileCaseInsensitive(const QDir& dir, const QString& requestedName)
    {
        const QStringList files = dir.entryList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QString& file : files)
        {
            if (file.compare(requestedName, Qt::CaseInsensitive) == 0)
                return file;
        }
        return QString();
    }

    bool containsCaseInsensitive(const QStringList& values, const QString& value)
    {
        for (const QString& entry : values)
        {
            if (entry.compare(value, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    }

    bool isPriorityContent(const QString& fileName)
    {
        return Config::isCanonicalContentFile(fileName);
    }

    bool isBaseArchive(const QString& fileName)
    {
        return fileName.compare(QStringLiteral("Morrowind.bsa"), Qt::CaseInsensitive) == 0
            || fileName.compare(QStringLiteral("Tribunal.bsa"), Qt::CaseInsensitive) == 0
            || fileName.compare(QStringLiteral("Bloodmoon.bsa"), Qt::CaseInsensitive) == 0;
    }

    bool isGroundcoverCandidate(const QString& fileName)
    {
        const QString lowered = fileName.toLower();
        return lowered.contains(QLatin1String("groundcover")) || lowered.contains(QLatin1String("grass"));
    }

    bool isContentFile(const QString& fileName)
    {
        return fileName.endsWith(QLatin1String(".esm"), Qt::CaseInsensitive)
            || fileName.endsWith(QLatin1String(".esp"), Qt::CaseInsensitive)
            || fileName.endsWith(QLatin1String(".omwgame"), Qt::CaseInsensitive)
            || fileName.endsWith(QLatin1String(".omwaddon"), Qt::CaseInsensitive);
    }

    bool containsWizardData(const QDir& dir)
    {
        if (!dir.exists())
            return false;

        const QStringList files = dir.entryList(
            QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QString& fileName : files)
        {
            if (isContentFile(fileName)
                || fileName.endsWith(QLatin1String(".bsa"), Qt::CaseInsensitive))
                return true;
        }
        return false;
    }

    QString resolveWizardDataPath(const QString& selectedPath)
    {
        if (selectedPath.trimmed().isEmpty())
            return QString();

        const QString cleanPath = QDir::cleanPath(selectedPath);
        const QDir selectedDir(cleanPath);
        if (!selectedDir.exists() || containsWizardData(selectedDir))
            return cleanPath;

        const QStringList childDirectories = selectedDir.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QString& childName : childDirectories)
        {
            if (childName.compare(QLatin1String("Data Files"), Qt::CaseInsensitive) != 0)
                continue;

            const QString childPath = QDir::cleanPath(selectedDir.filePath(childName));
            if (containsWizardData(QDir(childPath)))
                return childPath;
        }
        return cleanPath;
    }

    QString encodingForLanguage(const QString& language)
    {
        const QString canonical = Config::BuildManifest::canonicalLanguage(language);
        if (canonical == QLatin1String("Polish"))
            return QStringLiteral("win1250");
        if (canonical == QLatin1String("Russian"))
            return QStringLiteral("win1251");
        return QStringLiteral("win1252");
    }

    QString iniValue(const QStringList& lines, const QString& section, const QString& key)
    {
        QString currentSection;
        for (const QString& rawLine : lines)
        {
            const QString line = rawLine.trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
            {
                currentSection = line.mid(1, line.size() - 2).trimmed();
                continue;
            }
            if (currentSection.compare(section, Qt::CaseInsensitive) != 0
                || line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;
            if (line.left(equals).trimmed().compare(key, Qt::CaseInsensitive) == 0)
                return line.mid(equals + 1).trimmed();
        }
        return QString();
    }

    void setIniValue(QStringList& lines, const QString& section, const QString& key, const QString& value)
    {
        int sectionStart = -1;
        int sectionEnd = lines.size();
        QString currentSection;
        for (int i = 0; i < lines.size(); ++i)
        {
            const QString line = lines.at(i).trimmed();
            if (!line.startsWith(QLatin1Char('[')) || !line.endsWith(QLatin1Char(']')))
                continue;

            currentSection = line.mid(1, line.size() - 2).trimmed();
            if (sectionStart >= 0)
            {
                sectionEnd = i;
                break;
            }
            if (currentSection.compare(section, Qt::CaseInsensitive) == 0)
                sectionStart = i;
        }

        if (sectionStart < 0)
        {
            if (!lines.isEmpty() && !lines.last().isEmpty())
                lines.append(QString());
            lines.append(QStringLiteral("[%1]").arg(section));
            lines.append(key + QStringLiteral(" = ") + value);
            return;
        }

        int firstMatch = -1;
        for (int i = sectionStart + 1; i < sectionEnd; )
        {
            const QString line = lines.at(i).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            {
                ++i;
                continue;
            }
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals >= 0 && line.left(equals).trimmed().compare(key, Qt::CaseInsensitive) == 0)
            {
                if (firstMatch < 0)
                {
                    firstMatch = i;
                    lines[i] = key + QStringLiteral(" = ") + value;
                    ++i;
                }
                else
                {
                    lines.removeAt(i);
                    --sectionEnd;
                }
            }
            else
                ++i;
        }

        if (firstMatch < 0)
            lines.insert(sectionEnd, key + QStringLiteral(" = ") + value);
    }
}

Wizard::MainWizard::MainWizard(QWidget *parent) :
    QWizard(parent),
    mInstallations(),
    mError(false),
    mGameSettings(mCfgMgr),
    mBuildManifestLoaded(false),
    mBuildName(QStringLiteral("ArenaMP")),
    mBuildServerAddress(QStringLiteral("127.0.0.1")),
    mBuildServerPort(QStringLiteral("25565")),
    mBuildLanguage(QStringLiteral("English")),
    mBuildLanguageLocked(false)
{
#ifndef Q_OS_MAC
    setWizardStyle(QWizard::ModernStyle);
#else
    setWizardStyle(QWizard::ClassicStyle);
#endif

    setWindowTitle(tr("OpenMW Wizard"));
    setWindowIcon(QIcon(QLatin1String(":/images/openmw-wizard.png")));
    setMinimumWidth(550);

    // Set the property for comboboxes to the text instead of index
    setDefaultProperty("QComboBox", "currentText", "currentIndexChanged");

    setDefaultProperty("ComponentListWidget", "mCheckedItems", "checkedItemsChanged");

    mImporterInvoker = new ProcessInvoker();

    connect(mImporterInvoker->getProcess(), SIGNAL(started()),
            this, SLOT(importerStarted()));

    connect(mImporterInvoker->getProcess(), SIGNAL(finished(int,QProcess::ExitStatus)),
            this, SLOT(importerFinished(int,QProcess::ExitStatus)));

    mLogError = tr("<html><head/><body><p><b>Could not open %1 for writing</b></p> \
                   <p>Please make sure you have the right permissions \
                   and try again.</p></body></html>");

    setupLog();
    setupGameSettings();
    setupLauncherSettings();
    setupInstallations();
    setupPages();

    const boost::filesystem::path& installationPath = mCfgMgr.getInstallPath();
    if (!installationPath.empty())
    {
        const boost::filesystem::path& dataPath = installationPath / "Data Files";
        addInstallation(toQString(dataPath), false);
    }


}

Wizard::MainWizard::~MainWizard()
{
    delete mImporterInvoker;
}

void Wizard::MainWizard::setupLog()
{
    QDir logDir(toQString(mCfgMgr.getLogPath()));
    if (!logDir.exists())
        logDir.mkpath(QStringLiteral("."));

    const QString logPath = logDir.filePath(QLatin1String("wizard.log"));

    QFile file(logPath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error opening Wizard log file"));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(mLogError.arg(file.fileName()));
        msgBox.exec();
        return qApp->quit();
    }

    addLogText(QString("Started OpenMW Wizard on %1").arg(QDateTime::currentDateTime().toString()));

    qDebug() << logPath;
}

void Wizard::MainWizard::addLogText(const QString &text)
{
    QDir logDir(toQString(mCfgMgr.getLogPath()));
    const QString logPath = logDir.filePath(QLatin1String("wizard.log"));

    QFile file(logPath);

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error opening Wizard log file"));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(mLogError.arg(file.fileName()));
        msgBox.exec();
        return qApp->quit();
    }

    if (!file.isSequential())
        file.seek(file.size());

    QTextStream out(&file);

    if (!text.isEmpty())
    {
        out << text << "\n";
        out.flush();
    }
}

void Wizard::MainWizard::setupGameSettings()
{
    mGameSettings.clear();

    QDir userDir(toQString(mCfgMgr.getUserConfigPath()));
    QDir localDir(toQString(mCfgMgr.getLocalPath()));
    QDir globalDir(toQString(mCfgMgr.getGlobalPath()));
    QString message(tr("<html><head/><body><p><b>Could not open %1 for reading</b></p> \
                    <p>Please make sure you have the right permissions \
                    and try again.</p></body></html>"));

    // Load the user config file first, separately so writeSettings() only
    // writes values the user actually changed. Use QDir::filePath() here:
    // getUserConfigPath() is intentionally the local userdata directory and
    // does not have to end with a slash.
    QString path = userDir.filePath(QLatin1String("openmw.cfg"));
    QFile file(path);

    qDebug() << "Loading config file:" << path.toUtf8().constData();

    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox msgBox;
            msgBox.setWindowTitle(tr("Error opening OpenMW configuration file"));
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setText(message.arg(file.fileName()));
            msgBox.exec();
            return qApp->quit();
        }
        QTextStream stream(&file);
        stream.setCodec(QTextCodec::codecForName("UTF-8"));

        mGameSettings.readUserFile(stream);
    }

    file.close();

    // Load from lowest to highest priority. The portable userdata config must
    // win over local and global defaults, because the launcher/wizard write
    // the selected data/resource paths there.
    QStringList paths;
    paths.append(globalDir.filePath(QLatin1String("openmw.cfg")));
    paths.append(localDir.filePath(QLatin1String("openmw.cfg")));
    paths.append(userDir.filePath(QLatin1String("openmw.cfg")));

    for (const QString &path2 : paths)
    {
        qDebug() << "Loading config file:" << path2.toUtf8().constData();

        file.setFileName(path2);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QMessageBox msgBox;
                msgBox.setWindowTitle(tr("Error opening OpenMW configuration file"));
                msgBox.setIcon(QMessageBox::Critical);
                msgBox.setStandardButtons(QMessageBox::Ok);
                msgBox.setText(message.arg(file.fileName()));

                return qApp->quit();
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mGameSettings.readFile(stream);
        }
        file.close();
    }
}

void Wizard::MainWizard::setupLauncherSettings()
{
    mLauncherSettings.clear();
    mLauncherSettings.setMultiValueEnabled(true);

    QDir localDir(toQString(mCfgMgr.getLocalPath()));

    QString message(tr("<html><head/><body><p><b>Could not open %1 for reading</b></p> \
                    <p>Please make sure you have the right permissions \
                    and try again.</p></body></html>"));

    // A Wizard run is an explicit reset. Start from packaged Launcher defaults
    // and deliberately ignore userdata/launcher.cfg; writeSettings() replaces it
    // with the choices made during this Wizard run.
    QStringList paths;
    paths.append(localDir.filePath(QLatin1String(Config::LauncherSettings::sLauncherConfigFileName)));

    for (const QString& path : paths)
    {
        QFile file(path);

        qDebug() << "Loading config file:" << path.toUtf8().constData();

        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QMessageBox msgBox;
                msgBox.setWindowTitle(tr("Error opening OpenMW configuration file"));
                msgBox.setIcon(QMessageBox::Critical);
                msgBox.setStandardButtons(QMessageBox::Ok);
                msgBox.setText(message.arg(file.fileName()));
                msgBox.exec();
                return qApp->quit();
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mLauncherSettings.readFile(stream);
        }

        file.close();
    }
}

void Wizard::MainWizard::setupInstallations()
{
    // Treat an existing data directory as a selectable installation when it
    // contains at least one ESM or BSA. Morrowind.bsa is no longer mandatory.
    for (const QString& path : mGameSettings.getDataDirs())
    {
        const QString resolvedPath = resolveWizardDataPath(path);
        QDir dir(resolvedPath);
        if (!dir.exists())
            continue;

        bool hasDataFiles = false;
        const QStringList files = dir.entryList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QString& fileName : files)
        {
            if (fileName.endsWith(QLatin1String(".esm"), Qt::CaseInsensitive)
                || fileName.endsWith(QLatin1String(".bsa"), Qt::CaseInsensitive))
            {
                hasDataFiles = true;
                break;
            }
        }

        if (hasDataFiles)
            addInstallation(resolvedPath, false);
    }
}

void Wizard::MainWizard::runSettingsImporter()
{
    writeSettings();

    QString path(field(QLatin1String("installation.path")).toString());

    const QString iniPath = mInstallations.value(QDir::toNativeSeparators(path)).iniPath;
    if (field(QLatin1String("installation.retailDisc")).toBool() == false
        && (iniPath.isEmpty() || !QFileInfo::exists(iniPath)))
    {
        addLogText(tr("No Morrowind.ini was found. Data Files, base masters and BSA archives were configured directly; INI import was skipped."));
        return;
    }

    QDir userDir(toQString(mCfgMgr.getUserConfigPath()));
    const QString userCfgPath = userDir.filePath(QLatin1String("openmw.cfg"));

    // Construct the arguments to run the importer
    QStringList arguments;

    // Import plugin selection?
    if (field(QLatin1String("installation.retailDisc")).toBool() == true
            || field(QLatin1String("installation.import-addons")).toBool() == true)
        arguments.append(QLatin1String("--game-files"));

    arguments.append(QLatin1String("--encoding"));

    // Set encoding
    const QString language = mBuildLanguageLocked
        ? mBuildLanguage
        : Config::BuildManifest::canonicalLanguage(field(QLatin1String("installation.language")).toString());
    arguments.append(encodingForLanguage(language));

    // Now the paths
    arguments.append(QLatin1String("--ini"));

    if (field(QLatin1String("installation.retailDisc")).toBool() == true) {
        arguments.append(path + QDir::separator() + QLatin1String("Morrowind.ini"));
    } else {
        arguments.append(iniPath);
    }

    arguments.append(QLatin1String("--cfg"));
    arguments.append(userCfgPath);

    if (!mImporterInvoker->startProcess(QLatin1String("openmw-iniimporter"), arguments, false))
        return qApp->quit();
}

void Wizard::MainWizard::addInstallation(const QString &path, bool configureSelected)
{
    const QString cleanPath = resolveWizardDataPath(path);
    qDebug() << "add Data Files folder:" << cleanPath;

    Installation install;
    install.hasMorrowind = findFiles(QLatin1String("Morrowind"), cleanPath);
    install.hasTribunal = findFiles(QLatin1String("Tribunal"), cleanPath);
    install.hasBloodmoon = findFiles(QLatin1String("Bloodmoon"), cleanPath);

    // Morrowind.ini is optional. If present, it can still be used for importing
    // language and legacy settings, but selecting the Data Files folder does not
    // depend on it.
    QDir dir(cleanPath);
    QFile file(dir.filePath(QLatin1String("Morrowind.ini")));
    if (!file.exists())
    {
        QDir parentDir(cleanPath);
        parentDir.cdUp();
        file.setFileName(parentDir.filePath(QLatin1String("Morrowind.ini")));
    }
    if (file.exists())
        install.iniPath = file.fileName();

    const QString nativePath = QDir::toNativeSeparators(cleanPath);
    mInstallations.insert(nativePath, install);

    if (!mGameSettings.getDataDirs().contains(cleanPath))
    {
        mGameSettings.setMultiValue(QLatin1String("data"), cleanPath);
        mGameSettings.addDataDir(cleanPath);
    }

    if (configureSelected)
        configureDataFiles(cleanPath);
}

void Wizard::MainWizard::readClientEndpoint(QString& address, QString& port) const
{
    address = QStringLiteral("127.0.0.1");
    port = QStringLiteral("25565");

    const QDir userDir(toQString(mCfgMgr.getUserConfigPath()));
    QFile file(userDir.filePath(QStringLiteral("tes3mp-client-default.cfg")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    while (!stream.atEnd())
    {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1String("destinationAddress")))
            address = line.section(QLatin1Char('='), 1).trimmed();
        else if (line.startsWith(QLatin1String("port")) && !line.startsWith(QLatin1String("password")))
            port = line.section(QLatin1Char('='), 1).trimmed();
    }
}

void Wizard::MainWizard::writeClientEndpoint(const QString& address, const QString& port) const
{
    QDir userDir(toQString(mCfgMgr.getUserConfigPath()));
    if (!userDir.exists())
        userDir.mkpath(QStringLiteral("."));

    QFile file(userDir.filePath(QStringLiteral("tes3mp-client-default.cfg")));
    QStringList lines;
    bool foundAddress = false;
    bool foundPort = false;

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream input(&file);
        input.setCodec("UTF-8");
        while (!input.atEnd())
            lines.append(input.readLine());
        file.close();
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

    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QTextStream output(&file);
        output.setCodec("UTF-8");
        for (const QString& line : lines)
            output << line << '\n';
    }
}

bool Wizard::MainWizard::loadBuildManifest(const QString& dataFilesPath)
{
    mBuildManifestLoaded = false;
    mBuildManifestPath.clear();
    mBuildName = QStringLiteral("ArenaMP");
    mBuildDataPath = QDir::cleanPath(dataFilesPath);
    mBuildLanguage = QStringLiteral("English");
    mBuildLanguageLocked = false;
    readClientEndpoint(mBuildServerAddress, mBuildServerPort);

    const QString manifestPath = Config::BuildManifest::findForDataDir(dataFilesPath);
    if (manifestPath.isEmpty())
        return false;

    Config::BuildManifest manifest;
    QString error;
    if (!manifest.read(manifestPath, &error))
    {
        addLogText(tr("Could not read %1: %2").arg(manifestPath, error));
        return false;
    }

    const QString resolvedDataPath = manifest.resolvedDataPath(manifestPath);
    if (QFileInfo(resolvedDataPath).isDir())
        mBuildDataPath = QDir::cleanPath(resolvedDataPath);

    if (!mGameSettings.getDataDirs().contains(mBuildDataPath))
    {
        mGameSettings.setMultiValue(QStringLiteral("data"), mBuildDataPath);
        mGameSettings.addDataDir(mBuildDataPath);
    }

    mBuildManifestLoaded = true;
    mBuildManifestPath = manifestPath;
    mBuildName = manifest.buildName.trimmed().isEmpty() ? QStringLiteral("ArenaMP") : manifest.buildName.trimmed();
    mBuildServerAddress = manifest.serverAddress.trimmed().isEmpty()
        ? QStringLiteral("127.0.0.1") : manifest.serverAddress.trimmed();
    mBuildServerPort = manifest.serverPort.trimmed().isEmpty()
        ? QStringLiteral("25565") : manifest.serverPort.trimmed();

    if (manifest.languageSpecified)
    {
        mBuildLanguage = Config::BuildManifest::canonicalLanguage(manifest.language);
        mBuildLanguageLocked = true;
        setField(QStringLiteral("installation.language"), mBuildLanguage);
        mLauncherSettings.remove(QStringLiteral("Settings/language"));
        mLauncherSettings.setValue(QStringLiteral("Settings/language"), mBuildLanguage);
        mGameSettings.setValue(QStringLiteral("encoding"), encodingForLanguage(mBuildLanguage));
    }

    mLauncherSettings.setValue(QStringLiteral("General/Build/name"), mBuildName);
    mLauncherSettings.setValue(QStringLiteral("General/Server/vanillaBuild"),
        manifest.vanillaServerCompatibility ? QStringLiteral("true") : QStringLiteral("false"));
    writeClientEndpoint(mBuildServerAddress, mBuildServerPort);

    // The mere presence of build.ini is authoritative, including an
    // intentionally empty plug-in list. Never fall back to scanning every ESP.
    const QStringList orderedContent = manifest.contentFiles;
    mGameSettings.setContentList(orderedContent);
    mGameSettings.setGroundcoverList(manifest.groundcoverFiles);
    mGameSettings.remove(QStringLiteral("fallback-archive"));
    for (const QString& archive : manifest.archives)
        mGameSettings.setMultiValue(QStringLiteral("fallback-archive"), archive);

    QStringList profileFiles = orderedContent;
    profileFiles.append(manifest.groundcoverFiles);
    mLauncherSettings.setContentList(mBuildName, profileFiles, manifest.groundcoverFiles,
        !manifest.groundcoverFiles.isEmpty());
    mLauncherSettings.setCurrentContentListName(mBuildName);

    addLogText(tr("Loaded build manifest: %1").arg(manifestPath));
    return true;
}

bool Wizard::MainWizard::writeBuildManifest(const QString& dataFilesPath)
{
    QString dataPath = mBuildDataPath;
    if (dataPath.isEmpty() || !QFileInfo(dataPath).isDir())
        dataPath = QDir::cleanPath(dataFilesPath);
    if (dataPath.isEmpty() || !QFileInfo(dataPath).isDir())
        return false;

    // An existing build.ini is user-owned. The Wizard may rebuild launcher.cfg
    // and openmw.cfg, but it must never rewrite the manifest's language, plug-in
    // list, order, comments, or formatting. Only create build.ini for a new
    // installation where no manifest exists in any supported location.
    const QString existingManifestPath = Config::BuildManifest::findForDataDir(dataPath);
    if (!existingManifestPath.isEmpty())
    {
        mBuildManifestLoaded = true;
        mBuildManifestPath = existingManifestPath;
        mBuildDataPath = dataPath;
        addLogText(tr("Preserved existing build manifest without changes: %1")
            .arg(existingManifestPath));
        return true;
    }

    // No manifest exists yet, so create the canonical one beside Data Files.
    const QString sourceManifestPath = mBuildManifestPath;
    const QString manifestPath = Config::BuildManifest::canonicalPathForDataDir(dataPath);

    // Recreate launcher/openmw settings, but preserve authoritative values
    // from an existing manifest unless the Wizard is creating the first one.
    Config::BuildManifest previousManifest;
    QString previousManifestPath = manifestPath;
    if (!sourceManifestPath.isEmpty() && QFileInfo::exists(sourceManifestPath))
        previousManifestPath = sourceManifestPath;
    const bool previousManifestLoaded = QFileInfo::exists(previousManifestPath)
        && previousManifest.read(previousManifestPath);
    Config::BuildManifest manifest;

    QString address = mBuildServerAddress;
    QString port = mBuildServerPort;
    if (address.isEmpty() || port.isEmpty())
        readClientEndpoint(address, port);

    manifest.formatVersion = 1;
    manifest.buildName = mBuildName.trimmed().isEmpty() ? QStringLiteral("ArenaMP") : mBuildName.trimmed();
    manifest.dataPath = Config::BuildManifest::portableDataPath(manifestPath, dataPath);
    if (previousManifestLoaded && previousManifest.languageSpecified)
    {
        mBuildLanguage = Config::BuildManifest::canonicalLanguage(previousManifest.language);
        mBuildLanguageLocked = true;
    }
    manifest.language = mBuildLanguageLocked
        ? mBuildLanguage
        : Config::BuildManifest::canonicalLanguage(
            field(QStringLiteral("installation.language")).toString().trimmed());
    if (manifest.language.isEmpty())
        manifest.language = Config::BuildManifest::canonicalLanguage(
            mLauncherSettings.value(QStringLiteral("Settings/language"), QStringLiteral("English")));
    manifest.languageSpecified = true;
    manifest.serverAddress = address.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : address.trimmed();
    manifest.serverAddressSpecified = true;
    manifest.serverPort = port.trimmed().isEmpty() ? QStringLiteral("25565") : port.trimmed();
    manifest.serverPortSpecified = true;
    manifest.contentFiles = mGameSettings.getContentList();
    manifest.groundcoverFiles = mGameSettings.getGroundcoverList();
    manifest.archives = Config::LauncherSettings::reverse(
        mGameSettings.values(QStringLiteral("fallback-archive")));

    QString error;
    if (!manifest.write(manifestPath, &error))
    {
        addLogText(tr("Could not write %1: %2").arg(manifestPath, error));
        QMessageBox::warning(this, tr("Could not create build.ini"),
            tr("The game configuration was saved, but %1 could not be written.\n\n%2")
                .arg(manifestPath, error));
        return false;
    }

    mBuildManifestLoaded = true;
    mBuildManifestPath = manifestPath;
    mBuildDataPath = dataPath;
    mBuildServerAddress = manifest.serverAddress;
    mBuildServerPort = manifest.serverPort;
    mLauncherSettings.setValue(QStringLiteral("General/Build/name"), manifest.buildName);
    addLogText(tr("Saved build manifest: %1").arg(manifestPath));
    return true;
}

void Wizard::MainWizard::configureDataFiles(const QString& path)
{
    const QString resolvedPath = resolveWizardDataPath(path);
    const QDir dir(resolvedPath);
    if (!dir.exists())
        return;

    // An existing portable build.ini is the single source of truth. Reload it
    // on every Wizard pass so resetting launcher.cfg recreates exactly the
    // saved language and plug-in order instead of enabling every ESP found in
    // Data Files.
    if (!Config::BuildManifest::findForDataDir(resolvedPath).isEmpty())
    {
        if (loadBuildManifest(resolvedPath))
        {
            addLogText(tr("Applied the exact language and plug-in order from build.ini."));
            return;
        }

        // A present but unreadable manifest must not trigger an all-plugin
        // fallback. Leave the content list empty and report the problem.
        mGameSettings.setContentList(QStringList());
        mGameSettings.setGroundcoverList(QStringList());
        addLogText(tr("build.ini exists but could not be loaded; no additional plug-ins were enabled."));
        return;
    }

    mBuildManifestLoaded = false;
    mBuildManifestPath.clear();
    mBuildDataPath = resolvedPath;
    mBuildName = QStringLiteral("ArenaMP");
    mBuildLanguageLocked = false;

    // No build.ini exists yet: apply only the known recommended order. Do not
    // auto-enable unrelated ESP/ESM files merely because they are installed.
    QStringList content;
    const QStringList& baseMasters = Config::canonicalContentOrder();

    for (const QString& requested : baseMasters)
    {
        const QString actual = findFileCaseInsensitive(dir, requested);
        if (!actual.isEmpty())
            content.append(actual);
    }

    QStringList groundcover;

    mGameSettings.setContentList(content);
    mGameSettings.setGroundcoverList(groundcover);

    // Base BSAs are loaded first in game dependency order. Every additional BSA
    // in the selected Data Files folder follows in case-insensitive alphabetical
    // order.
    QStringList allArchives;
    const QStringList directoryFiles = dir.entryList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QString& fileName : directoryFiles)
    {
        if (fileName.endsWith(QLatin1String(".bsa"), Qt::CaseInsensitive))
            allArchives.append(fileName);
    }

    QStringList orderedArchives;
    const QStringList baseArchives = {
        QStringLiteral("Morrowind.bsa"),
        QStringLiteral("Tribunal.bsa"),
        QStringLiteral("Bloodmoon.bsa")
    };
    for (const QString& requested : baseArchives)
    {
        for (const QString& actual : allArchives)
        {
            if (actual.compare(requested, Qt::CaseInsensitive) == 0)
            {
                orderedArchives.append(actual);
                break;
            }
        }
    }

    QStringList additionalArchives;
    for (const QString& archive : allArchives)
    {
        if (!isBaseArchive(archive))
            additionalArchives.append(archive);
    }
    std::sort(additionalArchives.begin(), additionalArchives.end(), [](const QString& left, const QString& right) {
        return QString::localeAwareCompare(left.toLower(), right.toLower()) < 0;
    });
    orderedArchives.append(additionalArchives);

    mGameSettings.remove(QLatin1String("fallback-archive"));
    for (const QString& archive : orderedArchives)
        mGameSettings.setMultiValue(QLatin1String("fallback-archive"), archive);

    QStringList profileFiles = content;
    profileFiles.append(groundcover);
    mLauncherSettings.setContentList(mBuildName, profileFiles, groundcover, !groundcover.isEmpty());
    mLauncherSettings.setCurrentContentListName(mBuildName);
}

void Wizard::MainWizard::setupPages()
{
    setPage(Page_Intro, new IntroPage(this));
    setPage(Page_MethodSelection, new MethodSelectionPage(this));
    setPage(Page_LanguageSelection, new LanguageSelectionPage(this));
    setPage(Page_ExistingInstallation, new ExistingInstallationPage(this));
    setPage(Page_InstallationTarget, new InstallationTargetPage(this, mCfgMgr));
    setPage(Page_ComponentSelection, new ComponentSelectionPage(this));
#ifdef OPENMW_USE_UNSHIELD
    setPage(Page_Installation, new InstallationPage(this));
#endif
    setPage(Page_Import, new ImportPage(this));
    setPage(Page_Conclusion, new ConclusionPage(this));
    setStartId(Page_Intro);

}

void Wizard::MainWizard::importerStarted()
{
}

void Wizard::MainWizard::importerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const QString path = field(QStringLiteral("installation.path")).toString();
    const QString manifestPath = Config::BuildManifest::findForDataDir(path);
    const bool importerSucceeded = exitCode == 0 && exitStatus != QProcess::CrashExit;

    // The importer can leave a partially rewritten openmw.cfg. Reapply the
    // fresh Wizard-selected content/archive order before saving the new build.ini.
    if (!importerSucceeded && manifestPath.isEmpty())
        return;

    setupGameSettings();
    if (!path.isEmpty())
        configureDataFiles(path);
    writeSettings();

    if (importerSucceeded)
        addLogText(tr("Reapplied the new Wizard content, groundcover and archive order after Morrowind.ini import."));
    else
        addLogText(tr("The Morrowind.ini importer failed, but the new Wizard file order was restored before saving."));
}

void Wizard::MainWizard::accept()
{
    writeSettings();
    QWizard::accept();
}

void Wizard::MainWizard::reject()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle(tr("Quit Wizard"));
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setText(tr("Are you sure you want to exit the Wizard?"));

    if (msgBox.exec() == QMessageBox::Yes) {
        QWizard::reject();
    }
}

void Wizard::MainWizard::initializeNativeDisplaySettings()
{
    // Versioned marker: v53 could mark the old key as initialized merely
    // because a packaged 1280x720 settings.cfg existed. The wizard must still
    // perform one reliable native-display initialization for that profile.
    const QString markerV2 = QStringLiteral("General/Graphics/nativeDisplayInitializedV2");
    const bool initialized = mLauncherSettings.value(markerV2, QStringLiteral("false"))
        .compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    if (initialized)
        return;

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    const qreal pixelRatio = std::max<qreal>(1.0, screen->devicePixelRatio());
    const int width = qRound(screen->geometry().width() * pixelRatio);
    const int height = qRound(screen->geometry().height() * pixelRatio);
    if (width <= 0 || height <= 0)
        return;

    const QDir userDir(toQString(mCfgMgr.getUserConfigPath()));
    const QString settingsPath = userDir.filePath(QStringLiteral("settings.cfg"));
    QStringList lines;
    QFile input(settingsPath);
    if (input.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream stream(&input);
        stream.setCodec("UTF-8");
        while (!stream.atEnd())
            lines.append(stream.readLine());
    }

    // Preserve a clearly customized display profile. Packaged/default values
    // (1280x720, windowed, scale 1) are treated as uninitialized.
    bool okWidth = false;
    bool okHeight = false;
    bool okScale = false;
    const int currentWidth = iniValue(lines, QStringLiteral("Video"), QStringLiteral("resolution x")).toInt(&okWidth);
    const int currentHeight = iniValue(lines, QStringLiteral("Video"), QStringLiteral("resolution y")).toInt(&okHeight);
    const float currentScale = iniValue(lines, QStringLiteral("GUI"), QStringLiteral("scaling factor")).toFloat(&okScale);
    const QString currentFullscreen = iniValue(lines, QStringLiteral("Video"), QStringLiteral("fullscreen"));
    const bool customized = (okWidth && okHeight && (currentWidth != 1280 || currentHeight != 720))
        || (okScale && qAbs(currentScale - 1.f) > 0.001f)
        || currentFullscreen.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;

    if (!customized)
    {
        float scalingFactor = static_cast<float>(std::max(width, height)) / 1280.f;
        scalingFactor = std::max(0.5f, std::min(4.0f, scalingFactor));
        scalingFactor = qRound(scalingFactor * 100.f) / 100.f;

        setIniValue(lines, QStringLiteral("Video"), QStringLiteral("resolution x"), QString::number(width));
        setIniValue(lines, QStringLiteral("Video"), QStringLiteral("resolution y"), QString::number(height));
        setIniValue(lines, QStringLiteral("Video"), QStringLiteral("screen"), QStringLiteral("0"));
        setIniValue(lines, QStringLiteral("Video"), QStringLiteral("fullscreen"), QStringLiteral("true"));
        setIniValue(lines, QStringLiteral("Video"), QStringLiteral("window border"), QStringLiteral("false"));
        setIniValue(lines, QStringLiteral("GUI"), QStringLiteral("scaling factor"), QString::number(scalingFactor, 'f', 2));

        QSaveFile output(settingsPath);
        if (output.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream stream(&output);
            stream.setCodec("UTF-8");
            for (const QString& line : lines)
                stream << line << '\n';
            if (!output.commit())
                addLogText(tr("Could not save native display settings to %1").arg(settingsPath));
        }
        else
            addLogText(tr("Could not open %1 for native display initialization").arg(settingsPath));
    }

    // Keep v54 launcher behavior compatible and remember the corrected pass.
    mLauncherSettings.setValue(QStringLiteral("General/Graphics/nativeDisplayInitialized"), QStringLiteral("true"));
    mLauncherSettings.setValue(markerV2, QStringLiteral("true"));
}

void Wizard::MainWizard::writeSettings()
{
    // Resolve and load the selected installation first. This is intentionally
    // done before reading the language field: loading build.ini may lock a
    // non-English language and an exact content list that the Wizard must use.
    QString path = resolveWizardDataPath(
        field(QLatin1String("installation.path")).toString());
    setField(QLatin1String("installation.path"), path);

    configureDataFiles(path);
    if (!mBuildDataPath.isEmpty() && QFileInfo(mBuildDataPath).isDir())
        path = mBuildDataPath;

    // A language explicitly stored in build.ini is authoritative. Do not let
    // the hidden language page or its default English item replace it.
    const QString language = mBuildLanguageLocked
        ? mBuildLanguage
        : Config::BuildManifest::canonicalLanguage(
            field(QLatin1String("installation.language")).toString());
    setField(QLatin1String("installation.language"), language);
    mLauncherSettings.remove(QLatin1String("Settings/language"));
    mLauncherSettings.setValue(QLatin1String("Settings/language"), language);
    mGameSettings.setValue(QLatin1String("encoding"), encodingForLanguage(language));

    // Make sure the installation path is the last data= entry.
    mGameSettings.removeDataDir(path);
    mGameSettings.addDataDir(path);
    mGameSettings.setMultiValue(QLatin1String("data"), path);

    const QString userPath(toQString(mCfgMgr.getUserConfigPath()));
    QDir dir(userPath);

    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            QMessageBox msgBox;
            msgBox.setWindowTitle(tr("Error creating OpenMW configuration directory"));
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setText(tr("<html><head/><body><p><b>Could not create %1</b></p> \
                              <p>Please make sure you have the right permissions \
                              and try again.</p></body></html>").arg(userPath));
            msgBox.exec();
            return qApp->quit();
        }
    }

    // Game settings
    QFile file(dir.filePath(QLatin1String("openmw.cfg")));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate)) {
        // File cannot be opened or created
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error writing OpenMW configuration file"));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(tr("<html><head/><body><p><b>Could not open %1 for writing</b></p> \
                          <p>Please make sure you have the right permissions \
                          and try again.</p></body></html>").arg(file.fileName()));
        msgBox.exec();
        return qApp->quit();
    }

    QTextStream stream(&file);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));

    mGameSettings.writeFile(stream);
    file.close();

    mLauncherSettings.setValue(QStringLiteral("General/Build/name"),
        mBuildName.trimmed().isEmpty() ? QStringLiteral("ArenaMP") : mBuildName.trimmed());
    // A completed Wizard run is authoritative. Without this flag the next
    // Launcher process can start the Wizard again despite valid configuration.
    mLauncherSettings.remove(QStringLiteral("General/firstrun"));
    mLauncherSettings.setValue(QStringLiteral("General/firstrun"), QStringLiteral("false"));
    writeBuildManifest(path);
    initializeNativeDisplaySettings();

    // Request one and only one automatic hardware quality pass in the
    // Launcher immediately after this Wizard run. Later Launcher starts never
    // reapply a preset unless the user presses Apply preset.
    const QString initialPresetPendingKey
        = QStringLiteral("General/Graphics/initialQualityPresetPending");
    mLauncherSettings.remove(initialPresetPendingKey);
    mLauncherSettings.setValue(initialPresetPendingKey, QStringLiteral("true"));

    // Launcher settings
    file.setFileName(dir.filePath(QLatin1String(Config::LauncherSettings::sLauncherConfigFileName)));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate)) {
        // File cannot be opened or created
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error writing OpenMW configuration file"));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(tr("<html><head/><body><p><b>Could not open %1 for writing</b></p> \
                          <p>Please make sure you have the right permissions \
                          and try again.</p></body></html>").arg(file.fileName()));
        msgBox.exec();
        return qApp->quit();
    }

    stream.setDevice(&file);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));

    mLauncherSettings.writeFile(stream);
    file.close();
}

bool Wizard::MainWizard::findFiles(const QString &name, const QString &path)
{
    QDir dir(path);

    if (!dir.exists())
        return false;

    // A base master is detected independently from its BSA. The wizard can use
    // incomplete or custom Data Files folders and will register every BSA that
    // is actually present.
    return dir.entryList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase)
        .contains(name + QLatin1String(".esm"), Qt::CaseInsensitive);
}

QString Wizard::MainWizard::toQString(const boost::filesystem::path& path) const
{
    return QString::fromUtf8(path.string().c_str());
}
