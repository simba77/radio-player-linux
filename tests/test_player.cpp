// Состояния плеера: запуск потока, обрыв, реконнект, громкость.
//
// libmpv здесь не участвует — вместо него подставляется двойник. Проверяется
// именно логика: намерение играть отдельно от факта, реконнект по обрыву и по
// зависшей буферизации, и то, что собственный `stop()` реконнект не запускает.
//
// Таймеры не ждём: реальные интервалы измеряются секундами, поэтому срабатывание
// имитируется через `test::fire()`, а проверяется факт, что таймер взведён.

#include "fakes.hpp"
#include "paths.hpp"
#include "player.hpp"
#include "station_store.hpp"
#include "storage.hpp"
#include "support.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTimer>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <set>

using namespace radio;
using namespace Qt::StringLiterals;
using EndReason = MpvBackend::EndReason;

namespace {

// Адреса в зоне .invalid не разрешаются: провайдер метаданных HLS-станции
// не уйдёт в настоящую сеть.
const Station FIRST{u"Первая"_s, u"https://radio.invalid/first"_s, u"first"_s};
const Station SECOND{u"Вторая"_s, u"https://radio.invalid/second.m3u8"_s, u"second"_s};

struct PlayerFixture {
    StationStore store{{Catalog{u"test"_s, u"test"_s, 1, {FIRST, SECOND}}}};
    test::FakeMpv *mpv = new test::FakeMpv;
    PlayerManager player{&store, mpv};

    ~PlayerFixture() { player.shutdown(); }

    // Станция запущена, и поток действительно пошёл.
    void startPlaying(const Station &station = FIRST)
    {
        player.play(station);
        emit mpv->coreIdleChanged(false);
    }

    QTimer *timer(const char *name) const { return test::timer(&player, QString::fromLatin1(name)); }
};

}  // namespace

// MARK: - Запуск

TEST_CASE_METHOD(PlayerFixture, "play opens the stream")
{
    player.play(FIRST);
    CHECK(mpv->played == QStringList{FIRST.url});
}

TEST_CASE_METHOD(PlayerFixture, "playback is confirmed by mpv")
{
    player.play(FIRST);
    CHECK(player.state() == PlaybackState::Loading);

    emit mpv->coreIdleChanged(false);
    CHECK(player.state() == PlaybackState::Playing);
}

TEST_CASE_METHOD(PlayerFixture, "loading is not reported as playing")
{
    // Пока mpv не подтвердил звук, «играет» — неправда. Наблюдаемое состояние
    // важнее оптимистичного флага: на нём держится и иконка в трее, и MPRIS.
    player.play(FIRST);
    CHECK_FALSE(player.isPlaying());
    // Но намерение уже есть: кнопка показывает «стоп», система не спит.
    CHECK(player.isActive());
}

TEST_CASE_METHOD(PlayerFixture, "failure to open reports an error")
{
    mpv->failOnPlay = true;
    QSignalSpy errors(&player, &PlayerManager::errorOccurred);

    player.play(FIRST);
    CHECK(player.state() == PlaybackState::Failed);
    REQUIRE(errors.count() == 1);
    CHECK(errors[0][0].toString() == player.errorMessage());
}

TEST_CASE_METHOD(PlayerFixture, "a new attempt clears the previous error")
{
    mpv->failOnPlay = true;
    player.play(FIRST);
    mpv->failOnPlay = false;

    player.play(FIRST);
    CHECK(player.errorMessage().isEmpty());
    CHECK(player.state() == PlaybackState::Loading);
}

TEST_CASE_METHOD(PlayerFixture, "stop leaves nothing running")
{
    startPlaying();
    player.stop();

    CHECK(player.state() == PlaybackState::Idle);
    CHECK_FALSE(player.isActive());
    CHECK(mpv->stops == 1);
    CHECK_FALSE(timer("reconnectTimer")->isActive());
    CHECK_FALSE(timer("stallTimer")->isActive());
}

TEST_CASE_METHOD(PlayerFixture, "the last station is remembered")
{
    player.play(SECOND);
    CHECK(storage::loadState().value("last_station_id"_L1).toString() == SECOND.id);
}

TEST_CASE("the last station is restored on startup")
{
    storage::updateState({{"last_station_id"_L1, SECOND.id}});
    StationStore store({Catalog{u"test"_s, u"test"_s, 1, {FIRST, SECOND}}});
    auto *mpv = new test::FakeMpv;
    PlayerManager restored(&store, mpv);

    CHECK(restored.currentStation() == SECOND);
    // Восстановление — не воспроизведение: станция выбрана, поток молчит.
    CHECK(restored.state() == PlaybackState::Idle);
    CHECK(mpv->played.isEmpty());
    restored.shutdown();
}

