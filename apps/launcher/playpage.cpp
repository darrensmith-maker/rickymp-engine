#include "playpage.hpp"

#include <QApplication>
#include <QAbstractSocket>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QIntValidator>
#include <QLineEdit>
#include <QMessageBox>
#include <QLabel>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSpinBox>
#include <QStringList>
#include <QTextStream>
#include <QVBoxLayout>

namespace
{
    bool findConfigAssignment(const QString& text, const QString& key, QString* value)
    {
        const QRegularExpression pattern(QStringLiteral("(^|\\n)\\s*config\\.%1\\s*=\\s*([^\\r\\n]+)").arg(QRegularExpression::escape(key)));
        const QRegularExpressionMatch match = pattern.match(text);
        if (!match.hasMatch())
            return false;

        if (value != nullptr)
            *value = match.captured(2).trimmed();
        return true;
    }

    QString replaceConfigAssignment(const QString& text, const QString& key, const QString& value)
    {
        const QRegularExpression pattern(QStringLiteral("(^|\\n)(\\s*config\\.%1\\s*=\\s*)([^\\r\\n]+)").arg(QRegularExpression::escape(key)));
        const QRegularExpressionMatch match = pattern.match(text);
        if (!match.hasMatch())
            return text;

        // Only update the first, user-editable declaration. Several ArenaMP
        // settings are assigned again later when their values are clamped. A
        // global regular-expression replacement would replace the first line
        // of a multi-line clamp call and leave its arguments behind as invalid
        // Lua syntax.
        QString result = text;
        result.replace(match.capturedStart(3), match.capturedLength(3), value);
        return result;
    }

    void loadLineEdit(QLineEdit* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2)
            value = value.mid(1, value.size() - 2);

        widget->setText(value);
    }

    void loadSpinBox(QSpinBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        bool ok = false;
        const int parsed = value.toInt(&ok);
        if (ok)
            widget->setValue(parsed);
    }

    void loadDoubleSpinBox(QDoubleSpinBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        bool ok = false;
        const double parsed = value.toDouble(&ok);
        if (ok)
            widget->setValue(parsed);
    }

    void loadCheckBox(QCheckBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        widget->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
    }

    void loadComboBox(QComboBox* widget, const QString& text, const QString& key)
    {
        QString value;
        if (!findConfigAssignment(text, key, &value))
            return;

        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2)
            value = value.mid(1, value.size() - 2);

        const int index = widget->findData(value);
        if (index >= 0)
            widget->setCurrentIndex(index);
    }
}

