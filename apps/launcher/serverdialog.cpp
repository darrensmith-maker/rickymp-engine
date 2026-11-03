#include "serverdialog.hpp"

#include <QAbstractSocket>
#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QTcpSocket>
#include <QVBoxLayout>

namespace
{
    bool copyFileReplacing(const QString& sourcePath, const QString& destinationPath, QString* errorMessage)
    {
        const QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists() || !sourceInfo.isFile())
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Bundled server file is missing: %1")
                    .arg(QDir::toNativeSeparators(sourcePath));
            return false;
        }

        const QFileInfo destinationInfo(destinationPath);
        if (!QDir().mkpath(destinationInfo.absolutePath()))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not create userdata directory: %1")
                    .arg(QDir::toNativeSeparators(destinationInfo.absolutePath()));
            return false;
        }

        if (destinationInfo.exists() && !QFile::remove(destinationPath))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not replace server configuration: %1")
                    .arg(QDir::toNativeSeparators(destinationPath));
            return false;
        }

        if (!QFile::copy(sourcePath, destinationPath))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not copy server configuration from %1 to %2")
                    .arg(QDir::toNativeSeparators(sourcePath), QDir::toNativeSeparators(destinationPath));
            return false;
        }

        QFile::setPermissions(destinationPath, sourceInfo.permissions());
        return true;
    }

    bool writeTextFile(const QString& path, const QString& text, QString* errorMessage)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not write server configuration: %1")
                    .arg(QDir::toNativeSeparators(path));
            return false;
        }

        const QByteArray data = text.toUtf8();
        if (file.write(data) != data.size())
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not finish writing server configuration: %1")
                    .arg(QDir::toNativeSeparators(path));
            return false;
        }
        return true;
    }

    bool setIniValue(const QString& configPath, const QString& sectionName,
        const QString& keyName, const QString& value, QString* errorMessage)
    {
        QFile configFile(configPath);
        if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (errorMessage)
                *errorMessage = QObject::tr("Could not read server configuration: %1")
                    .arg(QDir::toNativeSeparators(configPath));
            return false;
        }

        const QByteArray originalData = configFile.readAll();
        configFile.close();
        const QString lineEnding = originalData.contains("\r\n")
            ? QStringLiteral("\r\n") : QStringLiteral("\n");
        QStringList lines = QString::fromUtf8(originalData)
            .split(QRegularExpression(QStringLiteral("\r?\n")));

        int sectionLine = -1;
        int insertionLine = lines.size();
        int valueLine = -1;
        QString currentSection;

        for (int i = 0; i < lines.size(); ++i)
        {
            const QString trimmed = lines.at(i).trimmed();
            if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']')))
            {
                const QString section = trimmed.mid(1, trimmed.size() - 2).trimmed();
                if (sectionLine >= 0 && currentSection == sectionName)
                {
                    insertionLine = i;
                    break;
                }
                currentSection = section;
                if (section == sectionName)
                    sectionLine = i;
                continue;
            }

            if (currentSection != sectionName || trimmed.startsWith(QLatin1Char('#'))
                || trimmed.startsWith(QLatin1Char(';')))
                continue;

            const int equalsPosition = trimmed.indexOf(QLatin1Char('='));
            if (equalsPosition >= 0 && trimmed.left(equalsPosition).trimmed() == keyName)
            {
                valueLine = i;
                break;
            }
        }

        if (valueLine >= 0)
        {
            const QString originalLine = lines.at(valueLine);
            const int equalsPosition = originalLine.indexOf(QLatin1Char('='));
            lines[valueLine] = equalsPosition >= 0
                ? originalLine.left(equalsPosition + 1) + QLatin1Char(' ') + value
                : keyName + QStringLiteral(" = ") + value;
        }
        else if (sectionLine >= 0)
        {
            lines.insert(insertionLine, keyName + QStringLiteral(" = ") + value);
        }
        else
        {
            if (!lines.isEmpty() && !lines.last().isEmpty())
                lines.append(QString());
            lines.append(QLatin1Char('[') + sectionName + QLatin1Char(']'));
            lines.append(keyName + QStringLiteral(" = ") + value);
        }

        return writeTextFile(configPath, lines.join(lineEnding), errorMessage);
    }
    bool clearDirectoryContents(const QString& directoryPath, QString* errorMessage)
    {
        QDir dir(directoryPath);
        if (!dir.exists())
            return QDir().mkpath(directoryPath);

        const QFileInfoList entries = dir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
        for (const QFileInfo& info : entries)
        {
            if (info.fileName() == QLatin1String(".gitkeep"))
                continue;

            bool ok = true;
            if (info.isDir())
                ok = QDir(info.absoluteFilePath()).removeRecursively();
            else
                ok = QFile::remove(info.absoluteFilePath());

            if (!ok)
            {
                if (errorMessage)
                    *errorMessage = QObject::tr("Could not remove server data: %1")
                        .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
                return false;
            }
        }
        return true;
    }

}

