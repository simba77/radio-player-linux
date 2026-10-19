#include "unix_signals.hpp"

#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace radio {

namespace {

// Пара сокетов общая на процесс: обработчик сигнала не знает об объектах.
int g_sockets[2] = {-1, -1};

void handleSignal(int)
{
    const char byte = 1;
    // Результат проверять негде и незачем: потерянный байт значит лишь, что
    // предыдущий ещё не прочитан, — выход и так уже запрошен.
    [[maybe_unused]] const auto written = ::write(g_sockets[0], &byte, 1);
}

}  // namespace

UnixSignals::UnixSignals(QObject *parent) : QObject(parent)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_sockets) != 0)
        return;
    notifier_ = new QSocketNotifier(g_sockets[1], QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &UnixSignals::onActivated);

    struct sigaction action{};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
}

UnixSignals::~UnixSignals()
{
    if (notifier_ == nullptr)
        return;
    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGINT, SIG_DFL);
    for (int &socket : g_sockets)
        ::close(std::exchange(socket, -1));
}

void UnixSignals::onActivated()
{
    char byte = 0;
    [[maybe_unused]] const auto read = ::read(g_sockets[1], &byte, 1);
    emit terminationRequested();
}

}  // namespace radio
