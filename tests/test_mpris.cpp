// MPRIS2.
//
// Главный тест здесь — про сигнатуру `PropertiesChanged`. Ошибка в ней не видна
// ни в `dbus-monitor`, ни при чтении свойств: сигнал уходит, но Qt-клиенты
// (plasmashell, kded) его молча игнорируют, и плеер не получает медиаклавиши.
// Поэтому сигнал принимается с настоящей шины отдельным клиентом.

#include "dbus_bus.hpp"
#include "fakes.hpp"
#include "mpris.hpp"
#include "player.hpp"
#include "station_store.hpp"
#include "support.hpp"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QSignalSpy>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const Station STATION{u"Моя станция"_s, u"https://radio.invalid/s"_s, u"s1"_s};
const Station OTHER{u"Другая"_s, u"https://radio.invalid/o"_s, u"s2"_s};
const QString PLAYER_INTERFACE = u"org.mpris.MediaPlayer2.Player"_s;
const QString PROPERTIES_INTERFACE = u"org.freedesktop.DBus.Properties"_s;

// MPRIS-сервер на своей шине и клиент, который его слушает.
struct MprisFixture {
    test::PrivateBus bus;
    StationStore store{{Catalog{u"test"_s, u"test"_s, 1, {STATION, OTHER}}}};
    test::FakeMpv *mpv = new test::FakeMpv;
    PlayerManager player{&store, mpv};
    QDBusConnection client{QString()};
    std::unique_ptr<MprisServer> server;

    MprisFixture()
    {
        if (!bus.available())
            SKIP("dbus-daemon недоступен");
        server = std::make_unique<MprisServer>(&player, bus.connect());
        REQUIRE(server->registerService());
        client = bus.connect();
    }
    ~MprisFixture()
    {
        server.reset();
        player.shutdown();
    }

    QDBusMessage call(const QString &interface, const QString &method, const QVariantList &arguments = {})
    {
        QDBusMessage message = QDBusMessage::createMethodCall(
            mprisBusName(), QLatin1String(MPRIS_OBJECT_PATH), interface, method);
        message.setArguments(arguments);
        // Сервер живёт в том же потоке: ждём ответа, не останавливая цикл событий.
        QDBusPendingCall pending = client.asyncCall(message);
        REQUIRE(test::waitUntil([&] { return pending.isFinished(); }));
        return pending.reply();
    }

    QVariant property(const QString &name)
    {
        const QDBusMessage reply = call(PROPERTIES_INTERFACE, u"Get"_s, {PLAYER_INTERFACE, name});
        REQUIRE(reply.type() == QDBusMessage::ReplyMessage);
        return reply.arguments().value(0).value<QDBusVariant>().variant();
    }
};

}  // namespace

// MARK: - Метаданные

TEST_CASE("metadata without a track shows the station")
{
    const QVariantMap metadata = mprisMetadata(STATION, std::nullopt, {});
    CHECK(metadata.value(u"xesam:title"_s).toString() == u"Моя станция"_s);
    CHECK(metadata.contains(u"mpris:trackid"_s));
}

TEST_CASE("metadata with a track")
{
    const QVariantMap metadata = mprisMetadata(STATION, TrackInfo{u"Трек"_s, u"Исполнитель"_s, {}}, {});
    CHECK(metadata.value(u"xesam:title"_s).toString() == u"Трек"_s);
    CHECK(metadata.value(u"xesam:artist"_s).toStringList() == QStringList{u"Исполнитель"_s});
    CHECK(metadata.value(u"xesam:album"_s).toString() == u"Моя станция"_s);
}

TEST_CASE("metadata artist is always a list")
{
    // `xesam:artist` по спецификации — массив строк, даже когда исполнителя нет.
    const QVariantMap metadata = mprisMetadata(STATION, TrackInfo{u"Трек"_s, {}, {}}, {});
    CHECK(metadata.value(u"xesam:artist"_s).metaType() == QMetaType::fromType<QStringList>());
    CHECK(metadata.value(u"xesam:artist"_s).toStringList().isEmpty());
}

TEST_CASE("metadata artwork is a file uri")
{
    const QVariantMap metadata =
        mprisMetadata(STATION, TrackInfo{u"Трек"_s, {}, {}}, u"/tmp/cover file.jpg"_s);
    CHECK(metadata.value(u"mpris:artUrl"_s).toString() == "file:///tmp/cover file.jpg"_L1);
}

TEST_CASE("metadata without artwork has no art url")
{
    CHECK_FALSE(mprisMetadata(STATION, std::nullopt, {}).contains(u"mpris:artUrl"_s));
}