// MARK: - Обрыв и реконнект

TEST_CASE_METHOD(PlayerFixture, "a stream break starts reconnecting")
{
    // Для живого радио и «конец файла», и ошибка означают обрыв.
    const EndReason reason = GENERATE(EndReason::Eof, EndReason::Error);
    startPlaying();
    emit mpv->endFile(reason);

    CHECK(player.isReconnecting());
    CHECK(timer("reconnectTimer")->isActive());
    CHECK(timer("blinkTimer")->isActive());
}

TEST_CASE_METHOD(PlayerFixture, "our own stop does not reconnect")
{
    // Остановку и переход по редиректу mpv отрабатывает сам.
    const EndReason reason = GENERATE(EndReason::Stop, EndReason::Quit, EndReason::Redirect);
    startPlaying();
    emit mpv->endFile(reason);

    CHECK_FALSE(player.isReconnecting());
    CHECK_FALSE(timer("reconnectTimer")->isActive());
}

TEST_CASE_METHOD(PlayerFixture, "stop cancels the intention to play")
{
    startPlaying();
    player.stop();
    emit mpv->endFile(EndReason::Error);

    CHECK_FALSE(player.isReconnecting());
}

TEST_CASE_METHOD(PlayerFixture, "reconnect retries the same station")
{
    startPlaying();
    emit mpv->endFile(EndReason::Error);
    test::fire(timer("reconnectTimer"));

    CHECK(mpv->played == QStringList{FIRST.url, FIRST.url});
}

TEST_CASE_METHOD(PlayerFixture, "a successful reconnect clears the indication")
{
    startPlaying();
    emit mpv->endFile(EndReason::Error);
    test::fire(timer("reconnectTimer"));
    emit mpv->coreIdleChanged(false);

    CHECK_FALSE(player.isReconnecting());
    CHECK(player.isPlaying());
    CHECK_FALSE(timer("blinkTimer")->isActive());
    CHECK_FALSE(player.blinkOn());
}

TEST_CASE_METHOD(PlayerFixture, "reconnecting still counts as active")
{
    // Пауза между попытками — не остановка: система не должна засыпать,
    // а кнопка обязана оставаться «стоп».
    startPlaying();
    emit mpv->endFile(EndReason::Error);

    CHECK(player.isActive());
    CHECK_FALSE(player.isPlaying());
}

TEST_CASE_METHOD(PlayerFixture, "a failed attempt during reconnect keeps reconnecting")
{
    // Неудачная попытка — часть цикла, а не отдельное событие для пользователя.
    startPlaying();
    emit mpv->endFile(EndReason::Error);
    mpv->failOnPlay = true;
    test::fire(timer("reconnectTimer"));

    CHECK(player.state() == PlaybackState::Reconnecting);
    CHECK(player.errorMessage().isEmpty());
}

TEST_CASE_METHOD(PlayerFixture, "reconnecting survives repeated breaks")
{
    // Второй обрыв подряд не должен перезапускать мигание с нуля.
    startPlaying();
    emit mpv->endFile(EndReason::Error);
    emit mpv->endFile(EndReason::Error);

    CHECK(player.isReconnecting());
    CHECK(timer("reconnectTimer")->isActive());
}

TEST_CASE_METHOD(PlayerFixture, "stalled buffering leads to reconnect")
{
    startPlaying();
    emit mpv->pausedForCacheChanged(true);
    CHECK(player.isLoading());
    CHECK(timer("stallTimer")->isActive());

    test::fire(timer("stallTimer"));
    CHECK(player.isReconnecting());
}

TEST_CASE_METHOD(PlayerFixture, "playback cancels the stall watchdog")
{
    player.play(FIRST);
    CHECK(timer("stallTimer")->isActive());

    emit mpv->coreIdleChanged(false);
    CHECK_FALSE(timer("stallTimer")->isActive());
}

// MARK: - Сеть

TEST_CASE_METHOD(PlayerFixture, "network loss before the sound starts reconnecting")
{
    // Звука ещё нет — доигрывать нечего.
    player.play(FIRST);
    player.onNetworkReachable(false);
    CHECK(player.isReconnecting());
}

TEST_CASE_METHOD(PlayerFixture, "network loss lets the buffer play out")
{
    // Перезапуск потока выбросил бы буфер, который перекрывает короткую пропажу Wi-Fi.
    startPlaying();
    player.onNetworkReachable(false);

    CHECK(player.isPlaying());
    CHECK(mpv->played == QStringList{FIRST.url});
    CHECK_FALSE(timer("reconnectTimer")->isActive());
}