Launcher::PlayPage::PlayPage(QWidget *parent)
    : QWidget(parent)
    , mEmbeddedServerConsole(nullptr)
    , mSyncingServerSettingsTabs(false)
    , mHostInterfaceLabel(nullptr)
    , mHostInterfaceCombo(nullptr)
    , mRefreshHostInterfacesButton(nullptr)
    , mUpdateHashesButton(nullptr)
    , mEnforceRequiredCheckBox(nullptr)
    , mServerModeLabel(nullptr)
    , mServerModeCombo(nullptr)
    , mClearCellsButton(nullptr)
    , mResetServerButton(nullptr)
{
    setObjectName("PlayPage");
    setupUi(this);
    serverPortEdit->setValidator(new QIntValidator(1, 65535, serverPortEdit));

    // Host mode has a separate bind-interface selector.  The public/share
    // address must not be confused with the address the local socket binds to:
    // a router-owned WAN IP usually cannot be bound by the host machine.
    mHostInterfaceLabel = new QLabel(tr("Server network interface:"), this);
    mHostInterfaceLabel->setStyleSheet(QStringLiteral("font-size: 11pt; font-weight: 500;"));
    mHostInterfaceCombo = new QComboBox(this);
    mHostInterfaceCombo->setMinimumHeight(30);
    mHostInterfaceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    mRefreshHostInterfacesButton = new QPushButton(tr("Refresh"), this);
    mRefreshHostInterfacesButton->setMinimumHeight(30);

    // Host-only helper: regenerate server/data/requiredDataFiles.json from the
    // exact content=/groundcover= selection currently active in the launcher.
    mUpdateHashesButton = new QPushButton(tr("Update Hash"), this);
    mUpdateHashesButton->setMinimumHeight(28);
    mUpdateHashesButton->setToolTip(tr("Generate the server data-file manifest from the current Content Files order and CRC32 hashes."));

    mEnforceRequiredCheckBox = new QCheckBox(tr("Enforce required DataFiles"), this);
    mEnforceRequiredCheckBox->setToolTip(tr("Reject clients whose required content list, order or CRC32 hashes do not match the server manifest."));

    mServerModeLabel = new QLabel(tr("Server mode:"), this);
    mServerModeLabel->setStyleSheet(QStringLiteral("font-size: 11pt; font-weight: 500;"));
    mServerModeCombo = new QComboBox(this);
    mServerModeCombo->addItem(tr("CO-OP"), QStringLiteral("CO-OP"));
    mServerModeCombo->addItem(tr("MMO"), QStringLiteral("MMO"));
    mServerModeCombo->setMinimumHeight(28);
    mServerModeCombo->setToolTip(tr("CO-OP shares story and progression between players. MMO keeps player progression separate while required world/actor networking remains enabled."));

    mClearCellsButton = new QPushButton(tr("Clear server cells"), this);
    mResetServerButton = new QPushButton(tr("Full server reset"), this);
    mClearCellsButton->setMinimumHeight(28);
    mResetServerButton->setMinimumHeight(28);
    mClearCellsButton->setToolTip(tr("Delete saved cell state while keeping player accounts and world data."));
    mResetServerButton->setToolTip(tr("Delete all persistent gameplay data. The data-file manifest and ban list are preserved."));

    serverConnectionLayout->removeWidget(autoStartServerCheckBox);
    QHBoxLayout* hostModeLayout = new QHBoxLayout();
    hostModeLayout->setSpacing(8);
    hostModeLayout->addWidget(autoStartServerCheckBox);
    hostModeLayout->addStretch(1);
    hostModeLayout->addWidget(mUpdateHashesButton);
    serverConnectionLayout->addLayout(hostModeLayout, 2, 0, 1, 2);

    QHBoxLayout* hostInterfaceLayout = new QHBoxLayout();
    hostInterfaceLayout->setSpacing(8);
    hostInterfaceLayout->addWidget(mHostInterfaceLabel);
    hostInterfaceLayout->addWidget(mHostInterfaceCombo, 1);
    hostInterfaceLayout->addWidget(mRefreshHostInterfacesButton);
    serverConnectionLayout->addLayout(hostInterfaceLayout, 3, 0, 1, 2);

    // Compact two-column host controls.
    serverConnectionLayout->removeWidget(autoRestartServerCheckBox);
    serverConnectionLayout->removeWidget(vanillaServerCheckBox);
    serverConnectionLayout->removeWidget(hideChatHistoryCheckBox);
    serverConnectionLayout->addWidget(autoRestartServerCheckBox, 4, 0);
    serverConnectionLayout->addWidget(mEnforceRequiredCheckBox, 4, 1);

    QHBoxLayout* serverModeLayout = new QHBoxLayout();
    serverModeLayout->setSpacing(8);
    serverModeLayout->addWidget(mServerModeLabel);
    serverModeLayout->addWidget(mServerModeCombo, 1);
    serverConnectionLayout->addLayout(serverModeLayout, 5, 0, 1, 2);

    QHBoxLayout* maintenanceLayout = new QHBoxLayout();
    maintenanceLayout->setSpacing(8);
    maintenanceLayout->addWidget(mClearCellsButton);
    maintenanceLayout->addWidget(mResetServerButton);
    maintenanceLayout->addStretch(1);
    serverConnectionLayout->addLayout(maintenanceLayout, 6, 0, 1, 2);

    serverConnectionLayout->addWidget(vanillaServerCheckBox, 7, 0, 1, 2);
    serverConnectionLayout->addWidget(hideChatHistoryCheckBox, 8, 0, 1, 2);

    refreshHostInterfaces(QStringLiteral("0.0.0.0"));
    updateHostModeUi(autoStartServerCheckBox->isChecked());

    connect(playButton, SIGNAL(clicked()), this, SLOT(slotPlayClicked()));
    connect(serverButton, SIGNAL(clicked()), this, SLOT(slotServerClicked()));
    connect(stopServerButton, SIGNAL(clicked()), this, SLOT(slotStopServerClicked()));
    connect(autoStartServerCheckBox, SIGNAL(toggled(bool)), this, SLOT(slotAutoStartServerToggled(bool)));
    connect(mRefreshHostInterfacesButton, SIGNAL(clicked()), this, SLOT(slotRefreshHostInterfaces()));
    connect(mUpdateHashesButton, SIGNAL(clicked()), this, SLOT(slotUpdateHashesClicked()));
    connect(mEnforceRequiredCheckBox, SIGNAL(toggled(bool)), this, SLOT(slotEnforceRequiredToggled(bool)));
    connect(enforceDataFilesCheckBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (mEnforceRequiredCheckBox == nullptr || mEnforceRequiredCheckBox->isChecked() == enabled)
            return;
        const QSignalBlocker blocker(mEnforceRequiredCheckBox);
        mEnforceRequiredCheckBox->setChecked(enabled);
    });
    connect(mServerModeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(slotServerModeChanged(int)));
    connect(mClearCellsButton, SIGNAL(clicked()), this, SIGNAL(clearServerCellsRequested()));
    connect(mResetServerButton, SIGNAL(clicked()), this, SIGNAL(resetServerDataRequested()));
    connect(autoRestartServerCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(autoRestartServerChanged(bool)));
    connect(vanillaServerCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(vanillaServerCompatibilityChanged(bool)));
    connect(hideChatHistoryCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(hideChatHistoryChanged(bool)));
    connect(reloadServerSettingsButton, SIGNAL(clicked()), this, SLOT(slotReloadServerSettings()));
    connect(saveServerSettingsButton, SIGNAL(clicked()), this, SLOT(slotSaveServerSettings()));
    connect(applyServerSettingsFormButton, SIGNAL(clicked()), this, SLOT(slotApplyFormToRawConfig()));
    connect(syncServerSettingsFormButton, SIGNAL(clicked()), this, SLOT(slotSyncFormFromRawConfig()));
    connect(serverSettingsModeTabs, SIGNAL(currentChanged(int)), this, SLOT(slotServerSettingsModeChanged(int)));

    pageTabs->setCurrentIndex(0);
    serverSettingsModeTabs->setCurrentIndex(0);
    loadServerSettings();
}


void Launcher::PlayPage::setBuildName(const QString& name)
{
    buildNameEdit->setText(name.trimmed().isEmpty() ? QStringLiteral("ArenaMP") : name.trimmed());
}

QString Launcher::PlayPage::buildName() const
{
    const QString name = buildNameEdit->text().trimmed();
    return name.isEmpty() ? QStringLiteral("ArenaMP") : name;
}

void Launcher::PlayPage::setServerAddress(const QString& addr)
{
    serverAddressEdit->setText(addr);
}

void Launcher::PlayPage::setServerPort(const QString& port)
{
    serverPortEdit->setText(port);
}

