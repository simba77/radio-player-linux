#include "main_window.hpp"

#include "app_info.hpp"
#include "icons.hpp"
#include "now_playing.hpp"
#include "player.hpp"
#include "settings/settings_dialog.hpp"
#include "settings_store.hpp"
#include "station_store.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

// Роль Qt для хранения id станции в элементе списка.
constexpr int STATION_ID_ROLE = Qt::UserRole;
// Размер иконки состояния в строке списка.
constexpr int ROW_ICON_SIZE = 14;

// Пустая иконка того же размера, иначе строки разъезжаются по горизонтали.
const QIcon &blankIcon()
{
    static const QIcon icon = [] {
        QPixmap pixmap(ROW_ICON_SIZE, ROW_ICON_SIZE);
        pixmap.fill(Qt::transparent);
        return QIcon(pixmap);
    }();
    return icon;
}

}  // namespace

TrayClick trayClickAction(bool visible, bool active)
{
    return visible && active ? TrayClick::Hide : TrayClick::Raise;
}

MainWindow::MainWindow(PlayerManager *player, StationStore *store, SettingsStore *settings,
                       CatalogUpdater *updater, QWidget *parent)
    : QWidget(parent), player_(player), store_(store), settings_(settings), updater_(updater),
      nowPlaying_(new NowPlayingWidget(player)), list_(new QListWidget)
{
    setWindowTitle(QLatin1String(APP_NAME));
    setWindowIcon(icons::get(icons::radio()));
    resize(340, 460);

    nowPlaying_->setObjectName(u"nowPlaying"_s);
    list_->setObjectName(u"stationList"_s);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setIconSize(QSize(ROW_ICON_SIZE, ROW_ICON_SIZE));
    list_->setStyleSheet(u"QListWidget { padding: 4px 6px; } QListWidget::item { padding: 5px 6px; }"_s);
    connect(list_, &QListWidget::itemClicked, this, &MainWindow::onItemClicked);

    auto *separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);

    auto *settingsButton = new QPushButton(tr("Settings..."));
    settingsButton->setIcon(icons::get(QLatin1String(icons::SETTINGS)));
    settingsButton->setFlat(true);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(8, 4, 8, 6);
    footer->addWidget(settingsButton);
    footer->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(nowPlaying_);
    layout->addWidget(separator);
    layout->addWidget(list_, 1);
    layout->addLayout(footer);

    connect(store, &StationStore::stationsChanged, this, &MainWindow::reloadStations);
    connect(player, &PlayerManager::stateChanged, this, &MainWindow::refreshRows);
    connect(player, &PlayerManager::stationChanged, this, &MainWindow::refreshRows);

    connect(new QShortcut(QKeySequence(u"P"_s), this), &QShortcut::activated, player, &PlayerManager::toggle);
    connect(new QShortcut(QKeySequence(u"Q"_s), this), &QShortcut::activated, qApp, &QApplication::quit);

    reloadStations();
}

// MARK: - Список станций

void MainWindow::reloadStations()
{
    list_->clear();
    const QList<Station> stations = store_->stations();
    if (stations.isEmpty()) {
        auto *placeholder = new QListWidgetItem(tr("The station list is empty"));
        placeholder->setFlags(Qt::NoItemFlags);
        list_->addItem(placeholder);
        return;
    }
    for (const Station &station : stations) {
        auto *item = new QListWidgetItem(station.name);
        item->setData(STATION_ID_ROLE, station.id);
        item->setToolTip(station.url);
        list_->addItem(item);
    }
    refreshRows();
}

void MainWindow::refreshRows()
{
    const auto current = player_->currentStation();
    // Загрузка и переподключение для строки списка — то же, что игра:
    // иконка не должна прыгать на каждой заминке потока.
    const bool playing = player_->isActive();

    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *item = list_->item(row);
        const QVariant stationId = item->data(STATION_ID_ROLE);
        if (!stationId.isValid())
            continue;

        const bool isCurrent = current && stationId.toString() == current->id;
        QFont font = item->font();
        font.setWeight(isCurrent ? QFont::DemiBold : QFont::Normal);
        item->setFont(font);

        if (isCurrent)
            item->setIcon(icons::get(QLatin1String(playing ? icons::PLAY : icons::STOP)));
        else
            item->setIcon(blankIcon());
    }
}

void MainWindow::onItemClicked(QListWidgetItem *item)
{
    const QVariant stationId = item->data(STATION_ID_ROLE);
    if (!stationId.isValid())
        return;
    const auto station = store_->find(stationId.toString());
    if (!station)
        return;
    const auto current = player_->currentStation();
    if (player_->isActive() && current && current->id == station->id)
        player_->stop();
    else
        player_->play(*station);
}

// MARK: - Окно

void MainWindow::openSettings()
{
    if (settingsDialog_.isNull())
        settingsDialog_ = new SettingsDialog(store_, settings_, updater_, this);
    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
}

SettingsDialog *MainWindow::settingsDialog() const
{
    return settingsDialog_;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (closable_ || !settings_->closeToTray()) {
        event->accept();
        QApplication::quit();
        return;
    }
    event->ignore();
    hide();
}

void MainWindow::toggleVisibility()
{
    qDebug().noquote() << u"клик по трею: visible=%1 active=%2 minimized=%3 token=%4"_s.arg(
        isVisible() ? u"да"_s : u"нет"_s, isActiveWindow() ? u"да"_s : u"нет"_s,
        isMinimized() ? u"да"_s : u"нет"_s,
        qEnvironmentVariableIsEmpty("XDG_ACTIVATION_TOKEN") ? u"нет"_s : u"есть"_s);
    if (trayClickAction(isVisible(), isActiveWindow()) == TrayClick::Hide)
        hide();
    else
        showAndRaise();
}

void MainWindow::showAndRaise(const QString &activationToken)
{
    if (!activationToken.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    showNormal();
    raise();
    activateWindow();
    qunsetenv("XDG_ACTIVATION_TOKEN");
}

}  // namespace radio