TEST_CASE_METHOD(PlayerFixture, "a buffer run dry without network waits the pause")
{
    startPlaying();
    player.onNetworkReachable(false);
    emit mpv->endFile(EndReason::Eof);

    CHECK(player.isReconnecting());
    CHECK(mpv->played == QStringList{FIRST.url});
    CHECK(timer("reconnectTimer")->isActive());
}

TEST_CASE_METHOD(PlayerFixture, "network return retries immediately")
{
    // Ждать конца трёхсекундного цикла, когда сеть уже вернулась, незачем.
    startPlaying();
    emit mpv->endFile(EndReason::Error);
    player.onNetworkReachable(false);
    player.onNetworkReachable(true);
    CHECK(mpv->played == QStringList{FIRST.url, FIRST.url});
}

TEST_CASE_METHOD(PlayerFixture, "network return keeps the stream that survived")
{
    startPlaying();
    player.onNetworkReachable(false);
    player.onNetworkReachable(true);

    CHECK(player.isPlaying());
    CHECK(mpv->played == QStringList{FIRST.url});
}

TEST_CASE_METHOD(PlayerFixture, "a break soon after the network returns reconnects at once")
{
    // HLS бросает поток, пока сети нет, а mpv сообщает об этом, лишь доиграв буфер —
    // когда сеть уже вернулась.
    startPlaying();
    player.onNetworkReachable(false);
    player.onNetworkReachable(true);
    emit mpv->endFile(EndReason::Eof);

    CHECK(mpv->played == QStringList{FIRST.url, FIRST.url});
}

TEST_CASE_METHOD(PlayerFixture, "a break long after the network returns waits the pause")
{
    startPlaying();
    player.onNetworkReachable(false);
    player.onNetworkReachable(true);
    // Однократный таймер, отработав, гаснет — так и истекает окно.
    timer("networkReturnTimer")->stop();
    emit mpv->endFile(EndReason::Eof);

    CHECK(mpv->played == QStringList{FIRST.url});
    CHECK(timer("reconnectTimer")->isActive());
}

TEST_CASE_METHOD(PlayerFixture, "a new stream closes the network return window")
{
    // Новое соединение пропажу сети не застало.
    const bool stopped = GENERATE(true, false);
    startPlaying();
    player.onNetworkReachable(false);
    player.onNetworkReachable(true);
    if (stopped)
        player.stop();
    else
        player.play(SECOND);

    CHECK_FALSE(timer("networkReturnTimer")->isActive());
}

TEST_CASE_METHOD(PlayerFixture, "network events are ignored when not playing")
{
    player.onNetworkReachable(false);
    CHECK_FALSE(player.isReconnecting());
    player.onNetworkReachable(true);
    CHECK_FALSE(timer("networkReturnTimer")->isActive());
}

// MARK: - Переключение станций

TEST_CASE_METHOD(PlayerFixture, "next and previous wrap around")
{
    startPlaying(SECOND);
    player.playNext();
    CHECK(player.currentStation() == FIRST);

    player.playPrevious();
    CHECK(player.currentStation() == SECOND);
}

TEST_CASE_METHOD(PlayerFixture, "toggle starts the first station when nothing was chosen")
{
    player.toggle();
    CHECK(player.currentStation() == FIRST);
}

TEST_CASE_METHOD(PlayerFixture, "toggle stops what is playing")
{
    startPlaying();
    player.toggle();
    CHECK_FALSE(player.isPlaying());
}

// MARK: - Громкость

TEST_CASE_METHOD(PlayerFixture, "volume is clamped and reaches mpv")
{
    player.setVolume(150);
    CHECK(player.volume() == 100);
    CHECK(mpv->volume == 100);

    player.setVolume(-10);
    CHECK(player.volume() == 0);
}

TEST_CASE_METHOD(PlayerFixture, "volume reaches the disk after the pause")
{
    player.setVolume(42);
    test::fire(timer("volumeSaveTimer"));
    CHECK(storage::loadState().value("volume"_L1).toInt() == 42);
}

TEST_CASE_METHOD(PlayerFixture, "dragging the slider writes once")
{
    // Слайдер шлёт значение на каждый пиксель, а запись идёт с fsync
    // в потоке интерфейса — за одно перетаскивание это десятки обращений к диску.
    for (int value = 40; value < 60; ++value)
        player.setVolume(value);
    CHECK_FALSE(QFile::exists(paths::stateFile()));
    CHECK(timer("volumeSaveTimer")->isActive());

    test::fire(timer("volumeSaveTimer"));
    CHECK(storage::loadState().value("volume"_L1).toInt() == 59);
}

TEST_CASE_METHOD(PlayerFixture, "volume is saved on shutdown")
{
    // Между последним движением слайдера и выходом пауза не успевает истечь.
    player.setVolume(42);
    player.shutdown();
    CHECK(storage::loadState().value("volume"_L1).toInt() == 42);
}