Launcher::ServerDialog::ServerDialog(QWidget* parent)
    : QWidget(parent)
    , mAddressLabel(nullptr)
    , mPortLabel(nullptr)
    , mEncodingLabel(nullptr)
    , mEncodingCombo(nullptr)
    , mRestartCheckBox(nullptr)
    , mLogView(nullptr)
    , mStopButton(nullptr)
    , mCloseButton(nullptr)
    , mProcess(new QProcess(this))
    , mBackupProcess(new QProcess(this))
    , mRestartCounter(0)
    , mStopRequested(false)
    , mBackupInProgress(false)
    , mRestartAfterBackup(false)
    , mCrashAfterStop(false)
    , mRapidCrashCount(0)
    , mLastStartMs(0)
    , mCachedDisplayAddressAtMs(0)
{
    setWindowTitle(tr("TES3MP Server"));
    resize(860, 620);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QHBoxLayout* infoLayout = new QHBoxLayout();
    mAddressLabel = new QLabel(this);
    mPortLabel = new QLabel(this);
    infoLayout->addWidget(mAddressLabel, 1);
    infoLayout->addWidget(mPortLabel, 0);
    mainLayout->addLayout(infoLayout);

    QHBoxLayout* optionsLayout = new QHBoxLayout();
    mEncodingLabel = new QLabel(tr("Log encoding:"), this);
    mEncodingCombo = new QComboBox(this);
    mEncodingCombo->addItem(tr("UTF-8"), QStringLiteral("UTF-8"));
    mEncodingCombo->addItem(tr("System"), QStringLiteral("System"));
    mEncodingCombo->addItem(tr("Windows-1251"), QStringLiteral("Windows-1251"));
    mEncodingCombo->addItem(tr("CP866"), QStringLiteral("CP866"));
    mRestartCheckBox = new QCheckBox(tr("Auto restart and backup"), this);
    mRestartCheckBox->setChecked(true);

    optionsLayout->addWidget(mEncodingLabel);
    optionsLayout->addWidget(mEncodingCombo);
    optionsLayout->addSpacing(16);
    optionsLayout->addWidget(mRestartCheckBox);
    optionsLayout->addStretch(1);
    mainLayout->addLayout(optionsLayout);

    mLogView = new QPlainTextEdit(this);
    mLogView->setReadOnly(true);
    mLogView->setLineWrapMode(QPlainTextEdit::NoWrap);
    mainLayout->addWidget(mLogView, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(this);
    mStopButton = buttons->addButton(tr("Stop Server"), QDialogButtonBox::ActionRole);
    mCloseButton = buttons->addButton(tr("Clear Log"), QDialogButtonBox::ResetRole);
    mainLayout->addWidget(buttons);

    connect(mStopButton, SIGNAL(clicked()), this, SLOT(stopServer()));
    connect(mCloseButton, SIGNAL(clicked()), mLogView, SLOT(clear()));
    connect(mEncodingCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(refreshDecodedLog()));
    connect(mRestartCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(autoRestartChanged(bool)));
    connect(mProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(processReadyReadStandardOutput()));
    connect(mProcess, SIGNAL(readyReadStandardError()), this, SLOT(processReadyReadStandardError()));
    connect(mProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(processFinished(int,QProcess::ExitStatus)));
    connect(mProcess, SIGNAL(error(QProcess::ProcessError)), this, SLOT(processError(QProcess::ProcessError)));
    connect(mBackupProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(backupProcessFinished(int,QProcess::ExitStatus)));
    connect(mBackupProcess, SIGNAL(error(QProcess::ProcessError)), this, SLOT(backupProcessError(QProcess::ProcessError)));
}

Launcher::ServerDialog::~ServerDialog()
{
}

bool Launcher::ServerDialog::startServer()
{
    if (mProcess->state() != QProcess::NotRunning)
        return true;

    mStopRequested = false;
    mCloseButton->setEnabled(true);
    mStopButton->setEnabled(true);
    if (!mRawLog.isEmpty())
        appendStatusLine(QStringLiteral("----------------------------------------"));

    QString preparationError;
    if (!preparePortableServer(&preparationError))
    {
        QMessageBox::warning(this, tr("Error preparing ArenaMP server"), preparationError);
        appendStatusLine(tr("Server preparation failed: %1").arg(preparationError));
        mStopButton->setEnabled(false);
        return false;
    }

    const ServerConfig config = readServerConfig();
    mAddressLabel->setText(tr("Connect IP: %1").arg(resolveDisplayAddress(config.localAddress)));
    mPortLabel->setText(tr("Port: %1").arg(config.port));

    appendStatusLine(tr("Starting tes3mp-server..."));
    appendStatusLine(tr("Server config: %1").arg(QDir::toNativeSeparators(config.configPath)));
    appendStatusLine(tr("Server data: %1").arg(QDir::toNativeSeparators(config.serverHomePath)));
    appendStatusLine(tr("Log encoding: %1").arg(mEncodingCombo->currentText()));

    const QString executable = resolveServerExecutable();
    if (executable.isEmpty())
    {
        QMessageBox::warning(this, tr("Error starting executable"),
            tr("Could not find tes3mp-server executable next to the launcher."));
        mStopButton->setEnabled(false);
        return false;
    }

    mProcess->setProgram(QDir::toNativeSeparators(executable));
    mProcess->setArguments(QStringList());
    mProcess->setProcessChannelMode(QProcess::SeparateChannels);

    const QString workingDirectory = serverRuntimeBasePath();
    mProcess->setWorkingDirectory(workingDirectory);

    mLastStartMs = QDateTime::currentMSecsSinceEpoch();
    appendStatusLine(tr("Working directory: %1").arg(QDir::toNativeSeparators(workingDirectory)));

    mProcess->start();

    if (!mProcess->waitForStarted(3000))
    {
        QMessageBox::critical(this, tr("Error starting executable"), mProcess->errorString());
        mStopButton->setEnabled(false);
        return false;
    }

    emit runningChanged(true, resolveDisplayAddress(config.localAddress), config.port);
    return true;
}


bool Launcher::ServerDialog::isRunning() const
{
    return mProcess != nullptr && mProcess->state() != QProcess::NotRunning;
}


bool Launcher::ServerDialog::isServerReachable(int timeoutMs) const
{
    const ServerConfig config = readServerConfig();
    bool portOk = false;
    const quint16 port = config.port.toUShort(&portOk);
    if (!portOk || port == 0)
        return false;

    QString probeAddress = config.localAddress;
    if (probeAddress.isEmpty() || probeAddress == QLatin1String("0.0.0.0"))
        probeAddress = QStringLiteral("127.0.0.1");

    QTcpSocket socket;
    socket.connectToHost(probeAddress, port);
    const bool connected = socket.waitForConnected(timeoutMs);
    if (connected)
        socket.disconnectFromHost();
    return connected;
}

QString Launcher::ServerDialog::displayAddress() const
{
    const ServerConfig config = readServerConfig();
    return resolveDisplayAddress(config.localAddress);
}

QString Launcher::ServerDialog::configuredPort() const
{
    return readServerConfig().port;
}

bool Launcher::ServerDialog::setConfiguredPort(const QString& port, QString* errorMessage)
{
    bool portOk = false;
    const uint parsedPort = port.trimmed().toUInt(&portOk);
    if (!portOk || parsedPort == 0 || parsedPort > 65535)
    {
        if (errorMessage)
            *errorMessage = tr("Port must be a number from 1 to 65535.");
        return false;
    }

    QString preparationError;
    if (!preparePortableServer(&preparationError))
    {
        if (errorMessage)
            *errorMessage = preparationError;
        return false;
    }

    return setIniValue(readServerConfig().configPath, QStringLiteral("General"),
        QStringLiteral("port"), QString::number(parsedPort), errorMessage);
}

bool Launcher::ServerDialog::setConfiguredLocalAddress(const QString& address, QString* errorMessage)
{
    const QString value = address.trimmed().isEmpty() ? QStringLiteral("0.0.0.0") : address.trimmed();
    QHostAddress parsed;
    if (!parsed.setAddress(value) || parsed.protocol() != QAbstractSocket::IPv4Protocol)
    {
        if (errorMessage)
            *errorMessage = tr("Server bind address must be a valid IPv4 address.");
        return false;
    }

    QString preparationError;
    if (!preparePortableServer(&preparationError))
    {
        if (errorMessage)
            *errorMessage = preparationError;
        return false;
    }

    mCachedDisplayAddress.clear();
    mCachedDisplayAddressAtMs = 0;
    return setIniValue(readServerConfig().configPath, QStringLiteral("General"),
        QStringLiteral("localAddress"), value, errorMessage);
}

QString Launcher::ServerDialog::localConnectAddress() const
{
    const QString bindAddress = readServerConfig().localAddress.trimmed();
    if (bindAddress.isEmpty() || bindAddress == QLatin1String("0.0.0.0"))
        return QStringLiteral("127.0.0.1");
    return bindAddress;
}

QString Launcher::ServerDialog::requiredDataFilesPath() const
{
    return QDir(QDir(serverRuntimeBasePath()).filePath(QStringLiteral("server/data")))
        .filePath(QStringLiteral("requiredDataFiles.json"));
}

bool Launcher::ServerDialog::autoRestartEnabled() const
{
    return mRestartCheckBox != nullptr && mRestartCheckBox->isChecked();
}

void Launcher::ServerDialog::setAutoRestartEnabled(bool enabled)
{
    if (mRestartCheckBox != nullptr)
        mRestartCheckBox->setChecked(enabled);
}

bool Launcher::ServerDialog::clearPersistentCells(QString* errorMessage) const
{
    if (isRunning())
    {
        if (errorMessage)
            *errorMessage = tr("Stop the server before clearing persistent data.");
        return false;
    }

    const QString cellPath = QDir(QDir(serverRuntimeBasePath()).filePath(QStringLiteral("server/data")))
        .filePath(QStringLiteral("cell"));
    return clearDirectoryContents(cellPath, errorMessage);
}

bool Launcher::ServerDialog::resetPersistentServerData(QString* errorMessage) const
{
    if (isRunning())
    {
        if (errorMessage)
            *errorMessage = tr("Stop the server before clearing persistent data.");
        return false;
    }

    const QDir dataDir(QDir(serverRuntimeBasePath()).filePath(QStringLiteral("server/data")));
    const QStringList gameplayDirectories = QStringList()
        << QStringLiteral("player") << QStringLiteral("cell") << QStringLiteral("world")
        << QStringLiteral("map") << QStringLiteral("custom") << QStringLiteral("recordstore");

    for (const QString& directory : gameplayDirectories)
    {
        if (!clearDirectoryContents(dataDir.filePath(directory), errorMessage))
            return false;
    }

    // SQLite is optional, but when enabled it is persistent gameplay data too.
    const QString databasePath = dataDir.filePath(QStringLiteral("database.db"));
    if (QFileInfo::exists(databasePath) && !QFile::remove(databasePath))
    {
        if (errorMessage)
            *errorMessage = tr("Could not remove server data: %1")
                .arg(QDir::toNativeSeparators(databasePath));
        return false;
    }

    // Intentionally preserve requiredDataFiles.json and banlist.json.
    return true;
}

void Launcher::ServerDialog::processReadyReadStandardOutput()
{
    appendRawLog(mProcess->readAllStandardOutput());
}

void Launcher::ServerDialog::processReadyReadStandardError()
{
    appendRawLog(mProcess->readAllStandardError());
}

void Launcher::ServerDialog::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    appendRawLog(mProcess->readAllStandardOutput());
    appendRawLog(mProcess->readAllStandardError());

    ++mRestartCounter;
    cleanupOldLogsIfNeeded();

    appendStatusLine(tr("Server stopped. Exit code: %1").arg(exitCode));
    emit runningChanged(false, displayAddress(), configuredPort());

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool rapidCrash = !mStopRequested
        && exitStatus == QProcess::CrashExit
        && mLastStartMs > 0
        && (nowMs - mLastStartMs) < 15000;

    if (rapidCrash)
        ++mRapidCrashCount;
    else
        mRapidCrashCount = 0;

    mRestartAfterBackup = false;
    if (!mStopRequested && mRestartCheckBox->isChecked())
    {
        if (mRapidCrashCount >= 3)
            appendStatusLine(tr("Server crashed too many times in a short period. Auto restart disabled."));
        else
            mRestartAfterBackup = true;
    }

    mCrashAfterStop = exitStatus == QProcess::CrashExit && !mStopRequested;
    mStopButton->setEnabled(false);

    if (mRestartCheckBox->isChecked())
    {
        QString backupError;
        if (startBackupArchive(&backupError))
            return;

        if (!backupError.isEmpty())
            appendStatusLine(tr("Backup failed: %1").arg(backupError));
    }
    finishServerStopSequence();
}

