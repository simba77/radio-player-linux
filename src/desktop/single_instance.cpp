#include "single_instance.hpp"

#include "paths.hpp"

#include <QLocalServer>
#include <QLocalSocket>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

// Команда второго экземпляра: показать окно, вот токен активации (может быть пуст).
const QByteArray SHOW_COMMAND = "show";
constexpr int CONNECT_TIMEOUT_MS = 500;

}  // namespace

namespace instance {

QString activationToken()
{
    const QString wayland = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    return wayland.isEmpty() ? qEnvironmentVariable("DESKTOP_STARTUP_ID") : wayland;
}

QByteArray encodeRequest(const QString &token)
{
    return SHOW_COMMAND + ' ' + token.toUtf8() + '\n';
}

std::optional<QString> decodeRequest(const QByteArray &data)
{
    const QString text = QString::fromUtf8(data).trimmed();
    if (!text.startsWith(QLatin1String(SHOW_COMMAND)))
        return std::nullopt;
    return text.mid(SHOW_COMMAND.size()).trimmed();
}

bool notifyRunningInstance()
{
    QLocalSocket socket;
    socket.connectToServer(paths::ipcSocket());
    if (!socket.waitForConnected(CONNECT_TIMEOUT_MS))
        return false;
    socket.write(encodeRequest(activationToken()));
    socket.waitForBytesWritten(CONNECT_TIMEOUT_MS);
    socket.disconnectFromServer();
    return true;
}

}  // namespace instance

InstanceServer::InstanceServer(QObject *parent) : QObject(parent), server_(new QLocalServer(this))
{
    // Осиротевший сокет остаётся после аварийного завершения и блокирует listen().
    QLocalServer::removeServer(paths::ipcSocket());
    connect(server_, &QLocalServer::newConnection, this, &InstanceServer::handleConnection);
    server_->listen(paths::ipcSocket());
}

void InstanceServer::close()
{
    server_->close();
    QLocalServer::removeServer(paths::ipcSocket());
}

void InstanceServer::handleConnection()
{
    while (QLocalSocket *connection = server_->nextPendingConnection()) {
        connect(connection, &QLocalSocket::disconnected, connection, &QObject::deleteLater);
        // Данные приходят отдельным событием: показывать окно нужно после них,
        // иначе токен активации опоздает и фокус не перейдёт.
        connect(connection, &QLocalSocket::readyRead, this,
                [this, connection] { handleRequest(connection); });
    }
}

void InstanceServer::handleRequest(QLocalSocket *connection)
{
    if (const auto token = instance::decodeRequest(connection->readAll()))
        emit showRequested(*token);
}

}  // namespace radio
