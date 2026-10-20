#include "player.hpp"

#include "artwork.hpp"
#include "json_values.hpp"
#include "metadata/provider.hpp"
#include "station_store.hpp"
#include "storage.hpp"

#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

QTimer *makeTimer(QObject *parent, const QString &name, int intervalMs, bool singleShot)
{
    auto *timer = new QTimer(parent);
    // По имени таймер находят тесты: срабатывание имитируется, а не ждётся.
    timer->setObjectName(name);
    timer->setInterval(intervalMs);
    timer->setSingleShot(singleShot);
    return timer;
}

}  // namespace

int withJitter(int intervalMs)
{
    const double spread = intervalMs * RECONNECT_JITTER;
    const double value = intervalMs - spread + 2 * spread * QRandomGenerator::global()->generateDouble();
    return std::max(1, int(value));
}

PlayerManager::PlayerManager(StationStore *store, MpvBackend *mpv, QObject *parent)
    : QObject(parent), store_(store), mpv_(mpv), artwork_(new ArtworkLoader(this)),
      reconnectTimer_(makeTimer(this, u"reconnectTimer"_s, RECONNECT_INTERVAL_MS, false)),
      stallTimer_(makeTimer(this, u"stallTimer"_s, STALL_TIMEOUT_MS, true)),
      blinkTimer_(makeTimer(this, u"blinkTimer"_s, BLINK_INTERVAL_MS, false)),
      volumeSaveTimer_(makeTimer(this, u"volumeSaveTimer"_s, VOLUME_SAVE_DELAY_MS, true)),
      networkReturnTimer_(makeTimer(this, u"networkReturnTimer"_s, NETWORK_RETURN_WINDOW_MS, true))
{
    const QJsonObject state = storage::loadState();
    volume_ = int(std::clamp<qint64>(json::integer(state.value("volume"_L1)).value_or(100), 0, 100));

    mpv_->setParent(this);
    mpv_->setVolume(volume_);
    connect(mpv_, &MpvBackend::coreIdleChanged, this, &PlayerManager::onCoreIdle);
    connect(mpv_, &MpvBackend::pausedForCacheChanged, this, &PlayerManager::onPausedForCache);
    connect(mpv_, &MpvBackend::endFile, this, &PlayerManager::onEndFile);

    connect(artwork_, &ArtworkLoader::artworkReady, this, &PlayerManager::onArtworkReady);

    connect(reconnectTimer_, &QTimer::timeout, this, &PlayerManager::attemptReconnect);
    connect(stallTimer_, &QTimer::timeout, this, &PlayerManager::onStalled);
    connect(blinkTimer_, &QTimer::timeout, this, &PlayerManager::onBlink);
    connect(volumeSaveTimer_, &QTimer::timeout, this, &PlayerManager::saveVolume);

    restoreLastStation(state.value("last_station_id"_L1).toString());
}

// MARK: - Состояние

bool PlayerManager::isActive() const
{
    return state_ == PlaybackState::Loading || state_ == PlaybackState::Playing ||
           state_ == PlaybackState::Reconnecting;
}

void PlayerManager::setState(PlaybackState state, const QString &error)
{
    if (state == state_ && error == errorMessage_)
        return;
    state_ = state;
    errorMessage_ = error;
    emit stateChanged();
}

QList<Station> PlayerManager::stations() const
{
    return store_->stations();
}

// MARK: - Публичное управление

void PlayerManager::play(const Station &station)
{
    currentStation_ = station;
    storage::updateState({{"last_station_id"_L1, station.id}});
    emit stationChanged();

    setTrack(std::nullopt);
    shouldBePlaying_ = true;
    reconnectTimer_->stop();
    stopBlinking();

    startMetadata(station);
    loadAndPlay(station);
}

void PlayerManager::stop()
{
    shouldBePlaying_ = false;
    reconnectTimer_->stop();
    stallTimer_->stop();
    networkReturnTimer_->stop();
    stopBlinking();
    stopMetadata();
    mpv_->stop();

    setTrack(std::nullopt);
    setState(PlaybackState::Idle);
}

void PlayerManager::toggle()
{
    if (isActive()) {
        stop();
        return;
    }
    if (currentStation_) {
        play(*currentStation_);
        return;
    }
    const QList<Station> available = stations();
    if (!available.isEmpty())
        play(available.first());
}