void Launcher::ServerDialog::processError(QProcess::ProcessError error)
{
    if (error == QProcess::Crashed)
        return;

    appendStatusLine(tr("Process error: %1").arg(mProcess->errorString()));
}

void Launcher::ServerDialog::stopServer()
{
    mStopRequested = true;
    if (mProcess->state() == QProcess::NotRunning)
    {
        mStopButton->setEnabled(false);
        emit runningChanged(false, displayAddress(), configuredPort());
        return;
    }

    appendStatusLine(tr("Stopping server..."));
    mStopButton->setEnabled(false);
    mProcess->terminate();

    // Never block the GUI thread while the server shuts down. Some server
    // scripts need a moment to flush state; force-kill only after the grace
    // period and only if the same process is still running.
    QTimer::singleShot(2000, this, [this]()
    {
        if (mProcess != nullptr && mProcess->state() != QProcess::NotRunning)
        {
            appendStatusLine(tr("Server did not stop in time; forcing termination..."));
            mProcess->kill();
        }
    });
}

void Launcher::ServerDialog::refreshDecodedLog()
{
    updateLogView();
}

void Launcher::ServerDialog::appendRawLog(const QByteArray& data)
{
    if (data.isEmpty())
        return;

    mRawLog.append(data);
    updateLogView();
}

