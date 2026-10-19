// Запрет засыпания.
//
// Служба питания — двойник на своей шине, с теми же типами методов, что у
// PowerDevil. Поэтому здесь проверяется и логика намерения (когда блокировка
// берётся и снимается, что делать с поздним ответом), и то, что `UnInhibit`
// уходит с `uint`: с другим типом шина до службы его не доставит.

#include "dbus_bus.hpp"
#include "fakes.hpp"
#include "player.hpp"
#include "power.hpp"
#include "station_store.hpp"
#include "support.hpp"

#include <QTest>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const Station STATION{u"Станция"_s, u"https://radio.invalid/s"_s, u"s"_s};

struct PowerFixture {
    test::PrivateBus bus;
    test::FakePowerManagement service;
    StationStore store{{Catalog{u"test"_s, u"test"_s, 1, {STATION}}}};
    test::FakeMpv *mpv = new test::FakeMpv;
    PlayerManager player{&store, mpv};
    std::unique_ptr<SleepInhibitor> inhibitor;

    PowerFixture()
    {
        if (!bus.available())
            SKIP("dbus-daemon недоступен");
        REQUIRE(service.registerOn(bus.connect()));
        inhibitor = std::make_unique<SleepInhibitor>(&player, bus.connect());
    }
    ~PowerFixture()
    {
        inhibitor.reset();
        player.shutdown();
    }

    bool held()
    {
        return test::waitUntil([&] { return inhibitor->isHeld(); }, 3000);
    }
};

}  // namespace

TEST_CASE_METHOD(PowerFixture, "playback takes the lock")
{
    player.play(STATION);
    REQUIRE(held());
    CHECK(service.inhibited == 1);
}

TEST_CASE_METHOD(PowerFixture, "stop releases the lock with a uint cookie")
{
    player.play(STATION);
    REQUIRE(held());

    player.stop();
    CHECK_FALSE(inhibitor->isHeld());
    CHECK(test::waitUntil([&] { return service.released == QList<uint>{7}; }));
}

TEST_CASE_METHOD(PowerFixture, "the lock is held while the player stays active")
{
    // Заснуть между попытками переподключения — то же, что заснуть в эфире.
    player.play(STATION);
    REQUIRE(held());
    emit mpv->endFile(MpvBackend::EndReason::Error);
    CHECK(player.isReconnecting());

    QTest::qWait(100);
    CHECK(inhibitor->isHeld());
    CHECK(service.released.isEmpty());
}

TEST_CASE_METHOD(PowerFixture, "repeated state changes do not stack locks")
{
    player.play(STATION);
    REQUIRE(held());
    emit mpv->coreIdleChanged(false);
    emit mpv->pausedForCacheChanged(true);

    QTest::qWait(100);
    CHECK(service.inhibited == 1);
}

TEST_CASE_METHOD(PowerFixture, "a late reply is released immediately")
{
    // Ответ пришёл, когда играть уже перестали — cookie нельзя оставлять висеть.
    player.play(STATION);
    player.stop();

    REQUIRE(test::waitUntil([&] { return service.released == QList<uint>{7}; }));
    CHECK_FALSE(inhibitor->isHeld());
}

TEST_CASE_METHOD(PowerFixture, "release drops the lock on quit")
{
    player.play(STATION);
    REQUIRE(held());

    inhibitor->release();
    CHECK(test::waitUntil([&] { return service.released == QList<uint>{7}; }));
}

TEST_CASE("a missing power service does not break playback")
{
    test::PrivateBus bus;
    if (!bus.available())
        SKIP("dbus-daemon недоступен");
    StationStore store({Catalog{u"test"_s, u"test"_s, 1, {STATION}}});
    PlayerManager player(&store, new test::FakeMpv);
    SleepInhibitor inhibitor(&player, bus.connect());

    player.play(STATION);
    QTest::qWait(200);
    CHECK_FALSE(inhibitor.isHeld());
    CHECK(player.isActive());
    player.shutdown();
}