TEST_CASE("metadata without a station is still valid")
{
    // Плеер без выбранной станции обязан отдавать корректный trackid.
    CHECK(mprisMetadata(std::nullopt, std::nullopt, {}).contains(u"mpris:trackid"_s));
}

// MARK: - На шине

TEST_CASE_METHOD(MprisFixture, "playback status reports activity, not sound")
{
    // Буферизация и переподключение остаются «Playing». Иначе апплет
    // «Медиаплеер» переключался бы в «остановлено» на каждой заминке потока.
    CHECK(property(u"PlaybackStatus"_s).toString() == "Stopped"_L1);
    player.play(STATION);
    CHECK(player.isLoading());
    CHECK(property(u"PlaybackStatus"_s).toString() == "Playing"_L1);
}

TEST_CASE_METHOD(MprisFixture, "properties changed carries a string array")
{
    // `sa{sv}as`, а не `sa{sv}av`: с массивом вариантов Plasma молча игнорирует
    // сигнал, и плеер не попадает в апплет и не получает медиаклавиши.
    test::SignalRecorder recorder;
    REQUIRE(client.connect(QString(), QLatin1String(MPRIS_OBJECT_PATH), PROPERTIES_INTERFACE,
                           u"PropertiesChanged"_s, &recorder, SLOT(record(QDBusMessage))));
    // Синхронный вызов к шине: правило подписки точно дошло до неё раньше сигнала.
    client.interface()->registeredServiceNames();

    player.setVolume(42);
    REQUIRE(test::waitUntil([&] { return !recorder.messages.isEmpty(); }));
    const QDBusMessage message = recorder.messages.first();
    CHECK(message.signature() == "sa{sv}as"_L1);
    CHECK(message.arguments().value(0).toString() == PLAYER_INTERFACE);
    const auto changed = qdbus_cast<QVariantMap>(message.arguments().value(1));
    CHECK(changed.value(u"Volume"_s).toDouble() == Catch::Approx(0.42));
}

TEST_CASE_METHOD(MprisFixture, "media keys reach the player")
{
    call(PLAYER_INTERFACE, u"PlayPause"_s);
    CHECK(mpv->played == QStringList{STATION.url});

    call(PLAYER_INTERFACE, u"Next"_s);
    CHECK(player.currentStation() == OTHER);

    call(PLAYER_INTERFACE, u"Pause"_s);
    CHECK_FALSE(player.isActive());
}

TEST_CASE_METHOD(MprisFixture, "the applet volume slider reaches the player")
{
    const QDBusMessage reply = call(PROPERTIES_INTERFACE, u"Set"_s,
                                    {PLAYER_INTERFACE, u"Volume"_s, QVariant::fromValue(QDBusVariant(0.3))});
    CHECK(reply.type() == QDBusMessage::ReplyMessage);
    CHECK(player.volume() == 30);
}

TEST_CASE_METHOD(MprisFixture, "seeking signatures follow the specification")
{
    // Seek(x) и SetPosition(ox): с другими типами клиент получил бы UnknownMethod.
    CHECK(call(PLAYER_INTERFACE, u"Seek"_s, {qlonglong(1000)}).type() == QDBusMessage::ReplyMessage);
    CHECK(
        call(PLAYER_INTERFACE, u"SetPosition"_s,
             {QVariant::fromValue(QDBusObjectPath(u"/org/mpris/MediaPlayer2/CurrentTrack"_s)), qlonglong(0)})
            .type() == QDBusMessage::ReplyMessage);
}

TEST_CASE_METHOD(MprisFixture, "raise and quit are passed to the application")
{
    QSignalSpy raised(server.get(), &MprisServer::raiseRequested);
    QSignalSpy quit(server.get(), &MprisServer::quitRequested);
    call(u"org.mpris.MediaPlayer2"_s, u"Raise"_s);
    call(u"org.mpris.MediaPlayer2"_s, u"Quit"_s);
    CHECK(raised.count() == 1);
    CHECK(quit.count() == 1);
}

TEST_CASE_METHOD(MprisFixture, "the desktop entry matches the application id")
{
    const QDBusMessage reply =
        call(PROPERTIES_INTERFACE, u"Get"_s, {u"org.mpris.MediaPlayer2"_s, u"DesktopEntry"_s});
    CHECK(reply.arguments().value(0).value<QDBusVariant>().variant().toString() ==
          "io.github.simba77.RadioPlayer"_L1);
}

TEST_CASE_METHOD(MprisFixture, "a second registration of the name fails")
{
    MprisServer second(&player, bus.connect());
    CHECK_FALSE(second.registerService());
}