void Launcher::PlayPage::setBuildManifestComplete(bool complete)
{
    buildNameEdit->setReadOnly(complete);
    buildNameEdit->setFocusPolicy(complete ? Qt::NoFocus : Qt::StrongFocus);
    buildNameEdit->setToolTip(complete
        ? tr("The build name is locked by build.ini (complete=true).") : QString());

    serverLabel->setVisible(!complete);
    portLabel->setVisible(!complete);
    serverAddressEdit->setVisible(!complete);
    serverPortEdit->setVisible(!complete);

    // A complete manifest fixes the network identity together with its endpoint.
    vanillaServerCheckBox->setEnabled(!complete);
    if (complete)
        vanillaServerCheckBox->setToolTip(tr("This value is locked by build.ini (complete=true)."));
}

void Launcher::PlayPage::setAutoStartServer(bool enabled)
{
    autoStartServerCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setAutoRestartServer(bool enabled)
{
    autoRestartServerCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setEnforceDataFiles(bool enabled)
{
    if (mEnforceRequiredCheckBox != nullptr)
    {
        const QSignalBlocker blocker(mEnforceRequiredCheckBox);
        mEnforceRequiredCheckBox->setChecked(enabled);
    }
    if (enforceDataFilesCheckBox != nullptr)
        enforceDataFilesCheckBox->setChecked(enabled);
}

bool Launcher::PlayPage::enforceDataFiles() const
{
    return mEnforceRequiredCheckBox != nullptr
        ? mEnforceRequiredCheckBox->isChecked()
        : enforceDataFilesCheckBox->isChecked();
}

void Launcher::PlayPage::setVanillaServerCompatibility(bool enabled)
{
    vanillaServerCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setHideChatHistory(bool enabled)
{
    hideChatHistoryCheckBox->setChecked(enabled);
}

void Launcher::PlayPage::setServerRunning(bool running, const QString&, const QString&, bool managed)
{
    stopServerButton->setEnabled(running && managed);
    serverButton->setEnabled(!running);

    // The running server endpoint is status information only. Do not write it
    // back into the editable fields: doing so used to replace the user's
    // address/port with the launcher's LAN/default endpoint as soon as a
    // local server was started.
}

void Launcher::PlayPage::setServerConsoleWidget(QWidget* widget)
{
    if (widget == nullptr)
        return;

    if (mEmbeddedServerConsole == widget)
        return;

    if (mEmbeddedServerConsole != nullptr)
    {
        serverConsoleHostLayout->removeWidget(mEmbeddedServerConsole);
        mEmbeddedServerConsole->setParent(nullptr);
    }

    mEmbeddedServerConsole = widget;
    widget->setParent(serverConsoleHost);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    serverConsoleHostLayout->addWidget(widget);
}

QString Launcher::PlayPage::serverAddress() const
{
    QString addr = serverAddressEdit->text().trimmed();
    return addr.isEmpty() ? QString("localhost") : addr;
}

bool Launcher::PlayPage::autoStartServer() const
{
    return autoStartServerCheckBox->isChecked();
}

bool Launcher::PlayPage::autoRestartServer() const
{
    return autoRestartServerCheckBox->isChecked();
}

bool Launcher::PlayPage::vanillaServerCompatibility() const
{
    return vanillaServerCheckBox->isChecked();
}

bool Launcher::PlayPage::hideChatHistory() const
{
    return hideChatHistoryCheckBox->isChecked();
}

QString Launcher::PlayPage::serverPort() const
{
    QString p = serverPortEdit->text().trimmed();
    return p.isEmpty() ? QString("25565") : p;
}

QString Launcher::PlayPage::hostBindAddress() const
{
    if (mHostInterfaceCombo == nullptr)
        return QStringLiteral("0.0.0.0");

    const QString address = mHostInterfaceCombo->currentData().toString().trimmed();
    return address.isEmpty() ? QStringLiteral("0.0.0.0") : address;
}

void Launcher::PlayPage::setHostBindAddress(const QString& address)
{
    const QString wanted = address.trimmed().isEmpty() ? QStringLiteral("0.0.0.0") : address.trimmed();
    refreshHostInterfaces(wanted);
}

void Launcher::PlayPage::refreshHostInterfaces(const QString& preferredAddress)
{
    if (mHostInterfaceCombo == nullptr)
        return;

    const QString previous = preferredAddress.isEmpty() ? hostBindAddress() : preferredAddress;
    mHostInterfaceCombo->clear();
    mHostInterfaceCombo->addItem(tr("All interfaces (recommended) - 0.0.0.0"), QStringLiteral("0.0.0.0"));
    mHostInterfaceCombo->addItem(tr("Local only - 127.0.0.1"), QStringLiteral("127.0.0.1"));

    QStringList seenAddresses;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces)
    {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning))
            continue;

        for (const QNetworkAddressEntry& entry : iface.addressEntries())
        {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            const QString address = entry.ip().toString();
            if (address.isEmpty() || address == QLatin1String("0.0.0.0")
                || address == QLatin1String("127.0.0.1") || address.startsWith(QLatin1String("169.254."))
                || seenAddresses.contains(address))
                continue;

            seenAddresses.append(address);
            QString name = iface.humanReadableName().trimmed();
            if (name.isEmpty())
                name = iface.name();
            mHostInterfaceCombo->addItem(QStringLiteral("%1 - %2").arg(name, address), address);
        }
    }

    int index = mHostInterfaceCombo->findData(previous);
    if (index < 0)
    {
        // Keep a previously selected adapter address visible if the VPN is
        // temporarily down. The user can refresh when it comes back.
        if (!previous.isEmpty() && previous != QLatin1String("0.0.0.0"))
        {
            mHostInterfaceCombo->addItem(tr("Unavailable interface - %1").arg(previous), previous);
            index = mHostInterfaceCombo->count() - 1;
        }
        else
            index = 0;
    }
    mHostInterfaceCombo->setCurrentIndex(index);
}

