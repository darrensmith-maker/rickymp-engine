#include <iostream>

#include <QTranslator>
#include <QTextCodec>
#include <QDir>

#include <components/misc/arenarussiantranslator.hpp>

#ifdef MAC_OS_X_VERSION_MIN_REQUIRED
#undef MAC_OS_X_VERSION_MIN_REQUIRED
// We need to do this because of Qt: https://bugreports.qt-project.org/browse/QTBUG-22154
#define MAC_OS_X_VERSION_MIN_REQUIRED __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#endif // MAC_OS_X_VERSION_MIN_REQUIRED

#include "maindialog.hpp"

int main(int argc, char *argv[])
{
    try
    {
        QApplication app(argc, argv);

        // ArenaMW UI language follows the Windows system language. Russian
        // Windows gets the built-in Russian UI; every other locale uses English.
        ArenaUi::RussianTranslator arenaTranslator;
        if (ArenaUi::useRussianSystemUi())
            app.installTranslator(&arenaTranslator);

        // Now we make sure the current dir is set to application path
        QDir dir(QCoreApplication::applicationDirPath());

        QDir::setCurrent(dir.absolutePath());

        Launcher::MainDialog mainWin;

        Launcher::FirstRunDialogResult result = mainWin.showFirstRunDialog();
        if (result == Launcher::FirstRunDialogResultFailure)
            return 0;

        if (result == Launcher::FirstRunDialogResultContinue)
            mainWin.show();

        int exitCode = app.exec();

        return exitCode;
    }
    catch (std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 0;
    }
}
