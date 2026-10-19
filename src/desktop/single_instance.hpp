#pragma once

// Single-instance через QLocalServer.
//
// Второй запуск не поднимает новое окно, а просит первый показать своё.
// Вместе с просьбой передаётся токен активации. На Wayland поднять и сфокусировать
// окно чужому процессу нельзя — композитор пускает вперёд только того, кому токен
// выдан. Токен есть у запускаемого процесса (`XDG_ACTIVATION_TOKEN`, на X11 —
// `DESKTOP_STARTUP_ID`), а окно принадлежит первому: без передачи окно в лучшем
// случае мигнёт в панели задач.

#include <QByteArray>
#include <QObject>
#include <QString>

#include <optional>

class QLocalServer;
class QLocalSocket;

namespace radio {

namespace instance {

// Токен, выданный окружением этому запуску; пусто при запуске из терминала.
QString activationToken();
QByteArray encodeRequest(const QString &token);
// Токен из запроса или пусто, если это не наша команда. Токена может не быть —
// тогда возвращается пустая строка.
std::optional<QString> decodeRequest(const QByteArray &data);

// Если приложение уже запущено — просит его показать окно.
//
// True, когда живой экземпляр найден и уведомлён: в этом случае текущий процесс
// должен молча завершиться. Работает и до создания QApplication.
bool notifyRunningInstance();

}  // namespace instance

// Слушает попытки повторного запуска.
class InstanceServer : public QObject {
    Q_OBJECT
public:
    explicit InstanceServer(QObject *parent = nullptr);

    void close();

signals:
    // Второй запуск просит показать окно; токен может быть пустым.
    void showRequested(const QString &activationToken);

private:
    void handleConnection();
    void handleRequest(QLocalSocket *connection);

    QLocalServer *server_;
};

}  // namespace radio