void PlayerManager::playNext()
{
    playRelative(1);
}

void PlayerManager::playPrevious()
{
    playRelative(-1);
}

void PlayerManager::setVolume(int value)
{
    const int volume = std::clamp(value, 0, 100);
    if (volume == volume_)
        return;
    volume_ = volume;
    mpv_->setVolume(volume_);
    emit volumeChanged(volume_);
    // Звук меняется сразу, диск ждёт: пользователь ведёт слайдер, а не
    // сохраняет настройку. Отсчёт начинается заново на каждом движении.
    volumeSaveTimer_->start();
}

void PlayerManager::saveVolume()
{
    storage::updateState({{"volume"_L1, volume_}});
}

void PlayerManager::shutdown()
{
    shouldBePlaying_ = false;
    // Громкость, изменённую перед самым выходом, теряем только вместе
    // с несохранённым таймером — поэтому дописываем её здесь.
    if (volumeSaveTimer_->isActive()) {
        volumeSaveTimer_->stop();
        saveVolume();
    }
    for (QTimer *timer : {reconnectTimer_, stallTimer_, blinkTimer_, networkReturnTimer_})
        timer->stop();
    stopMetadata();
    artwork_->cancelAll();
    mpv_->terminate();
}

// MARK: - Воспроизведение

void PlayerManager::loadAndPlay(const Station &station)
{
    // Не трогает намерение и метаданные — используется и при первом запуске,
    // и при каждом переподключении.
    stallTimer_->stop();
    // Новое соединение пропажу сети не застало — его обрыв ждёт обычной паузы.
    networkReturnTimer_->stop();
    const QString error = mpv_->play(station.url);
    if (!error.isEmpty()) {
        fail(tr("Could not open the stream: %1").arg(error));
        return;
    }

    // Играем не мы, а mpv: пока он не подтвердил звук, состояние — загрузка.
    if (!isReconnecting())
        setState(PlaybackState::Loading);
    stallTimer_->start();
}

void PlayerManager::playRelative(int offset)
{
    const QList<Station> available = stations();
    if (available.isEmpty())
        return;
    const auto current =
        currentStation_
            ? std::find_if(available.cbegin(), available.cend(),
                           [&](const Station &station) { return station.id == currentStation_->id; })
            : available.cend();
    if (current == available.cend()) {
        play(available.first());
        return;
    }
    const qsizetype count = available.size();
    const qsizetype index = ((current - available.cbegin()) + offset % count + count) % count;
    play(available[index]);
}

void PlayerManager::restoreLastStation(const QString &lastId)
{
    if (lastId.isEmpty())
        return;
    for (const Station &station : stations()) {
        if (station.id == lastId) {
            currentStation_ = station;
            return;
        }
    }
}

// MARK: - События mpv

void PlayerManager::onCoreIdle(bool idle)
{
    if (!idle)
        onPlaybackStarted();
}

void PlayerManager::onPausedForCache(bool paused)
{
    if (paused && shouldBePlaying_ && !isReconnecting()) {
        setState(PlaybackState::Loading);
        stallTimer_->start();
    }
}

void PlayerManager::onEndFile(MpvBackend::EndReason reason)
{
    // Для live-радио и ошибка, и «конец файла» означают обрыв. Но не наши
    // собственные stop/quit и не redirect — там mpv продолжает сам.
    const bool disconnected = reason == MpvBackend::EndReason::Eof || reason == MpvBackend::EndReason::Error;
    if (shouldBePlaying_ && disconnected)
        beginReconnecting();
}

void PlayerManager::onPlaybackStarted()
{
    // Поток реально пошёл: гасим индикаторы и сторожевые таймеры.
    stallTimer_->stop();
    if (isReconnecting()) {
        reconnectTimer_->stop();
        stopBlinking();
    }
    setState(shouldBePlaying_ ? PlaybackState::Playing : PlaybackState::Idle);
}

void PlayerManager::fail(const QString &message)
{
    // Во время переподключения неудачная попытка — часть цикла, а не событие:
    // индикатор иначе дёргался бы между «переподключение» и ошибкой.
    if (isReconnecting())
        return;
    // Намерение играть не сбрасываем: вернётся сеть — попробуем снова.
    setState(PlaybackState::Failed, message);
    emit errorOccurred(message);
}

// MARK: - Реконнект

