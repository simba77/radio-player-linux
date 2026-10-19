#include "settings/settings_dialog.hpp"

#include "settings/catalogs_tab.hpp"
#include "settings/general_tab.hpp"
#include "settings/stations_tab.hpp"

#include <QTabWidget>
#include <QVBoxLayout>

namespace radio {

SettingsDialog::SettingsDialog(StationStore *store, SettingsStore *settings, CatalogUpdater *updater,
                               QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    resize(460, 460);

    auto *tabs = new QTabWidget;
    tabs->addTab(new GeneralTab(settings), tr("General"));
    tabs->addTab(new StationsTab(store), tr("Stations"));
    tabs->addTab(new CatalogsTab(settings, updater), tr("Catalogs"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
}

}  // namespace radio