void Launcher::ServerDialog::updateLogView()
{
    QTextCodec* codec = currentCodec();
    const QString text = codec != nullptr ? codec->toUnicode(mRawLog) : QString::fromUtf8(mRawLog.constData(), mRawLog.size());
    mLogView->setPlainText(text);
    QTextCursor cursor = mLogView->textCursor();
    cursor.movePosition(QTextCursor::End);
    mLogView->setTextCursor(cursor);
    mLogView->ensureCursorVisible();
}

QTextCodec* Launcher::ServerDialog::currentCodec() const
{
    const QString codecName = mEncodingCombo->currentData().toString();
    if (codecName == QLatin1String("System"))
        return QTextCodec::codecForLocale();

    QTextCodec* codec = QTextCodec::codecForName(codecName.toUtf8());
    return codec != nullptr ? codec : QTextCodec::codecForLocale();
}

Launcher::ServerDialog::ServerConfig Launcher::ServerDialog::readServerConfig() const
{
    ServerConfig result;
    result.localAddress = QStringLiteral("0.0.0.0");
    result.port = QStringLiteral("25565");

    const QDir serverBaseDir(serverRuntimeBasePath());
    const QDir userDir(QDir(runtimeDataBasePath()).filePath(QStringLiteral("userdata")));
    result.serverHomePath = serverBaseDir.filePath(QStringLiteral("server"));
    result.configPath = userDir.filePath(QStringLiteral("tes3mp-server.cfg"));

    QString configuredPluginHome = QStringLiteral("./server");

    // Load from lowest to highest priority. The server scripts always come
    // from the bundled server directory; userdata contains only the cfg file.
    QStringList configCandidates;
    configCandidates << serverBaseDir.filePath(QStringLiteral("tes3mp-server-default.cfg"))
                     << userDir.filePath(QStringLiteral("tes3mp-server-default.cfg"))
                     << serverBaseDir.filePath(QStringLiteral("tes3mp-server.cfg"))
                     << userDir.filePath(QStringLiteral("tes3mp-server.cfg"));

    for (const QString& configPath : configCandidates)
    {
        QFile file(configPath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        result.configPath = QFileInfo(configPath).absoluteFilePath();

        QString currentSection;
        QTextStream in(&file);
        while (!in.atEnd())
        {
            const QString rawLine = in.readLine().trimmed();
            if (rawLine.isEmpty() || rawLine.startsWith('#') || rawLine.startsWith(';'))
                continue;
            if (rawLine.startsWith('[') && rawLine.endsWith(']'))
            {
                currentSection = rawLine.mid(1, rawLine.size() - 2).trimmed();
                continue;
            }

            const int equalsPos = rawLine.indexOf('=');
            if (equalsPos < 0)
                continue;

            const QString key = rawLine.left(equalsPos).trimmed();
            const QString value = rawLine.mid(equalsPos + 1).trimmed();

            if (currentSection == QLatin1String("General"))
            {
                if (key == QLatin1String("localAddress"))
                    result.localAddress = value;
                else if (key == QLatin1String("port"))
                    result.port = value;
            }
            else if (currentSection == QLatin1String("Plugins") && key == QLatin1String("home"))
            {
                configuredPluginHome = value;
            }
        }
    }

    if (QDir::isRelativePath(configuredPluginHome))
        result.serverHomePath = serverBaseDir.absoluteFilePath(configuredPluginHome);
    else
        result.serverHomePath = QDir(configuredPluginHome).absolutePath();

    // Do not allow a legacy portable config to redirect the launcher back to
    // userdata/server. That directory is no longer an active server tree.
    const QString legacyServerPath = QDir(userDir.absolutePath()).absoluteFilePath(QStringLiteral("server"));
    if (QDir::cleanPath(result.serverHomePath) == QDir::cleanPath(legacyServerPath))
        result.serverHomePath = serverBaseDir.filePath(QStringLiteral("server"));

    result.configPath = QFileInfo(result.configPath).absoluteFilePath();
    return result;
}

bool Launcher::ServerDialog::preparePortableServer(QString* errorMessage) const
{
    const QDir serverBaseDir(serverRuntimeBasePath());
    const QString bundledServerPath = serverBaseDir.filePath(QStringLiteral("server"));
    const QString bundledCore = QDir(bundledServerPath).filePath(QStringLiteral("scripts/serverCore.lua"));
    const QString bundledConfig = QDir(bundledServerPath).filePath(QStringLiteral("scripts/config.lua"));
    const QString bundledVersion = QDir(bundledServerPath).filePath(QStringLiteral("ARENAMP_CORE_VERSION.txt"));

    if (!QFileInfo::exists(bundledCore) || !QFileInfo::exists(bundledConfig)
        || !QFileInfo::exists(bundledVersion))
    {
        if (errorMessage)
            *errorMessage = tr("The bundled ArenaMP server core is incomplete: %1")
                .arg(QDir::toNativeSeparators(bundledServerPath));
        return false;
    }

    const QString userDataPath = QDir(runtimeDataBasePath()).filePath(QStringLiteral("userdata"));
    if (!QDir().mkpath(userDataPath))
    {
        if (errorMessage)
            *errorMessage = tr("Could not create userdata directory: %1")
                .arg(QDir::toNativeSeparators(userDataPath));
        return false;
    }

    const QString userConfigPath = QDir(userDataPath).filePath(QStringLiteral("tes3mp-server.cfg"));
    if (!QFileInfo::exists(userConfigPath))
    {
        const QString defaultConfigPath = serverBaseDir.filePath(QStringLiteral("tes3mp-server-default.cfg"));
        if (!copyFileReplacing(defaultConfigPath, userConfigPath, errorMessage))
            return false;
    }

    // Keep the editable cfg in userdata, but always run the bundled server
    // tree. No files or directories are created under userdata/server.
    return setIniValue(userConfigPath, QStringLiteral("Plugins"), QStringLiteral("home"),
        QStringLiteral("./server"), errorMessage);
}

QString Launcher::ServerDialog::resolveServerExecutable() const
{
    QDir dir(applicationBasePath());
#ifdef Q_OS_WIN
    const QString executable = dir.absoluteFilePath(QStringLiteral("tes3mp-server.exe"));
#else
    const QString executable = dir.absoluteFilePath(QStringLiteral("tes3mp-server"));
#endif
    return QFileInfo(executable).exists() ? executable : QString();
}

QString Launcher::ServerDialog::resolveDisplayAddress(const QString& bindAddress) const
{
    if (!bindAddress.isEmpty() && bindAddress != QLatin1String("0.0.0.0"))
        return bindAddress;

    // QNetworkInterface::allInterfaces() may wake Windows network-location
    // services hosted by svchost.exe. Cache the LAN address instead of
    // enumerating every time the launcher refreshes its main page.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!mCachedDisplayAddress.isEmpty() && now - mCachedDisplayAddressAtMs < 30000)
        return mCachedDisplayAddress;

    QString bestAddress;
    int bestScore = -1;

    foreach (const QNetworkInterface& iface, QNetworkInterface::allInterfaces())
    {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning))
            continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;

        const QString interfaceName = (iface.humanReadableName() + QLatin1Char(' ') + iface.name()).toLower();
        const bool looksVirtual = interfaceName.contains(QLatin1String("virtual"))
            || interfaceName.contains(QLatin1String("vmware"))
            || interfaceName.contains(QLatin1String("hyper-v"))
            || interfaceName.contains(QLatin1String("vethernet"))
            || interfaceName.contains(QLatin1String("wsl"))
            || interfaceName.contains(QLatin1String("docker"))
            || interfaceName.contains(QLatin1String("loopback"));

        foreach (const QNetworkAddressEntry& entry, iface.addressEntries())
        {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            const QString address = entry.ip().toString();
            if (address.startsWith(QLatin1String("169.254.")) || address == QLatin1String("0.0.0.0"))
                continue;

            int score = looksVirtual ? 0 : 100;
            if (address.startsWith(QLatin1String("192.168.")))
                score += 30;
            else if (address.startsWith(QLatin1String("10.")))
                score += 20;
            else if (address.startsWith(QLatin1String("172.")))
            {
                const int secondOctet = address.section(QLatin1Char('.'), 1, 1).toInt();
                if (secondOctet >= 16 && secondOctet <= 31)
                    score += 10;
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestAddress = address;
            }
        }
    }

    mCachedDisplayAddress = bestAddress.isEmpty() ? QStringLiteral("127.0.0.1") : bestAddress;
    mCachedDisplayAddressAtMs = now;
    return mCachedDisplayAddress;
}

