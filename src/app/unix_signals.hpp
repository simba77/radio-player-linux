#pragma once

// SIGTERM и SIGINT как сигнал Qt.
//
// SIGTERM приходит при завершении сеанса и `systemctl --user stop`: без
// обработки процесс умирает мимо `aboutToQuit`, не сняв имя на шине и не
// остановив mpv. Ctrl+C (SIGINT) — то же самое из терминала.
//
// Из обработчика сигнала звать Qt нельзя, поэтому он только пишет байт в пару
// сокетов, а читает её QSocketNotifier уже в цикле событий.

#include <QObject>

class QSocketNotifier;

namespace radio {

class UnixSignals : public QObject {
    Q_OBJECT
public:
    explicit UnixSignals(QObject *parent = nullptr);
    ~UnixSignals() override;

signals:
    void terminationRequested();

private:
    void onActivated();

    QSocketNotifier *notifier_ = nullptr;
};

}  // namespace radio
