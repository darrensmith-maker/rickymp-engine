#include "existinginstallationpage.hpp"

#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QStringList>

#include <components/config/buildmanifest.hpp>

#include "mainwizard.hpp"

Wizard::ExistingInstallationPage::ExistingInstallationPage(QWidget *parent) :
    QWizardPage(parent)
{
    mWizard = qobject_cast<MainWizard*>(parent);

    setupUi(this);

    // Add a placeholder item to the list of installations
    QListWidgetItem *emptyItem = new QListWidgetItem(tr("No existing Data Files folders detected"));
    emptyItem->setFlags(Qt::NoItemFlags);

    installationsList->insertItem(0, emptyItem);

}

void Wizard::ExistingInstallationPage::initializePage()
{
    // Add the available installation paths
    QStringList paths(mWizard->mInstallations.keys());

    // Hide the default item if there are installations to choose from
    installationsList->item(0)->setHidden(!paths.isEmpty());

    for (const QString &path : paths)
    {
        if (installationsList->findItems(path, Qt::MatchExactly).isEmpty())
        {
            QListWidgetItem *item = new QListWidgetItem(path);
            installationsList->addItem(item);
        }
    }

    connect(installationsList, SIGNAL(currentTextChanged(QString)),
            this, SLOT(textChanged(QString)));

    connect(installationsList,SIGNAL(itemSelectionChanged()),
            this, SIGNAL(completeChanged()));
}

bool Wizard::ExistingInstallationPage::validatePage()
{
    const QString path = field(QLatin1String("installation.path")).toString();
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir())
    {
        QMessageBox::warning(this, tr("Invalid Data Files folder"),
            tr("The selected Data Files folder does not exist or is not a directory."));
        return false;
    }

    // Morrowind.ini, Morrowind.esm and Morrowind.bsa are not mandatory at this
    // stage. Existing base masters and all BSA archives are detected separately.
    return true;
}

void Wizard::ExistingInstallationPage::on_browseButton_clicked()
{
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        tr("Select the Morrowind Data Files folder"),
        QDir::currentPath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (selectedDirectory.isEmpty())
        return;

    const QFileInfo info(selectedDirectory);
    if (!info.exists() || !info.isDir())
        return;

    const QString path = QDir::toNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
    QList<QListWidgetItem*> items = installationsList->findItems(path, Qt::MatchExactly);

    if (items.isEmpty())
    {
        mWizard->addInstallation(path);
        installationsList->item(0)->setHidden(true);

        QListWidgetItem *item = new QListWidgetItem(path);
        installationsList->addItem(item);
        installationsList->setCurrentItem(item);
    }
    else
    {
        installationsList->setCurrentItem(items.first());
    }

    updateDetectedFiles(path);
    emit completeChanged();
}

void Wizard::ExistingInstallationPage::textChanged(const QString &text)
{
    // Set the installation path manually, as registerField doesn't work
    // Because it doesn't accept two widgets operating on a single field
    if (!text.isEmpty())
    {
        mWizard->setField(QLatin1String("installation.path"), text);
        mWizard->configureDataFiles(text);
        updateDetectedFiles(text);
    }
}

void Wizard::ExistingInstallationPage::updateDetectedFiles(const QString& path)
{
    QDir dir(path);
    if (!dir.exists())
    {
        detectedFilesLabel->clear();
        return;
    }

    QStringList masters;
    QStringList archives;
    const QStringList files = dir.entryList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QString& fileName : files)
    {
        if (fileName.compare(QStringLiteral("Morrowind.esm"), Qt::CaseInsensitive) == 0
            || fileName.compare(QStringLiteral("Tribunal.esm"), Qt::CaseInsensitive) == 0
            || fileName.compare(QStringLiteral("Bloodmoon.esm"), Qt::CaseInsensitive) == 0)
            masters.append(fileName);
        else if (fileName.endsWith(QLatin1String(".bsa"), Qt::CaseInsensitive))
            archives.append(fileName);
    }

    QString summary;
    const QString manifestPath = Config::BuildManifest::findForDataDir(path);
    if (!manifestPath.isEmpty())
    {
        Config::BuildManifest manifest;
        if (manifest.read(manifestPath))
        {
            summary = tr("build.ini detected: %1. Plugins: %2, groundcover: %3, BSA archives: %4, server: %5:%6. The stored order will be applied automatically.")
                .arg(manifest.buildName)
                .arg(manifest.contentFiles.size())
                .arg(manifest.groundcoverFiles.size())
                .arg(manifest.archives.size())
                .arg(manifest.serverAddress)
                .arg(manifest.serverPort);
            detectedFilesLabel->setText(summary);
            return;
        }
    }

    summary = tr("Base masters found: %1. BSA archives found: %2.")
        .arg(masters.isEmpty() ? tr("none") : masters.join(QStringLiteral(", ")))
        .arg(archives.size());
    if (masters.isEmpty())
        summary += tr(" You can continue, but the launcher will require at least one ESM before starting the game.");
    else
        summary += tr(" Existing base ESM files will be enabled automatically. Base BSA files load first, followed by all other BSA files alphabetically.");

    detectedFilesLabel->setText(summary);
}

bool Wizard::ExistingInstallationPage::isComplete() const
{
    if (installationsList->selectionModel()->hasSelection()) {
        return true;
    } else {
        return false;
    }
}

int Wizard::ExistingInstallationPage::nextId() const
{
    const QString path = field(QLatin1String("installation.path")).toString();
    const QString manifestPath = Config::BuildManifest::findForDataDir(path);
    if (!manifestPath.isEmpty())
    {
        Config::BuildManifest manifest;
        if (manifest.read(manifestPath) && manifest.languageSpecified)
        {
            // The build author already selected the encoding/language. Apply the
            // canonical spelling (for example russian -> Russian) and skip the
            // redundant page without letting its English default take over.
            mWizard->setField(QLatin1String("installation.language"),
                Config::BuildManifest::canonicalLanguage(manifest.language));

            const QString nativePath = QDir::toNativeSeparators(QDir::cleanPath(path));
            const auto installation = mWizard->mInstallations.constFind(nativePath);
            if (installation != mWizard->mInstallations.constEnd()
                && installation->hasMorrowind
                && installation->hasTribunal
                && installation->hasBloodmoon)
            {
                return MainWizard::Page_Import;
            }
            return MainWizard::Page_ComponentSelection;
        }
    }

    return MainWizard::Page_LanguageSelection;
}