QString Launcher::ServerDialog::applicationBasePath() const
{
    return QCoreApplication::applicationDirPath();
}

QString Launcher::ServerDialog::serverRuntimeBasePath() const
{
#ifdef Q_OS_MAC
    return QDir(applicationBasePath()).absoluteFilePath(QStringLiteral("../Resources"));
#else
    return applicationBasePath();
#endif
}

QString Launcher::ServerDialog::runtimeDataBasePath() const
{
#ifdef Q_OS_MAC
    const QString writablePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!writablePath.isEmpty())
        return writablePath;
#endif
    return applicationBasePath();
}

QString Launcher::ServerDialog::backupDirectoryPath() const
{
    QDir dir(runtimeDataBasePath());
    const QString backupPath = dir.absoluteFilePath(QStringLiteral("Backup"));
    QDir().mkpath(backupPath);
    return backupPath;
}

QString Launcher::ServerDialog::makeBackupArchivePath() const
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy_MM_dd_HH_mm_ss"));
    return QDir(backupDirectoryPath()).absoluteFilePath(QStringLiteral("archive_") + timestamp + QStringLiteral(".zip"));
}

bool Launcher::ServerDialog::startBackupArchive(QString* errorMessage)
{
    if (mBackupInProgress || mBackupProcess->state() != QProcess::NotRunning)
    {
        if (errorMessage)
            *errorMessage = tr("A previous backup is still running.");
        return false;
    }

    const ServerConfig config = readServerConfig();
    const QFileInfo sourceInfo(config.serverHomePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir())
    {
        if (errorMessage)
            *errorMessage = tr("Server directory was not found: %1").arg(QDir::toNativeSeparators(config.serverHomePath));
        return false;
    }

    mPendingBackupPath = makeBackupArchivePath();
    mBackupProcess->setWorkingDirectory(applicationBasePath());
    mBackupProcess->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    QStringList arguments;
    arguments << QStringLiteral("-NoProfile")
              << QStringLiteral("-ExecutionPolicy") << QStringLiteral("Bypass")
              << QStringLiteral("-Command")
              << QStringLiteral("Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force")
                    .arg(QDir::toNativeSeparators(config.serverHomePath).replace("'", "''"),
                         QDir::toNativeSeparators(mPendingBackupPath).replace("'", "''"));
    mBackupProcess->setProgram(QStringLiteral("powershell.exe"));
    mBackupProcess->setArguments(arguments);
#else
    QStringList arguments;
    arguments << QStringLiteral("-r") << mPendingBackupPath << QDir(config.serverHomePath).dirName();
    mBackupProcess->setWorkingDirectory(QFileInfo(config.serverHomePath).absolutePath());
    mBackupProcess->setProgram(QStringLiteral("zip"));
    mBackupProcess->setArguments(arguments);
#endif

    mBackupInProgress = true;
    appendStatusLine(tr("Creating backup in background: %1")
        .arg(QDir::toNativeSeparators(mPendingBackupPath)));
    mBackupProcess->start();
    return true;
}

