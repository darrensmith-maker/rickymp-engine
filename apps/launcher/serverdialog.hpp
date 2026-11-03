#ifndef LAUNCHER_SERVERDIALOG_HPP
#define LAUNCHER_SERVERDIALOG_HPP

#include <QWidget>
#include <QByteArray>
#include <QProcess>
#include <QString>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QTextCodec;

namespace Launcher
{
    class ServerDialog : public QWidget
    {
        Q_OBJECT

    public:
        explicit ServerDialog(QWidget* parent = nullptr);
        ~ServerDialog();

        bool startServer();
        bool isRunning() const;
        bool isServerReachable(int timeoutMs = 300) const;
        QString displayAddress() const;
        QString configuredPort() const;
        bool setConfiguredPort(const QString& port, QString* errorMessage = nullptr);
        bool setConfiguredLocalAddress(const QString& address, QString* errorMessage = nullptr);
        QString localConnectAddress() const;
        QString requiredDataFilesPath() const;
        bool autoRestartEnabled() const;
        void setAutoRestartEnabled(bool enabled);
        bool clearPersistentCells(QString* errorMessage = nullptr) const;
        bool resetPersistentServerData(QString* errorMessage = nullptr) const;

    public slots:
        void stopServer();

    signals:
        void runningChanged(bool running, const QString& address, const QString& port);
        void autoRestartChanged(bool enabled);

    private slots:
        void processReadyReadStandardOutput();
        void processReadyReadStandardError();
        void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
        void processError(QProcess::ProcessError error);
        void backupProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
        void backupProcessError(QProcess::ProcessError error);
        void refreshDecodedLog();

    private:
        struct ServerConfig
        {
            QString localAddress;
            QString port;
            QString serverHomePath;
            QString configPath;
        };

        void appendRawLog(const QByteArray& data);
        void updateLogView();
        QTextCodec* currentCodec() const;
        ServerConfig readServerConfig() const;
        bool preparePortableServer(QString* errorMessage = nullptr) const;
        QString resolveServerExecutable() const;
        QString resolveDisplayAddress(const QString& bindAddress) const;
        QString applicationBasePath() const;
        QString serverRuntimeBasePath() const;
        QString runtimeDataBasePath() const;
        QString backupDirectoryPath() const;
        QString makeBackupArchivePath() const;
        bool startBackupArchive(QString* errorMessage = nullptr);
        void finishServerStopSequence();
        void cleanupOldLogsIfNeeded();
        void appendStatusLine(const QString& text);

        QLabel* mAddressLabel;
        QLabel* mPortLabel;
        QLabel* mEncodingLabel;
        QComboBox* mEncodingCombo;
        QCheckBox* mRestartCheckBox;
        QPlainTextEdit* mLogView;
        QPushButton* mStopButton;
        QPushButton* mCloseButton;
        QProcess* mProcess;
        QProcess* mBackupProcess;
        QByteArray mRawLog;
        QString mPendingBackupPath;
        int mRestartCounter;
        bool mStopRequested;
        bool mBackupInProgress;
        bool mRestartAfterBackup;
        bool mCrashAfterStop;
        int mRapidCrashCount;
        qint64 mLastStartMs;
        mutable QString mCachedDisplayAddress;
        mutable qint64 mCachedDisplayAddressAtMs;
    };
}

#endif
