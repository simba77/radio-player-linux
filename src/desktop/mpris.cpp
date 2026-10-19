#include "mpris.hpp"

#include "app_info.hpp"
#include "player.hpp"

#include <QDBusMessage>
#include <QUrl>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

const QString PLAYER_INTERFACE = u"org.mpris.MediaPlayer2.Player"_s;
const QString PROPERTIES_INTERFACE = u"org.freedesktop.DBus.Properties"_s;
// Путь трека обязан быть валидным objectpath; конкретный трек нас не интересует.
const QString TRACK_ID = u"/org/mpris/MediaPlayer2/CurrentTrack"_s;

}  // namespace

QString mprisBusName()
{
    return u"org.mpris.MediaPlayer2."_s + QLatin1String(APP_RDNS_ID);
}

QVariantMap mprisMetadata(const std::optional<Station> &station, const std::optional<TrackInfo> &track,
                          const QString &artworkPath)
{
    QVariantMap metadata{{u"mpris:trackid"_s, QVariant::fromValue(QDBusObjectPath(TRACK_ID))}};
    if (track) {
        metadata.insert(u"xesam:title"_s, track->title);
        metadata.insert(u"xesam:artist"_s,
                        track->artist.isEmpty() ? QStringList() : QStringList{track->artist});
        if (station)
            metadata.insert(u"xesam:album"_s, station->name);
    } else if (station) {
        metadata.insert(u"xesam:title"_s, station->name);
    }
    if (!artworkPath.isEmpty())
        metadata.insert(u"mpris:artUrl"_s, QUrl::fromLocalFile(artworkPath).toString());
    return metadata;
}

// MARK: - org.mpris.MediaPlayer2

MprisRootAdaptor::MprisRootAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}

QString MprisRootAdaptor::identity() const
{
    return QLatin1String(APP_NAME);
}

QString MprisRootAdaptor::desktopEntry() const
{
    return QLatin1String(APP_RDNS_ID);
}

QStringList MprisRootAdaptor::supportedUriSchemes() const
{
    return {u"http"_s, u"https"_s};
}

QStringList MprisRootAdaptor::supportedMimeTypes() const
{
    return {u"audio/mpeg"_s, u"audio/aac"_s, u"application/vnd.apple.mpegurl"_s};
}

void MprisRootAdaptor::Raise()
{
    emit raiseRequested();
}

void MprisRootAdaptor::Quit()
{
    emit quitRequested();
}

// MARK: - org.mpris.MediaPlayer2.Player

MprisPlayerAdaptor::MprisPlayerAdaptor(QObject *parent, PlayerManager *player)
    : QDBusAbstractAdaptor(parent), player_(player)
{
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    return player_->isActive() ? u"Playing"_s : u"Stopped"_s;
}

QVariantMap MprisPlayerAdaptor::metadata() const
{
    return mprisMetadata(player_->currentStation(), player_->currentTrack(), player_->artworkPath());
}

double MprisPlayerAdaptor::volume() const
{
    return player_->volume() / 100.0;
}

void MprisPlayerAdaptor::setVolume(double value)
{
    player_->setVolume(int(std::lround(std::clamp(value, 0.0, 1.0) * 100)));
}

bool MprisPlayerAdaptor::canGoNext() const
{
    return player_->stations().size() > 1;
}

bool MprisPlayerAdaptor::canPlay() const
{
    return !player_->stations().isEmpty();
}

void MprisPlayerAdaptor::Play()
{
    if (!player_->isActive())
        player_->toggle();
}

void MprisPlayerAdaptor::Pause()
{
    player_->stop();
}

void MprisPlayerAdaptor::PlayPause()
{
    player_->toggle();
}

void MprisPlayerAdaptor::Stop()
{
    player_->stop();
}

void MprisPlayerAdaptor::Next()
{
    player_->playNext();
}

void MprisPlayerAdaptor::Previous()
{
    player_->playPrevious();
}

// MARK: - Сервер

MprisServer::MprisServer(PlayerManager *player, const QDBusConnection &connection, QObject *parent)
    : QObject(parent), player_(player), connection_(connection),
      playerAdaptor_(new MprisPlayerAdaptor(this, player))
{
    auto *root = new MprisRootAdaptor(this);
    connect(root, &MprisRootAdaptor::raiseRequested, this, &MprisServer::raiseRequested);
    connect(root, &MprisRootAdaptor::quitRequested, this, &MprisServer::quitRequested);

    connect(player, &PlayerManager::stateChanged, this, &MprisServer::emitPlaybackStatus);
    connect(player, &PlayerManager::volumeChanged, this, &MprisServer::emitVolume);
    connect(player, &PlayerManager::trackChanged, this, &MprisServer::emitMetadata);
    connect(player, &PlayerManager::stationChanged, this, &MprisServer::emitMetadata);
    connect(player, &PlayerManager::artworkChanged, this, &MprisServer::emitMetadata);
}

bool MprisServer::registerService()
{
    if (!connection_.isConnected())
        return false;
    if (!connection_.registerObject(QLatin1String(MPRIS_OBJECT_PATH), this, QDBusConnection::ExportAdaptors))
        return false;
    if (!connection_.registerService(mprisBusName())) {
        connection_.unregisterObject(QLatin1String(MPRIS_OBJECT_PATH));
        return false;
    }
    registered_ = true;
    return true;
}

void MprisServer::unregisterService()
{
    if (!registered_)
        return;
    connection_.unregisterService(mprisBusName());
    connection_.unregisterObject(QLatin1String(MPRIS_OBJECT_PATH));
    registered_ = false;
}

void MprisServer::emitPlaybackStatus()
{
    emitProperties({
        {u"PlaybackStatus"_s, playerAdaptor_->playbackStatus()},
        {u"CanGoNext"_s, playerAdaptor_->canGoNext()},
        {u"CanGoPrevious"_s, playerAdaptor_->canGoNext()},
        {u"CanPlay"_s, playerAdaptor_->canPlay()},
    });
}

void MprisServer::emitMetadata()
{
    emitProperties({{u"Metadata"_s, playerAdaptor_->metadata()}});
}

void MprisServer::emitVolume()
{
    // Без этого слайдер в апплете «Медиаплеер» и в OSD остаётся на старом
    // значении: клиент узнаёт об изменении только из этого сигнала.
    emitProperties({{u"Volume"_s, playerAdaptor_->volume()}});
}

void MprisServer::emitProperties(const QVariantMap &changed)
{
    if (!registered_)
        return;
    QDBusMessage message = QDBusMessage::createSignal(QLatin1String(MPRIS_OBJECT_PATH), PROPERTIES_INTERFACE,
                                                      u"PropertiesChanged"_s);
    // Третий аргумент — список строк (`as`), а не вариантов (`av`): сигнал
    // с сигнатурой `sa{sv}av` Plasma молча игнорирует, и плеер не попадает
    // в апплет. QStringList маршалится как `as` сам — список вариантов сюда нельзя.
    message << PLAYER_INTERFACE << changed << QStringList();
    connection_.send(message);
}

}  // namespace radio