void Launcher::PlayPage::updateHostModeUi(bool enabled)
{
    if (mHostInterfaceLabel != nullptr)
        mHostInterfaceLabel->setVisible(enabled);
    if (mHostInterfaceCombo != nullptr)
        mHostInterfaceCombo->setVisible(enabled);
    if (mRefreshHostInterfacesButton != nullptr)
        mRefreshHostInterfacesButton->setVisible(enabled);
    if (mUpdateHashesButton != nullptr)
        mUpdateHashesButton->setVisible(enabled);
    autoRestartServerCheckBox->setVisible(enabled);
    if (mEnforceRequiredCheckBox != nullptr)
        mEnforceRequiredCheckBox->setVisible(enabled);
    if (mServerModeLabel != nullptr)
        mServerModeLabel->setVisible(enabled);
    if (mServerModeCombo != nullptr)
        mServerModeCombo->setVisible(enabled);
    if (mClearCellsButton != nullptr)
        mClearCellsButton->setVisible(enabled);
    if (mResetServerButton != nullptr)
        mResetServerButton->setVisible(enabled);

    serverLabel->setText(enabled ? tr("Address for players:") : tr("Server Address:"));
    serverAddressEdit->setPlaceholderText(enabled
        ? tr("Radmin/VPN IP or public IP (optional)")
        : tr("192.168.x.x"));
    serverAddressEdit->setToolTip(enabled
        ? tr("Address you give to other players. It is not used as the server bind address. "
             "For a public Internet IP, forward the UDP server port on your router to this PC. "
             "For Radmin VPN, enter the Radmin IPv4 address or leave this as your preferred share address.")
        : QString());
}

void Launcher::PlayPage::switchToServerConsoleTab()
{
    pageTabs->setCurrentWidget(serverConsoleTab);
}

QString Launcher::PlayPage::serverConfigPath() const
{
    const QDir baseDir(QApplication::applicationDirPath());
#ifdef Q_OS_MAC
    return QDir::cleanPath(
        baseDir.filePath(QStringLiteral("../Resources/server/scripts/config.lua")));
#else
    return QDir::cleanPath(
        baseDir.filePath(QStringLiteral("server/scripts/config.lua")));
#endif
}

QString Launcher::PlayPage::persistentServerConfigPath() const
{
#ifdef Q_OS_MAC
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty())
        basePath = QApplication::applicationDirPath();
    const QDir baseDir(basePath);
#else
    const QDir baseDir(QApplication::applicationDirPath());
#endif
    return QDir::cleanPath(baseDir.filePath(QStringLiteral("userdata/server-config.lua")));
}

