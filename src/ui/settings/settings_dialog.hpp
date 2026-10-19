#pragma once

// Окно настроек: три вкладки и ничего своего.

#include <QDialog>

namespace radio {

class CatalogUpdater;
class SettingsStore;
class StationStore;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(StationStore *store, SettingsStore *settings, CatalogUpdater *updater,
                   QWidget *parent = nullptr);
};

}  // namespace radio
