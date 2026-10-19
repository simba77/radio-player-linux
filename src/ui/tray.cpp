#include "tray.hpp"

#include "app_info.hpp"
#include "icons.hpp"
#include "player.hpp"

#include <QMenu>

using namespace Qt::StringLiterals;

namespace radio {

TrayIcon::TrayIcon(PlayerManager *player, QObject *parent)
    : QSystemTrayIcon(parent), player_(player), menu_(std::make_unique<QMenu>())
{
    setToolTip(QLatin1String(APP_NAME));

    playAction_ = menu_->addAction(tr("Play"));
    playAction_->setObjectName(u"playAction"_s);
    connect(playAction_, &QAction::triggered, player, &PlayerManager::toggle);
    connect(menu_->addAction(tr("Next station")), &QAction::triggered, player, &PlayerManager::playNext);
    connect(menu_->addAction(tr("Previous station")), &QAction::triggered, player,
            &PlayerManager::playPrevious);
    menu_->addSeparator();
    connect(menu_->addAction(tr("Show window")), &QAction::triggered, this, &TrayIcon::toggleWindowRequested);
    connect(menu_->addAction(tr("Settings...")), &QAction::triggered, this, &TrayIcon::settingsRequested);
    menu_->addSeparator();
    connect(menu_->addAction(tr("Quit")), &QAction::triggered, this, &TrayIcon::quitRequested);
    setContextMenu(menu_.get());

    connect(this, &QSystemTrayIcon::activated, this, [this](ActivationReason reason) {
        if (reason == Trigger)
            emit toggleWindowRequested();
    });
    connect(player, &PlayerManager::stateChanged, this, &TrayIcon::refresh);
    connect(player, &PlayerManager::trackChanged, this, &TrayIcon::refresh);
    connect(player, &PlayerManager::blinkToggled, this, &TrayIcon::setIconActive);
    refresh();
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::refresh()
{
    setIconActive(player_->isPlaying());
    playAction_->setText(player_->isActive() ? tr("Stop") : tr("Play"));
    setToolTip(tooltip());
}

void TrayIcon::setIconActive(bool active)
{
    setIcon(QIcon::fromTheme(QLatin1String(active ? icons::PLAY : icons::STOP)));
}

QString TrayIcon::tooltip() const
{
    if (player_->isReconnecting())
        return tr("%1 — reconnecting...").arg(QLatin1String(APP_NAME));
    if (const auto track = player_->currentTrack())
        return track->display();
    if (const auto station = player_->currentStation(); station && player_->isPlaying())
        return station->name;
    return QLatin1String(APP_NAME);
}

}  // namespace radio