bool Launcher::PlayPage::writeServerConfigFile(const QString& path, const QString& text, QString* errorMessage) const
{
    const QFileInfo info(path);
    QDir dir = info.absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (errorMessage != nullptr)
            *errorMessage = tr("Could not create directory: %1").arg(QDir::toNativeSeparators(dir.absolutePath()));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (errorMessage != nullptr)
            *errorMessage = tr("Could not write: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << text;
    file.close();
    return file.error() == QFile::NoError;
}

QString Launcher::PlayPage::replaceRawValue(const QString& text, const QString& key, const QString& value) const
{
    return replaceConfigAssignment(text, key, value);
}

void Launcher::PlayPage::setServerSettingsStatus(const QString& text, bool isError)
{
    serverSettingsStatusLabel->setText(text);
    serverSettingsStatusLabel->setStyleSheet(isError
        ? QStringLiteral("color: #9f1f1f; font-weight: 600;")
        : QStringLiteral("color: #245027; font-weight: 600;"));
}

void Launcher::PlayPage::populateFormFromConfig(const QString& text)
{
    if (QComboBox* serverLanguageComboBox = findChild<QComboBox*>(QStringLiteral("serverLanguageComboBox")))
        loadComboBox(serverLanguageComboBox, text, QStringLiteral("serverLanguage"));
    loadLineEdit(gameModeEdit, text, QStringLiteral("gameMode"));
    loadLineEdit(dataPathEdit, text, QStringLiteral("dataPath"));

    loadSpinBox(loginTimeSpinBox, text, QStringLiteral("loginTime"));
    loadSpinBox(maxClientsPerIPSpinBox, text, QStringLiteral("maxClientsPerIP"));
    loadSpinBox(difficultySpinBox, text, QStringLiteral("difficulty"));
    loadSpinBox(nightStartHourSpinBox, text, QStringLiteral("nightStartHour"));
    loadSpinBox(nightEndHourSpinBox, text, QStringLiteral("nightEndHour"));
    loadSpinBox(deathTimeSpinBox, text, QStringLiteral("deathTime"));
    loadSpinBox(deathPenaltyJailDaysSpinBox, text, QStringLiteral("deathPenaltyJailDays"));
    loadSpinBox(fixmeIntervalSpinBox, text, QStringLiteral("fixmeInterval"));
    loadSpinBox(pingDifferenceSpinBox, text, QStringLiteral("pingDifferenceRequiredForAuthority"));
    loadSpinBox(enforcedLogLevelSpinBox, text, QStringLiteral("enforcedLogLevel"));
    loadSpinBox(physicsFramerateSpinBox, text, QStringLiteral("physicsFramerate"));

    loadCheckBox(arenaTacticalCombatCheckBox, text, QStringLiteral("arenaTacticalCombat"));
    loadDoubleSpinBox(arenaCombatWeaponSheatheDelayDoubleSpinBox, text, QStringLiteral("arenaCombatWeaponSheatheDelay"));
    loadCheckBox(arenaCombatPursuitThroughDoorsCheckBox, text, QStringLiteral("arenaCombatPursuitThroughDoors"));
    loadSpinBox(arenaCombatPursuitGuaranteedDistanceSpinBox, text, QStringLiteral("arenaCombatPursuitGuaranteedDistance"));
    loadSpinBox(arenaCombatPursuitDoorMaxDistanceSpinBox, text, QStringLiteral("arenaCombatPursuitDoorMaxDistance"));
    loadDoubleSpinBox(arenaCombatPursuitMinimumChanceDoubleSpinBox, text, QStringLiteral("arenaCombatPursuitMinimumChance"));
    loadSpinBox(arenaCombatPursuitMaxActorsSpinBox, text, QStringLiteral("arenaCombatPursuitMaxActors"));
    loadSpinBox(arenaCombatPursuitMaxDistanceSpinBox, text, QStringLiteral("arenaCombatPursuitMaxDistance"));
    loadCheckBox(arenaFollowersAttackOnSightCheckBox, text, QStringLiteral("arenaFollowersAttackOnSight"));
    loadCheckBox(arenaNpcAvoidCollisionsCheckBox, text, QStringLiteral("arenaNpcAvoidCollisions"));
    loadCheckBox(arenaNpcGiveWayCheckBox, text, QStringLiteral("arenaNpcGiveWay"));
    loadCheckBox(arenaAllowActorsFollowOverWaterCheckBox, text, QStringLiteral("arenaAllowActorsFollowOverWater"));
    loadSpinBox(arenaActorsProcessingRangeSpinBox, text, QStringLiteral("arenaActorsProcessingRange"));
    loadCheckBox(arenaCanLootDuringDeathAnimationCheckBox, text, QStringLiteral("arenaCanLootDuringDeathAnimation"));
    loadCheckBox(arenaWeaponSheathingCheckBox, text, QStringLiteral("arenaWeaponSheathing"));
    loadCheckBox(arenaShieldSheathingCheckBox, text, QStringLiteral("arenaShieldSheathing"));
    loadCheckBox(arenaGraphicHerbalismCheckBox, text, QStringLiteral("arenaGraphicHerbalism"));
    loadCheckBox(arenaLongBladesUseAgilityCheckBox, text, QStringLiteral("arenaLongBladesUseAgility"));
    loadCheckBox(arenaTwoHandedAccuracyPenaltyCheckBox, text, QStringLiteral("arenaTwoHandedAccuracyPenalty"));
    loadCheckBox(arenaStavesAccuracyBonusCheckBox, text, QStringLiteral("arenaStavesAccuracyBonus"));
    loadCheckBox(arenaSkillBooksLevelLimitCheckBox, text, QStringLiteral("arenaSkillBooksLevelLimit"));
    loadCheckBox(arenaNewConstantEffectDifficultyCheckBox, text, QStringLiteral("arenaNewConstantEffectDifficulty"));
    loadDoubleSpinBox(arenaGlobalXpMultiplierDoubleSpinBox, text, QStringLiteral("arenaGlobalXpMultiplier"));

    loadCheckBox(passTimeWhenEmptyCheckBox, text, QStringLiteral("passTimeWhenEmpty"));
    loadCheckBox(allowConsoleCheckBox, text, QStringLiteral("allowConsole"));
    loadCheckBox(allowBedRestCheckBox, text, QStringLiteral("allowBedRest"));
    loadCheckBox(allowWildernessRestCheckBox, text, QStringLiteral("allowWildernessRest"));
    loadCheckBox(allowWaitCheckBox, text, QStringLiteral("allowWait"));
    loadCheckBox(useInstancedSpawnCheckBox, text, QStringLiteral("useInstancedSpawn"));
    loadCheckBox(respawnAtImperialShrineCheckBox, text, QStringLiteral("respawnAtImperialShrine"));
    loadCheckBox(respawnAtTribunalTempleCheckBox, text, QStringLiteral("respawnAtTribunalTemple"));
    loadCheckBox(playersRespawnCheckBox, text, QStringLiteral("playersRespawn"));
    loadCheckBox(bountyResetOnDeathCheckBox, text, QStringLiteral("bountyResetOnDeath"));
    loadCheckBox(bountyDeathPenaltyCheckBox, text, QStringLiteral("bountyDeathPenalty"));
    loadCheckBox(allowSuicideCommandCheckBox, text, QStringLiteral("allowSuicideCommand"));
    loadCheckBox(allowFixmeCommandCheckBox, text, QStringLiteral("allowFixmeCommand"));
    loadCheckBox(allowOnContainerForUnloadedCellsCheckBox, text, QStringLiteral("allowOnContainerForUnloadedCells"));
    loadCheckBox(enablePlayerCollisionCheckBox, text, QStringLiteral("enablePlayerCollision"));
    loadCheckBox(enableActorCollisionCheckBox, text, QStringLiteral("enableActorCollision"));
    loadCheckBox(enablePlacedObjectCollisionCheckBox, text, QStringLiteral("enablePlacedObjectCollision"));
    loadCheckBox(useActorCollisionForPlacedObjectsCheckBox, text, QStringLiteral("useActorCollisionForPlacedObjects"));
    loadCheckBox(enforceDataFilesCheckBox, text, QStringLiteral("enforceDataFiles"));
    if (mEnforceRequiredCheckBox != nullptr)
    {
        const QSignalBlocker blocker(mEnforceRequiredCheckBox);
        mEnforceRequiredCheckBox->setChecked(enforceDataFilesCheckBox->isChecked());
    }
    loadCheckBox(ignoreScriptErrorsCheckBox, text, QStringLiteral("ignoreScriptErrors"));

    loadCheckBox(shareJournalCheckBox, text, QStringLiteral("shareJournal"));
    loadCheckBox(shareFactionRanksCheckBox, text, QStringLiteral("shareFactionRanks"));
    loadCheckBox(shareFactionExpulsionCheckBox, text, QStringLiteral("shareFactionExpulsion"));
    loadCheckBox(shareFactionReputationCheckBox, text, QStringLiteral("shareFactionReputation"));
    loadCheckBox(shareTopicsCheckBox, text, QStringLiteral("shareTopics"));
    loadCheckBox(shareBountyCheckBox, text, QStringLiteral("shareBounty"));
    loadCheckBox(shareReputationCheckBox, text, QStringLiteral("shareReputation"));
    loadCheckBox(shareMapExplorationCheckBox, text, QStringLiteral("shareMapExploration"));
    loadCheckBox(shareVideosCheckBox, text, QStringLiteral("shareVideos"));
    loadCheckBox(shareKillsCheckBox, text, QStringLiteral("shareKills"));

    if (mServerModeCombo != nullptr)
    {
        const bool coop = shareJournalCheckBox->isChecked()
            && shareFactionRanksCheckBox->isChecked()
            && shareFactionReputationCheckBox->isChecked()
            && shareTopicsCheckBox->isChecked()
            && shareReputationCheckBox->isChecked();
        const QSignalBlocker blocker(mServerModeCombo);
        mServerModeCombo->setCurrentIndex(coop ? 0 : 1);
    }

    loadComboBox(databaseTypeComboBox, text, QStringLiteral("databaseType"));
}

QString Launcher::PlayPage::updatedConfigFromForm(const QString& input) const
{
    QString text = input;

    const auto replaceString = [&text](const QString& key, const QString& value)
    {
        text = replaceConfigAssignment(text, key, QStringLiteral("\"") + value + QStringLiteral("\""));
    };

    const auto replaceNumber = [&text](const QString& key, int value)
    {
        text = replaceConfigAssignment(text, key, QString::number(value));
    };

    const auto replaceReal = [&text](const QString& key, double value, int precision)
    {
        text = replaceConfigAssignment(text, key, QString::number(value, 'f', precision));
    };

    const auto replaceBool = [&text](const QString& key, bool value)
    {
        text = replaceConfigAssignment(text, key, value ? QStringLiteral("true") : QStringLiteral("false"));
    };

    const QComboBox* serverLanguageComboBox = findChild<QComboBox*>(QStringLiteral("serverLanguageComboBox"));
    const QString serverLanguage = serverLanguageComboBox != nullptr
        ? serverLanguageComboBox->currentData().toString()
        : QStringLiteral("AUTO");
    replaceString(QStringLiteral("serverLanguage"),
        serverLanguage.isEmpty() ? QStringLiteral("AUTO") : serverLanguage);
    replaceString(QStringLiteral("gameMode"), gameModeEdit->text().trimmed());

    const QString dataPathValue = dataPathEdit->text().trimmed();
    if (dataPathValue == QStringLiteral("tes3mp.GetDataPath()"))
        text = replaceRawValue(text, QStringLiteral("dataPath"), dataPathValue);
    else
        replaceString(QStringLiteral("dataPath"), dataPathValue);

    replaceNumber(QStringLiteral("loginTime"), loginTimeSpinBox->value());
    replaceNumber(QStringLiteral("maxClientsPerIP"), maxClientsPerIPSpinBox->value());
    replaceNumber(QStringLiteral("difficulty"), difficultySpinBox->value());
    replaceNumber(QStringLiteral("nightStartHour"), nightStartHourSpinBox->value());
    replaceNumber(QStringLiteral("nightEndHour"), nightEndHourSpinBox->value());
    replaceNumber(QStringLiteral("deathTime"), deathTimeSpinBox->value());
    replaceNumber(QStringLiteral("deathPenaltyJailDays"), deathPenaltyJailDaysSpinBox->value());
    replaceNumber(QStringLiteral("fixmeInterval"), fixmeIntervalSpinBox->value());
    replaceNumber(QStringLiteral("pingDifferenceRequiredForAuthority"), pingDifferenceSpinBox->value());
    replaceNumber(QStringLiteral("enforcedLogLevel"), enforcedLogLevelSpinBox->value());
    replaceNumber(QStringLiteral("physicsFramerate"), physicsFramerateSpinBox->value());

    replaceBool(QStringLiteral("arenaTacticalCombat"), arenaTacticalCombatCheckBox->isChecked());
    replaceReal(QStringLiteral("arenaCombatWeaponSheatheDelay"), arenaCombatWeaponSheatheDelayDoubleSpinBox->value(), 1);
    replaceBool(QStringLiteral("arenaCombatPursuitThroughDoors"), arenaCombatPursuitThroughDoorsCheckBox->isChecked());
    replaceNumber(QStringLiteral("arenaCombatPursuitGuaranteedDistance"), arenaCombatPursuitGuaranteedDistanceSpinBox->value());
    replaceNumber(QStringLiteral("arenaCombatPursuitDoorMaxDistance"), arenaCombatPursuitDoorMaxDistanceSpinBox->value());
    replaceReal(QStringLiteral("arenaCombatPursuitMinimumChance"), arenaCombatPursuitMinimumChanceDoubleSpinBox->value(), 2);
    replaceNumber(QStringLiteral("arenaCombatPursuitMaxActors"), arenaCombatPursuitMaxActorsSpinBox->value());
    replaceNumber(QStringLiteral("arenaCombatPursuitMaxDistance"), arenaCombatPursuitMaxDistanceSpinBox->value());
    replaceBool(QStringLiteral("arenaFollowersAttackOnSight"), arenaFollowersAttackOnSightCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaNpcAvoidCollisions"), arenaNpcAvoidCollisionsCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaNpcGiveWay"), arenaNpcGiveWayCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaAllowActorsFollowOverWater"), arenaAllowActorsFollowOverWaterCheckBox->isChecked());
    replaceNumber(QStringLiteral("arenaActorsProcessingRange"), arenaActorsProcessingRangeSpinBox->value());
    replaceBool(QStringLiteral("arenaCanLootDuringDeathAnimation"), arenaCanLootDuringDeathAnimationCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaWeaponSheathing"), arenaWeaponSheathingCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaShieldSheathing"), arenaShieldSheathingCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaGraphicHerbalism"), arenaGraphicHerbalismCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaLongBladesUseAgility"), arenaLongBladesUseAgilityCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaTwoHandedAccuracyPenalty"), arenaTwoHandedAccuracyPenaltyCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaStavesAccuracyBonus"), arenaStavesAccuracyBonusCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaSkillBooksLevelLimit"), arenaSkillBooksLevelLimitCheckBox->isChecked());
    replaceBool(QStringLiteral("arenaNewConstantEffectDifficulty"), arenaNewConstantEffectDifficultyCheckBox->isChecked());
    replaceReal(QStringLiteral("arenaGlobalXpMultiplier"), arenaGlobalXpMultiplierDoubleSpinBox->value(), 2);

    replaceBool(QStringLiteral("passTimeWhenEmpty"), passTimeWhenEmptyCheckBox->isChecked());
    replaceBool(QStringLiteral("allowConsole"), allowConsoleCheckBox->isChecked());
    replaceBool(QStringLiteral("allowBedRest"), allowBedRestCheckBox->isChecked());
    replaceBool(QStringLiteral("allowWildernessRest"), allowWildernessRestCheckBox->isChecked());
    replaceBool(QStringLiteral("allowWait"), allowWaitCheckBox->isChecked());
    replaceBool(QStringLiteral("useInstancedSpawn"), useInstancedSpawnCheckBox->isChecked());
    replaceBool(QStringLiteral("respawnAtImperialShrine"), respawnAtImperialShrineCheckBox->isChecked());
    replaceBool(QStringLiteral("respawnAtTribunalTemple"), respawnAtTribunalTempleCheckBox->isChecked());
    replaceBool(QStringLiteral("playersRespawn"), playersRespawnCheckBox->isChecked());
    replaceBool(QStringLiteral("bountyResetOnDeath"), bountyResetOnDeathCheckBox->isChecked());
    replaceBool(QStringLiteral("bountyDeathPenalty"), bountyDeathPenaltyCheckBox->isChecked());
    replaceBool(QStringLiteral("allowSuicideCommand"), allowSuicideCommandCheckBox->isChecked());
    replaceBool(QStringLiteral("allowFixmeCommand"), allowFixmeCommandCheckBox->isChecked());
    replaceBool(QStringLiteral("allowOnContainerForUnloadedCells"), allowOnContainerForUnloadedCellsCheckBox->isChecked());
    replaceBool(QStringLiteral("enablePlayerCollision"), enablePlayerCollisionCheckBox->isChecked());
    replaceBool(QStringLiteral("enableActorCollision"), enableActorCollisionCheckBox->isChecked());
    replaceBool(QStringLiteral("enablePlacedObjectCollision"), enablePlacedObjectCollisionCheckBox->isChecked());
    replaceBool(QStringLiteral("useActorCollisionForPlacedObjects"), useActorCollisionForPlacedObjectsCheckBox->isChecked());
    replaceBool(QStringLiteral("enforceDataFiles"), enforceDataFilesCheckBox->isChecked());
    replaceBool(QStringLiteral("ignoreScriptErrors"), ignoreScriptErrorsCheckBox->isChecked());

    replaceBool(QStringLiteral("shareJournal"), shareJournalCheckBox->isChecked());
    replaceBool(QStringLiteral("shareFactionRanks"), shareFactionRanksCheckBox->isChecked());
    replaceBool(QStringLiteral("shareFactionExpulsion"), shareFactionExpulsionCheckBox->isChecked());
    replaceBool(QStringLiteral("shareFactionReputation"), shareFactionReputationCheckBox->isChecked());
    replaceBool(QStringLiteral("shareTopics"), shareTopicsCheckBox->isChecked());
    replaceBool(QStringLiteral("shareBounty"), shareBountyCheckBox->isChecked());
    replaceBool(QStringLiteral("shareReputation"), shareReputationCheckBox->isChecked());
    replaceBool(QStringLiteral("shareMapExploration"), shareMapExplorationCheckBox->isChecked());
    replaceBool(QStringLiteral("shareVideos"), shareVideosCheckBox->isChecked());
    replaceBool(QStringLiteral("shareKills"), shareKillsCheckBox->isChecked());

    const QString databaseTypeValue = databaseTypeComboBox->currentData().toString();
    if (!databaseTypeValue.isEmpty())
        replaceString(QStringLiteral("databaseType"), databaseTypeValue);

    return text;
}