void PlayerManager::beginReconnecting()
{
    if (!shouldBePlaying_ || isReconnecting())
        return;
    stallTimer_->stop();
    setState(PlaybackState::Reconnecting);
    blinkTimer_->start();
    reconnectDelayMs_ = RECONNECT_INTERVAL_MS;
    // Обрыв вскоре после возвращения сети — её последствие, а не падение сервера:
    // волны слушателей не будет, и выжидать паузу незачем.
    if (networkReturnTimer_->isActive()) {
        attemptReconnect();
        return;
    }
    armReconnect();
}

void PlayerManager::attemptReconnect()
{
    if (!shouldBePlaying_ || !currentStation_) {
        reconnectTimer_->stop();
        return;
    }
    // Попытка не удалась — следующей ждём дольше. Сеть, вернувшаяся раньше,
    // всё равно разбудит нас через `onNetworkReachable`.
    reconnectDelayMs_ = std::min(reconnectDelayMs_ * 2, RECONNECT_MAX_INTERVAL_MS);
    armReconnect();
    loadAndPlay(*currentStation_);
}

void PlayerManager::armReconnect()
{
    reconnectTimer_->setInterval(jitter_(reconnectDelayMs_));
    reconnectTimer_->start();
}

void PlayerManager::onStalled()
{
    // Буферизация затянулась — считаем поток зависшим.
    if (shouldBePlaying_ && !isReconnecting())
        beginReconnecting();
}

void PlayerManager::onBlink()
{
    blinkOn_ = !blinkOn_;
    emit blinkToggled(blinkOn_);
}

void PlayerManager::stopBlinking()
{
    blinkTimer_->stop();
    if (blinkOn_) {
        blinkOn_ = false;
        emit blinkToggled(false);
    }
}

void PlayerManager::onNetworkReachable(bool reachable)
{
    if (!shouldBePlaying_)
        return;
    if (!reachable) {
        // Звук идёт — пусть доиграет буфер: короткую пропажу Wi-Fi он перекроет.
        // Перезапуск выбросил бы его и оборвал звук на секунды раньше.
        if (!isPlaying())
            beginReconnecting();
        return;
    }
    // Сеть вернулась — пробуем сразу, не дожидаясь интервала цикла.
    if (isReconnecting()) {
        attemptReconnect();
        return;
    }
    // Буфер перекрыл пропажу, но поток мог уже оборваться внутри: HLS в ffmpeg
    // бросает его, не сумев перечитать плейлист, и mpv узнает об этом, лишь
    // доиграв буфер. Такой обрыв переподключаем без паузы.
    networkReturnTimer_->start();
}

// MARK: - Метаданные

void PlayerManager::startMetadata(const Station &station)
{
    stopMetadata();
    provider_ = makeProvider(station, mpv_, this);
    connect(provider_, &MetadataProvider::trackChanged, this, &PlayerManager::onTrackChanged);
    connect(provider_, &MetadataProvider::trackCleared, this, [this] { setTrack(std::nullopt); });
    provider_->start(station);
}

void PlayerManager::stopMetadata()
{
    if (provider_ == nullptr)
        return;
    MetadataProvider *provider = std::exchange(provider_, nullptr);
    provider->stop();
    provider->deleteLater();
}

void PlayerManager::onTrackChanged(const TrackInfo &track)
{
    const std::optional<TrackInfo> previous = currentTrack_;
    currentTrack_ = track;
    emit trackChanged();

    if (!track.artworkUrl.isEmpty())
        artwork_->load(track.artworkUrl);
    else if (!previous || previous->title != track.title)
        setArtwork({}, {});  // Новый трек без обложки — старую показывать нельзя.
}

void PlayerManager::setTrack(const std::optional<TrackInfo> &track)
{
    if (!track && !currentTrack_)
        return;
    currentTrack_ = track;
    emit trackChanged();
    if (!track)
        setArtwork({}, {});
}

void PlayerManager::onArtworkReady(const QString &url, const QString &path, const QImage &image)
{
    if (!currentTrack_ || currentTrack_->artworkUrl != url)
        return;  // Трек успел смениться.
    setArtwork(image, path);
}

void PlayerManager::setArtwork(const QImage &image, const QString &path)
{
    artworkImage_ = image;
    artworkPath_ = path;
    emit artworkChanged();
}

}  // namespace radio
