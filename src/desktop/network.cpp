#include "network.hpp"

#include "player.hpp"

#include <QNetworkInformation>

namespace radio {

bool installNetworkMonitor(PlayerManager *player)
{
    if (!QNetworkInformation::loadDefaultBackend())
        return false;
    QNetworkInformation *info = QNetworkInformation::instance();
    if (info == nullptr)
        return false;
    // Подписка живёт, пока жив плеер: отключать её вручную незачем.
    QObject::connect(info, &QNetworkInformation::reachabilityChanged, player,
                     [player](QNetworkInformation::Reachability reachability) {
                         player->onNetworkReachable(reachability ==
                                                    QNetworkInformation::Reachability::Online);
                     });
    return true;
}

}  // namespace radio