void Launcher::PlayPage::loadServerSettings()
{
    const QString runtimePath = serverConfigPath();
    const QString persistentPath = persistentServerConfigPath();
    serverSettingsPathLabel->setText(tr("Persistent: %1  ->  Runtime: %2")
        .arg(QDir::toNativeSeparators(persistentPath), QDir::toNativeSeparators(runtimePath)));

    // The userdata copy is authoritative once it exists. The packaged
    // server/scripts/config.lua can be replaced by an update or deployment,
    // so using it as the only settings store caused user changes to appear to
    // reset to defaults on the next launch.
    QString sourcePath = QFileInfo::exists(persistentPath) ? persistentPath : runtimePath;
    QFile file(sourcePath);
    if (!file.exists())
    {
        serverSettingsEditor->setPlainText(QString());
        setServerSettingsStatus(tr("config.lua not found"), true);
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setServerSettingsStatus(tr("Could not open config.lua"), true);
        return;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    const QString text = stream.readAll();
    file.close();

    serverSettingsEditor->setPlainText(text);
    populateFormFromConfig(text);

    QString syncError;
    const bool persistentOk = sourcePath == persistentPath
        || writeServerConfigFile(persistentPath, text, &syncError);
    const bool runtimeOk = sourcePath == runtimePath
        || writeServerConfigFile(runtimePath, text, &syncError);

    if (!persistentOk || !runtimeOk)
        setServerSettingsStatus(tr("Loaded settings, but could not synchronize the persistent/runtime copy: %1").arg(syncError), true);
    else
        setServerSettingsStatus(tr("Loaded persistent server settings"));
}

bool Launcher::PlayPage::saveServerSettings()
{
    // When the form is visible it is the authoritative editor. If the user
    // switched to Raw, slotServerSettingsModeChanged() has already applied the
    // form values before the switch, so neither mode can silently restore the
    // old/default text on Play/Start Server.
    if (serverSettingsModeTabs->currentWidget() == formServerSettingsTab)
        serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));

    const QString text = serverSettingsEditor->toPlainText();
    QString error;

    if (!writeServerConfigFile(persistentServerConfigPath(), text, &error))
    {
        QMessageBox::warning(this, tr("Save error"), error);
        setServerSettingsStatus(error, true);
        return false;
    }

    if (!writeServerConfigFile(serverConfigPath(), text, &error))
    {
        QMessageBox::warning(this, tr("Save error"), error);
        setServerSettingsStatus(error, true);
        return false;
    }

    setServerSettingsStatus(tr("Saved persistent server settings"));
    return true;
}