void Launcher::ServerDialog::backupProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!mBackupInProgress)
        return;

    const QString standardError = QString::fromLocal8Bit(mBackupProcess->readAllStandardError()).trimmed();
    if (exitStatus == QProcess::NormalExit && exitCode == 0)
        appendStatusLine(tr("Backup created: %1").arg(QDir::toNativeSeparators(mPendingBackupPath)));
    else
        appendStatusLine(tr("Backup failed: %1").arg(standardError.isEmpty()
            ? tr("archiver exit code %1").arg(exitCode) : standardError));

    mBackupInProgress = false;
    mPendingBackupPath.clear();
    finishServerStopSequence();
}

void Launcher::ServerDialog::backupProcessError(QProcess::ProcessError error)
{
    if (!mBackupInProgress || error == QProcess::Crashed)
        return;

    appendStatusLine(tr("Backup failed: %1").arg(mBackupProcess->errorString()));
    mBackupInProgress = false;
    mPendingBackupPath.clear();
    finishServerStopSequence();
}

void Launcher::ServerDialog::finishServerStopSequence()
{
    if (mCrashAfterStop)
        appendStatusLine(tr("The server process crashed."));
    mCrashAfterStop = false;

    if (mRestartAfterBackup)
    {
        mRestartAfterBackup = false;
        appendStatusLine(tr("Restarting server..."));
        startServer();
        return;
    }

    mStopButton->setEnabled(false);
}

void Launcher::ServerDialog::cleanupOldLogsIfNeeded()
{
    if (mRestartCounter < 5)
        return;

    mRestartCounter = 0;

    QDir logDir(QDir(runtimeDataBasePath()).absoluteFilePath(QStringLiteral("userdata")));
    if (!logDir.exists())
        return;

    const QStringList logFiles = logDir.entryList(
        QStringList() << QStringLiteral("tes3mp-client-*.log")
                      << QStringLiteral("tes3mp-server-*.log"),
        QDir::Files);
    for (const QString& fileName : logFiles)
        logDir.remove(fileName);

    if (!logFiles.isEmpty())
        appendStatusLine(tr("Old timestamped log files were cleaned up."));
}

void Launcher::ServerDialog::appendStatusLine(const QString& text)
{
    const QString line = QStringLiteral("[")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        + QStringLiteral("] ") + text + QStringLiteral("\n");
    mRawLog.append(line.toUtf8());
    updateLogView();
}
