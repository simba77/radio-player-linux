#include "http_server.hpp"

#include <QTcpSocket>

#include <catch2/catch_test_macros.hpp>

namespace radio::test {

HttpServer::HttpServer()
{
    REQUIRE(server_.listen(QHostAddress::LocalHost));
    connect(&server_, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = server_.nextPendingConnection()) {
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { serve(socket); });
        }
    });
}

QString HttpServer::url(const QString &path) const
{
    return QStringLiteral("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(path);
}

void HttpServer::respond(const QString &path, const Response &response)
{
    responses_.insert(path, response);
}

void HttpServer::serve(QTcpSocket *socket)
{
    // Запрос целиком умещается в один пакет — тела у GET нет, ждём конца заголовков.
    QByteArray head = socket->property("head").toByteArray() + socket->readAll();
    socket->setProperty("head", head);
    if (!head.contains("\r\n\r\n"))
        return;

    const QList<QByteArray> lines = head.left(head.indexOf("\r\n\r\n")).split('\n');
    const QString path = QString::fromLatin1(lines.value(0).split(' ').value(1));
    Request request;
    for (qsizetype index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines[index].trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            request.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }
    requests_[path].append(request);

    const Response response = responses_.value(path, Response{404, "not found", {}});
    QByteArray reply = "HTTP/1.1 " + QByteArray::number(response.status) + " Test\r\n";
    // У 304 тела нет по стандарту, и Content-Length ему не нужен.
    if (response.status != 304)
        reply += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    reply += "Connection: close\r\n";
    for (const auto &[name, value] : response.headers)
        reply += name + ": " + value + "\r\n";
    reply += "\r\n";
    if (response.status != 304)
        reply += response.body;
    socket->write(reply);
    socket->disconnectFromHost();
}

}  // namespace radio::test
