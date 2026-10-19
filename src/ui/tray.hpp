#pragma once

// Иконка в системном лотке.
//
// На KDE Wayland QSystemTrayIcon уходит через StatusNotifierItem. В GNOME без расширения
// AppIndicator лотка нет вообще — приложение должно оставаться пригодным и без него,
// поэтому трей здесь только дополняет окно, но ничем не владеет.

#include <QSystemTrayIcon>

#include <memory>

class QAction;
class QMenu;

namespace radio {

class PlayerManager;

class TrayIcon : public QSystemTrayIcon {
    Q_OBJECT
public:
    explicit TrayIcon(PlayerManager *player, QObject *parent = nullptr);
    ~TrayIcon() override;

signals:
    // Клик по иконке: спрятать или показать окно.
    void toggleWindowRequested();
    void settingsRequested();
    void quitRequested();

private:
    void refresh();
    // Во время переподключения иконка мигает.
    void setIconActive(bool active);
    QString tooltip() const;

    PlayerManager *player_;
    // Меню без родителя: QSystemTrayIcon им не владеет.
    std::unique_ptr<QMenu> menu_;
    QAction *playAction_;
};

}  // namespace radio
