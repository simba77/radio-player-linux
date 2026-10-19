#pragma once

// HTTP-сервер для тестов: отвечает заданным содержимым на 127.0.0.1.
//
// Сетевой код проверяется настоящими запросами, а не подменой
// QNetworkAccessManager: ETag, коды ответа и битые тела проходят тот же путь,
// что и в работе. Сервер живёт в потоке теста, поэтому ответы приходят, пока
// тест ждёт сигнала (`QSignalSpy::wait`) и крутит цикл событий.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QTcpServer>

namespace radio::test {

class HttpServer : public QObject {
    Q_OBJECT
public:
    struct Response {
        int status = 200;
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
    };

    // Заголовки одного полученного запроса; имена — в нижнем регистре.
    using Request = QHash<QByteArray, QByteArray>;

    HttpServer();

    // Полный адрес пути на этом сервере.
    QString url(const QString &path) const;
    // Что отвечать на путь. Незнакомый путь получает 404.
    void respond(const QString &path, const Response &response);
    // Запросы, пришедшие на путь, по порядку.
    QList<Request> requests(const QString &path) const { return requests_.value(path); }

private:
    void serve(QTcpSocket *socket);

    QTcpServer server_;
    QHash<QString, Response> responses_;
    QHash<QString, QList<Request>> requests_;
};

}  // namespace radio::test
