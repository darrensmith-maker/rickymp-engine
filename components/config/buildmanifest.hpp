#ifndef BUILDMANIFEST_HPP
#define BUILDMANIFEST_HPP

#include <QString>
#include <QStringList>

namespace Config
{
    /// Portable ArenaMP build description stored next to a Data Files folder.
    /// The file intentionally keeps ordered, repeated content/archive entries.
    class BuildManifest
    {
    public:
        BuildManifest();

        void clear();

        bool read(const QString& filePath, QString* errorMessage = nullptr);
        bool write(const QString& filePath, QString* errorMessage = nullptr) const;

        QString resolvedDataPath(const QString& manifestPath) const;

        static QString canonicalPathForDataDir(const QString& dataDir);
        static QString findForDataDir(const QString& dataDir);
        static QString portableDataPath(const QString& manifestPath, const QString& dataDir);
        static QString canonicalLanguage(const QString& language);

        int formatVersion;
        QString buildName;
        QString dataPath;
        QString language;
        bool languageSpecified;
        QString serverAddress;
        bool serverAddressSpecified;
        QString serverPort;
        bool serverPortSpecified;
        bool vanillaServerCompatibility;
        bool complete;
        QStringList contentFiles;
        QStringList groundcoverFiles;
        QStringList archives;
    };
}

#endif
