#include "importpage.hpp"

#include "mainwizard.hpp"

#include <QDir>
#include <QFileInfo>

Wizard::ImportPage::ImportPage(QWidget *parent) :
    QWizardPage(parent)
{
    mWizard = qobject_cast<MainWizard*>(parent);

    setupUi(this);

    registerField(QLatin1String("installation.import-settings"), importCheckBox);
    registerField(QLatin1String("installation.import-addons"), addonsCheckBox);
}


void Wizard::ImportPage::initializePage()
{
    const QString path = field(QLatin1String("installation.path")).toString();
    const QString key = QDir::toNativeSeparators(path);
    const QString iniPath = mWizard->mInstallations.value(key).iniPath;
    const bool hasIni = !iniPath.isEmpty() && QFileInfo::exists(iniPath);

    importCheckBox->setEnabled(hasIni);
    addonsCheckBox->setEnabled(hasIni);
    if (!hasIni)
    {
        importCheckBox->setChecked(false);
        addonsCheckBox->setChecked(false);
        infoLabel->setText(tr("Morrowind.ini was not found. This is allowed: the selected Data Files folder, existing base ESM files and all BSA archives will be configured directly."));
    }
    else
    {
        infoLabel->setText(tr("Morrowind.ini was found. Importing legacy settings and plugin selection is optional; detected base ESM and BSA files are configured automatically in either case."));
    }
}

int Wizard::ImportPage::nextId() const
{
    return MainWizard::Page_Conclusion;
}