void Launcher::PlayPage::slotPlayClicked()
{
    emit playButtonClicked();
}

void Launcher::PlayPage::slotServerClicked()
{
    switchToServerConsoleTab();
    emit serverButtonClicked();
}

void Launcher::PlayPage::slotStopServerClicked()
{
    emit stopServerButtonClicked();
}

void Launcher::PlayPage::slotAutoStartServerToggled(bool enabled)
{
    updateHostModeUi(enabled);
    if (enabled)
        refreshHostInterfaces();
    emit autoStartServerChanged(enabled);
}

void Launcher::PlayPage::slotRefreshHostInterfaces()
{
    refreshHostInterfaces();
}

void Launcher::PlayPage::slotUpdateHashesClicked()
{
    // A freshly generated manifest is only useful when enforcement is enabled.
    if (mEnforceRequiredCheckBox != nullptr && !mEnforceRequiredCheckBox->isChecked())
        mEnforceRequiredCheckBox->setChecked(true);
    else if (!enforceDataFilesCheckBox->isChecked())
        setEnforceDataFiles(true);

    serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
    saveServerSettings();
    emit updateHashesRequested();
}

void Launcher::PlayPage::slotEnforceRequiredToggled(bool enabled)
{
    if (enforceDataFilesCheckBox->isChecked() != enabled)
        enforceDataFilesCheckBox->setChecked(enabled);
    serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
    saveServerSettings();
}

