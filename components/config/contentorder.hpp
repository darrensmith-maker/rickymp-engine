#ifndef OPENMW_COMPONENTS_CONFIG_CONTENTORDER_HPP
#define OPENMW_COMPONENTS_CONFIG_CONTENTORDER_HPP

#include <QDir>
#include <QStringList>

namespace Config
{
    inline const QStringList& canonicalContentOrder()
    {
        static const QStringList order = {
            QStringLiteral("Morrowind.esm"),
            QStringLiteral("Tribunal.esm"),
            QStringLiteral("Bloodmoon.esm"),
            QStringLiteral("GFM.esm"),
            QStringLiteral("Rebirth_Main.esm"),
            QStringLiteral("OAAB_Data.esm"),
            QStringLiteral("MFR.esm"),
            QStringLiteral("Tamriel_Data.esm"),
            QStringLiteral("TR_Mainland.esm"),
            QStringLiteral("Cyr_Main.esm"),
            QStringLiteral("Sky_Main.esm"),
            QStringLiteral("Wares-base.esm"),
            QStringLiteral("NOD_Core.esm"),
            QStringLiteral("TDoO_Main.esm"),
            QStringLiteral("Nirn_Core.esp"),
            QStringLiteral("TR_Factions.esp"),
            QStringLiteral("MFR_TR_patch.esp")
        };
        return order;
    }

    inline bool containsContentFile(const QStringList& files, const QString& requested)
    {
        for (const QString& file : files)
        {
            if (file.compare(requested, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    }

    inline QString findContentFile(const QStringList& files, const QString& requested)
    {
        for (const QString& file : files)
        {
            if (file.compare(requested, Qt::CaseInsensitive) == 0)
                return file;
        }
        return QString();
    }

    inline bool isCanonicalContentFile(const QString& fileName)
    {
        return containsContentFile(canonicalContentOrder(), fileName);
    }

    inline QStringList applyCanonicalContentOrderFromFiles(
        const QStringList& existing, const QStringList& available)
    {
        QStringList ordered;
        for (const QString& requested : canonicalContentOrder())
        {
            QString actual = findContentFile(available, requested);
            if (actual.isEmpty())
                actual = findContentFile(existing, requested);
            if (!actual.isEmpty() && !containsContentFile(ordered, actual))
                ordered.append(actual);
        }

        for (const QString& file : existing)
        {
            if (!isCanonicalContentFile(file) && !containsContentFile(ordered, file))
                ordered.append(file);
        }
        return ordered;
    }

    inline QStringList applyCanonicalContentOrder(const QStringList& existing, const QDir& dataDir)
    {
        const QStringList available = dataDir.exists()
            ? dataDir.entryList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase)
            : QStringList();
        return applyCanonicalContentOrderFromFiles(existing, available);
    }

    inline QStringList applyCanonicalContentOrder(const QStringList& existing)
    {
        return applyCanonicalContentOrderFromFiles(existing, QStringList());
    }
}

#endif
