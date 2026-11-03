#ifndef ADVANCEDPAGE_H
#define ADVANCEDPAGE_H

#include <QCompleter>
#include <QStringListModel>

#include "ui_advancedpage.h"

#include <components/settings/settings.hpp>

namespace Config { class GameSettings; }

namespace Launcher
{
    class AdvancedPage : public QWidget, private Ui::AdvancedPage
    {
        Q_OBJECT

    public:
        explicit AdvancedPage(Config::GameSettings &gameSettings, QWidget *parent = nullptr);

        bool loadSettings();
        void saveSettings();
        void setGameMechanicsVisible(bool visible);

    public slots:
        void slotLoadedCellsChanged(QStringList cellNames);

    private slots:
        void on_skipMenuCheckBox_stateChanged(int state);
        void on_runScriptAfterStartupBrowseButton_clicked();
        void slotAnimSourcesToggled(bool checked);
        void slotViewOverShoulderToggled(bool checked);
        void slotApplyOsgPreset();
        void slotOsgPresetSelectionChanged();
        void slotOsgPatchControlChanged();

    private:
        Config::GameSettings &mGameSettings;
        QCompleter mCellNameCompleter;
        QStringListModel mCellNameCompleterModel;

        /**
         * Load the cells associated with the given content files for use in autocomplete
         * @param filePaths the file paths of the content files to be examined
         */
        void loadCellsForAutocomplete(QStringList filePaths);
        void loadSettingBool(QCheckBox *checkbox, const std::string& setting, const std::string& group);
        void saveSettingBool(QCheckBox *checkbox, const std::string& setting, const std::string& group);
        void applyOsgPreset(const QString& presetName);
        QString currentOsgPreset() const;
        void updateOsgPresetDescription(const QString& presetName);

        bool mUpdatingOsgPresetUi = false;
    };
}
#endif