void Launcher::PlayPage::applyServerModePreset(int index)
{
    const bool coop = index == 0;
    gameModeEdit->setText(coop ? QStringLiteral("ArenaMP CO-OP") : QStringLiteral("ArenaMP MMO"));
    shareJournalCheckBox->setChecked(coop);
    shareFactionRanksCheckBox->setChecked(coop);
    shareFactionExpulsionCheckBox->setChecked(coop);
    shareFactionReputationCheckBox->setChecked(coop);
    shareTopicsCheckBox->setChecked(coop);
    // Crime bounty stays personal in both presets.
    shareBountyCheckBox->setChecked(false);
    shareReputationCheckBox->setChecked(coop);
    shareMapExplorationCheckBox->setChecked(coop);
    shareVideosCheckBox->setChecked(coop);
    shareKillsCheckBox->setChecked(coop);
}

void Launcher::PlayPage::slotServerModeChanged(int index)
{
    applyServerModePreset(index);
    serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
    saveServerSettings();
}

void Launcher::PlayPage::slotReloadServerSettings()
{
    loadServerSettings();
}

void Launcher::PlayPage::slotSaveServerSettings()
{
    saveServerSettings();
}


void Launcher::PlayPage::slotServerSettingsModeChanged(int index)
{
    if (mSyncingServerSettingsTabs)
        return;

    mSyncingServerSettingsTabs = true;
    QWidget* target = serverSettingsModeTabs->widget(index);
    if (target == rawServerSettingsTab)
    {
        // Never discard unsaved form edits merely because the user opens Raw.
        serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
        setServerSettingsStatus(tr("Form synchronized to raw config"));
    }
    else if (target == formServerSettingsTab)
    {
        // Likewise, manual Raw edits become the values shown by the form.
        populateFormFromConfig(serverSettingsEditor->toPlainText());
        setServerSettingsStatus(tr("Raw config synchronized to form"));
    }
    mSyncingServerSettingsTabs = false;
}

void Launcher::PlayPage::slotApplyFormToRawConfig()
{
    serverSettingsEditor->setPlainText(updatedConfigFromForm(serverSettingsEditor->toPlainText()));
    setServerSettingsStatus(tr("Form applied to raw config"));
    serverSettingsModeTabs->setCurrentWidget(rawServerSettingsTab);
}

void Launcher::PlayPage::slotSyncFormFromRawConfig()
{
    populateFormFromConfig(serverSettingsEditor->toPlainText());
    setServerSettingsStatus(tr("Form updated from raw config"));
    serverSettingsModeTabs->setCurrentWidget(formServerSettingsTab);
}