TEST_CASE("volume is restored on startup")
{
    storage::updateState({{"volume"_L1, 33}});
    StationStore store({Catalog{u"test"_s, u"test"_s, 1, {FIRST}}});
    auto *mpv = new test::FakeMpv;
    PlayerManager restored(&store, mpv);

    CHECK(restored.volume() == 33);
    CHECK(mpv->volume == 33);
    restored.shutdown();
}

TEST_CASE("a broken saved volume falls back to full")
{
    storage::updateState({{"volume"_L1, u"громко"_s}});
    StationStore store;
    PlayerManager restored(&store, new test::FakeMpv);
    CHECK(restored.volume() == 100);
    restored.shutdown();
}

TEST_CASE_METHOD(PlayerFixture, "a volume change is announced")
{
    // Громкость меняют и снаружи: слайдер в окне и апплет MPRIS узнают об этом
    // только из сигнала.
    QSignalSpy announced(&player, &PlayerManager::volumeChanged);
    player.setVolume(55);
    REQUIRE(announced.count() == 1);
    CHECK(announced[0][0].toInt() == 55);
}

TEST_CASE_METHOD(PlayerFixture, "setting the same volume announces nothing")
{
    player.setVolume(55);
    QSignalSpy announced(&player, &PlayerManager::volumeChanged);
    player.setVolume(55);
    CHECK(announced.isEmpty());
}

// MARK: - Завершение

TEST_CASE_METHOD(PlayerFixture, "shutdown releases everything")
{
    startPlaying();
    emit mpv->endFile(EndReason::Error);
    player.shutdown();

    CHECK(mpv->terminated);
    CHECK_FALSE(timer("reconnectTimer")->isActive());
    CHECK_FALSE(timer("blinkTimer")->isActive());
    CHECK_FALSE(timer("stallTimer")->isActive());
}

// MARK: - Пауза между попытками

TEST_CASE_METHOD(PlayerFixture, "the reconnect pause grows with each failure")
{
    // Сервер радио может лежать часами — стучаться каждые три секунды невежливо.
    player.setJitter([](int interval) { return interval; });
    startPlaying();
    emit mpv->endFile(EndReason::Error);

    QList<int> delays{timer("reconnectTimer")->interval()};
    for (int attempt = 0; attempt < 3; ++attempt) {
        test::fire(timer("reconnectTimer"));
        delays.append(timer("reconnectTimer")->interval());
    }
    CHECK(delays == QList<int>{3000, 6000, 12000, 24000});
}

TEST_CASE_METHOD(PlayerFixture, "the reconnect pause stops at the ceiling")
{
    player.setJitter([](int interval) { return interval; });
    startPlaying();
    emit mpv->endFile(EndReason::Error);
    for (int attempt = 0; attempt < 20; ++attempt)
        test::fire(timer("reconnectTimer"));

    CHECK(timer("reconnectTimer")->interval() == RECONNECT_MAX_INTERVAL_MS);
}

TEST_CASE_METHOD(PlayerFixture, "the reconnect pause starts over after a new break")
{
    // Связь восстановилась — накопленная задержка больше не про эту ситуацию.
    player.setJitter([](int interval) { return interval; });
    startPlaying();
    emit mpv->endFile(EndReason::Error);
    test::fire(timer("reconnectTimer"));
    emit mpv->coreIdleChanged(false);

    emit mpv->endFile(EndReason::Error);
    CHECK(timer("reconnectTimer")->interval() == RECONNECT_INTERVAL_MS);
}

TEST_CASE("jitter stays within the declared spread")
{
    std::set<int> values;
    for (int sample = 0; sample < 200; ++sample)
        values.insert(withJitter(10'000));
    CHECK(*values.begin() >= 10'000 * (1 - RECONNECT_JITTER));
    CHECK(*values.rbegin() <= 10'000 * (1 + RECONNECT_JITTER));
    // Разброс должен быть настоящим, иначе смысла в нём нет.
    CHECK(values.size() > 1);
}

// MARK: - Трек и обложка

TEST_CASE_METHOD(PlayerFixture, "icy metadata becomes the current track")
{
    startPlaying();
    emit mpv->metadataChanged({{u"icy-title"_s, u"Кто-то - Что-то"_s}});
    REQUIRE(player.currentTrack());
    CHECK(player.currentTrack()->display() == u"Кто-то — Что-то"_s);
}

TEST_CASE_METHOD(PlayerFixture, "stop forgets the track")
{
    startPlaying();
    emit mpv->metadataChanged({{u"icy-title"_s, u"Кто-то - Что-то"_s}});
    QSignalSpy tracks(&player, &PlayerManager::trackChanged);
    player.stop();

    CHECK_FALSE(player.currentTrack());
    CHECK(tracks.count() >= 1);
}
