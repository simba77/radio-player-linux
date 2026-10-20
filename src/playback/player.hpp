#pragma once

// Воспроизведение: обёртка над libmpv с состояниями, реконнектом и метаданными.
//
// Ключевые решения:
//
// * «намерение играть» (`shouldBePlaying_`) живёт отдельно от факта воспроизведения
//   и сбрасывается только явным `stop()` — обрыв сети его не отменяет;
// * состояния берутся из наблюдения за mpv, а не выставляются оптимистично;
// * пропажа сети не обрывает уже идущий звук: буфер доигрывает до конца;
// * перед каждым запуском потока и в `stop()` снимаются все наблюдатели и таймеры.
//
// Наблюдаемое состояние — одно поле `state`. Проверки `isPlaying()`, `isLoading()`
// и `isReconnecting()` выводятся из него и разойтись друг с другом не могут.

#include "models.hpp"
#include "mpv_backend.hpp"

#include <QImage>
#include <QObject>

#include <functional>
#include <optional>

class QTimer;

namespace radio {

class ArtworkLoader;
class MetadataProvider;
class StationStore;

// Первая пауза между попытками переподключения.
inline constexpr int RECONNECT_INTERVAL_MS = 3000;
// Потолок паузы. Дальше расти незачем: минута — приемлемая задержка возврата
// эфира, а стучаться чаще при долгом простое сервера просто невежливо.
inline constexpr int RECONNECT_MAX_INTERVAL_MS = 60'000;
// Разброс паузы. Сервер радио падает сразу для всех слушателей, и без разброса
// они возвращаются одной волной ровно в те же секунды.
inline constexpr double RECONNECT_JITTER = 0.25;
// Сколько ждём выхода из буферизации, прежде чем считать поток зависшим.
inline constexpr int STALL_TIMEOUT_MS = 6000;
// Сколько после возвращения сети обрыв потока считаем последствием её пропажи
// и переподключаемся без паузы. С запасом длиннее буфера живого потока: три
// HLS-сегмента — это 20–40 секунд звука.
inline constexpr int NETWORK_RETURN_WINDOW_MS = 60'000;
// Период мигания иконки в трее во время переподключения.
inline constexpr int BLINK_INTERVAL_MS = 600;
// Пауза перед сохранением громкости. Слайдер шлёт значение на каждый пиксель,
// а запись состояния идёт с `fsync` в потоке интерфейса — за одно перетаскивание
// это десятки лишних обращений к диску.
inline constexpr int VOLUME_SAVE_DELAY_MS = 800;

// Пауза с разбросом: разводит по времени слушателей, потерявших связь одновременно.
int withJitter(int intervalMs);

// Что происходит с воспроизведением прямо сейчас.
enum class PlaybackState {
    // Ничего не играет и не запускается.
    Idle,
    // Поток открыт, но mpv ещё не подтвердил, что звук пошёл.
    Loading,
    // Идёт эфир — это подтвердил сам mpv, а не наше намерение.
    Playing,
    // Связь оборвалась, идут попытки переподключения.
    Reconnecting,
    // Поток не удалось открыть; причина — в `errorMessage()`.
    Failed,
};

class PlayerManager : public QObject {
    Q_OBJECT
public:
    // Плеер забирает `mpv` себе. Громкость и последняя станция читаются с диска.
    PlayerManager(StationStore *store, MpvBackend *mpv, QObject *parent = nullptr);

    PlaybackState state() const { return state_; }
    // Пользователь ждёт звука: играет, загружается или переподключается.
    //
    // Именно это, а не факт звука, определяет вид кнопки, запрет засыпания
    // и `PlaybackStatus` в MPRIS — иначе они мигали бы на каждой буферизации.
    bool isActive() const;
    // Эфир действительно идёт — по данным mpv, а не по нашему намерению.
    bool isPlaying() const { return state_ == PlaybackState::Playing; }
    bool isLoading() const { return state_ == PlaybackState::Loading; }
    bool isReconnecting() const { return state_ == PlaybackState::Reconnecting; }
    QString errorMessage() const { return errorMessage_; }
    bool blinkOn() const { return blinkOn_; }

    std::optional<Station> currentStation() const { return currentStation_; }
    std::optional<TrackInfo> currentTrack() const { return currentTrack_; }
    QImage artworkImage() const { return artworkImage_; }
    // Файл обложки в кэше; пусто, если обложки нет или она только в памяти.
    QString artworkPath() const { return artworkPath_; }

    QList<Station> stations() const;
    int volume() const { return volume_; }

    void play(const Station &station);
    void stop();
    void toggle();
    void playNext();
    void playPrevious();
    void setVolume(int value);
    // Реакция на изменение доступности сети. Идущий звук пропажа сети не трогает:
    // буфер перекрывает короткий обрыв Wi-Fi, а его конец ловят штатно — по концу
    // потока или сторожу буферизации.
    void onNetworkReachable(bool reachable);
    // Снимает всё, что держит ресурсы. Вызывается перед выходом.
    void shutdown();

    // Подменяет разброс пауз реконнекта — тестам нужны точные значения.
    void setJitter(std::function<int(int)> jitter) { jitter_ = std::move(jitter); }

signals:
    // Изменилось `state()` или `errorMessage()`.
    void stateChanged();
    void stationChanged();
    void trackChanged();
    void artworkChanged();
    // Фаза мигания во время переподключения.
    void blinkToggled(bool on);
    // Громкость 0–100. Её меняют и извне — через MPRIS.
    void volumeChanged(int volume);
    void errorOccurred(const QString &message);

private:
    void setState(PlaybackState state, const QString &error = {});
    void loadAndPlay(const Station &station);
    void playRelative(int offset);
    void restoreLastStation(const QString &lastId);

    void onCoreIdle(bool idle);
    void onPausedForCache(bool paused);
    void onEndFile(MpvBackend::EndReason reason);
    void onPlaybackStarted();
    void fail(const QString &message);

    void beginReconnecting();
    void attemptReconnect();
    void armReconnect();
    void onStalled();
    void onBlink();
    void stopBlinking();
    void saveVolume();

    void startMetadata(const Station &station);
    void stopMetadata();
    void onTrackChanged(const TrackInfo &track);
    void setTrack(const std::optional<TrackInfo> &track);
    void onArtworkReady(const QString &url, const QString &path, const QImage &image);
    void setArtwork(const QImage &image, const QString &path);

    StationStore *store_;
    MpvBackend *mpv_;
    ArtworkLoader *artwork_;
    MetadataProvider *provider_ = nullptr;

    QTimer *reconnectTimer_;
    QTimer *stallTimer_;
    QTimer *blinkTimer_;
    QTimer *volumeSaveTimer_;
    // Активен, пока обрыв потока считается эхом недавней пропажи сети.
    QTimer *networkReturnTimer_;

    PlaybackState state_ = PlaybackState::Idle;
    QString errorMessage_;
    bool blinkOn_ = false;
    std::optional<Station> currentStation_;
    std::optional<TrackInfo> currentTrack_;
    QImage artworkImage_;
    QString artworkPath_;

    bool shouldBePlaying_ = false;
    int reconnectDelayMs_ = RECONNECT_INTERVAL_MS;
    int volume_ = 100;
    std::function<int(int)> jitter_ = withJitter;
};

}  // namespace radio
