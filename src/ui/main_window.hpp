#pragma once

// Главное окно: «Сейчас играет» сверху, список станций снизу.

#include <QPointer>
#include <QWidget>

class QListWidget;
class QListWidgetItem;

namespace radio {

class CatalogUpdater;
class NowPlayingWidget;
class PlayerManager;
class SettingsDialog;
class SettingsStore;
class StationStore;

// Что делать по клику на иконку в трее.
enum class TrayClick { Hide, Raise };

// Спрятать окно, если с ним работают, иначе показать.
//
// Ориентироваться на свёрнутость нельзя: в xdg-shell нет состояния «свёрнуто»,
// о сворачивании клиенту не сообщают, и на Wayland свёрнутое окно выглядит для
// Qt как обычное видимое (проверено на Plasma 6: `isVisible()` true,
// `isMinimized()` false даже сразу после `showMinimized()`).
//
// Активность окна композитор сообщает честно, и она же отвечает вопросу
// пользователя: спрятать имеет смысл то, что сейчас перед глазами, а
// свёрнутое или перекрытое чужим окном по клику должно подниматься.
TrayClick trayClickAction(bool visible, bool active);

class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(PlayerManager *player, StationStore *store, SettingsStore *settings, CatalogUpdater *updater,
               QWidget *parent = nullptr);

    // Разрешает закрытие окна: без трея прятать его некуда.
    void allowClose() { closable_ = true; }
    // Показывает окно настроек, переиспользуя уже открытое.
    void openSettings();
    SettingsDialog *settingsDialog() const;
    // Клик по иконке в трее.
    void toggleVisibility();
    // Показывает окно и переводит на него фокус.
    //
    // Токен приходит от второго запуска приложения: на Wayland композитор
    // отдаёт фокус только по нему. Qt читает токен из окружения в момент
    // активации, поэтому переменная ставится прямо перед ней и сразу убирается —
    // чужой токен, оставленный в окружении, был бы уже недействителен.
    // Без токена (X11, запуск из терминала) окно просто поднимется.
    //
    // Свёрнутое окно возвращает именно активация: развернуть себя клиент на
    // Wayland не может, а композитор, активируя окно, разворачивает его сам
    // (проверено скриптом KWin). Прятать и показывать окно заново для этого
    // нельзя: KWin запоминает свёрнутость по приложению и вернёт её новому
    // окну — оно мелькнёт и исчезнет.
    void showAndRaise(const QString &activationToken = {});

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void reloadStations();
    // Отмечает активную станцию иконкой и полужирным начертанием.
    void refreshRows();
    void onItemClicked(QListWidgetItem *item);

    PlayerManager *player_;
    StationStore *store_;
    SettingsStore *settings_;
    CatalogUpdater *updater_;
    NowPlayingWidget *nowPlaying_;
    QListWidget *list_;
    QPointer<SettingsDialog> settingsDialog_;
    bool closable_ = false;
};

}  // namespace radio
