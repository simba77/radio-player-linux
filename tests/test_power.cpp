// Запрет засыпания.
//
// Службы — двойники на своей шине, с теми же типами методов, что у настоящих:
// портал принимает только `sua{sv}`, а служба питания — `UnInhibit` с `uint`.
// Поэтому здесь проверяется и логика намерения (когда блокировка берётся
// и снимается, что делать с поздним ответом), и то, что вызовы уходят с верными
// типами: с другими шина до службы их не доставит.

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

// Плеер и инхибитор на своей шине; какие службы на ней есть — решает тест.
struct PowerFixture {
    test::PrivateBus bus;
    StationStore store{{Catalog{u"test"_s, u"test"_s, 1, {STATION}}}};
    test::FakeMpv *mpv = new test::FakeMpv;
    PlayerManager player{&store, mpv};
    std::unique_ptr<SleepInhibitor> inhibitor;

    PowerFixture()
    {
        if (!bus.available())
            SKIP("dbus-daemon недоступен");
    }
    ~PowerFixture()
    {
        inhibitor.reset();
        player.shutdown();
    }

    void start() { inhibitor = std::make_unique<SleepInhibitor>(&player, bus.connect()); }
    bool held()
    {
        return test::waitUntil([&] { return inhibitor->isHeld(); }, 3000);
    }
};

// Есть только служба питания: окружение без портала.
struct LegacyFixture : PowerFixture {
    test::FakePowerManagement service;

    LegacyFixture()
    {
        REQUIRE(service.registerOn(bus.connect()));
        start();
    }

    bool released(QList<uint> cookies)
    {
        return test::waitUntil([&] { return service.released == cookies; });
    }
};

// Есть и портал, и служба питания: портал должен победить.
struct PortalFixture : PowerFixture {
    test::FakeInhibitPortal portal;
    test::FakePowerManagement service;

    PortalFixture()
    {
        REQUIRE(portal.registerOn(bus.connect()));
        REQUIRE(service.registerOn(bus.connect()));
        start();
    }

    bool closed(qsizetype count)
    {
        return test::waitUntil([&] { return portal.closed.size() == count; });
    }
};

}  // namespace

// MARK: - Портал

TEST_CASE_METHOD(PortalFixture, "the portal is preferred when it is there")
{
    player.play(STATION);
    REQUIRE(held());
    CHECK(inhibitor->backend() == SleepInhibitor::Backend::Portal);
    REQUIRE(portal.calls.size() == 1);
    CHECK(service.inhibited == 0);
}

TEST_CASE_METHOD(PortalFixture, "the portal inhibits only suspend")
{
    // Гашение экрана и блокировка сеанса радио не мешают: флаг Idle не нужен.
    player.play(STATION);
    REQUIRE(held());
    CHECK(portal.calls[0].flags == 4u);
    CHECK(portal.calls[0].options.value(u"reason"_s).toString() == "Radio playback"_L1);
}

TEST_CASE_METHOD(PortalFixture, "stop closes the portal request")
{
    player.play(STATION);
    REQUIRE(held());
    player.stop();
    CHECK_FALSE(inhibitor->isHeld());
    CHECK(closed(1));
}

TEST_CASE_METHOD(PortalFixture, "a late portal reply is closed immediately")
{
    player.play(STATION);
    player.stop();
    REQUIRE(closed(1));
    CHECK_FALSE(inhibitor->isHeld());
}

TEST_CASE_METHOD(PortalFixture, "a close that outruns the portal request is retried")
{
    // Портал отвечает раньше, чем заводит объект запроса. Поздний ответ закрывается
    // сразу и получает UnknownObject; без повтора блокировка висела бы до выхода.
    portal.requestDelayMs = 150;
    player.play(STATION);
    player.stop();
    CHECK(closed(1));
}

TEST_CASE_METHOD(PortalFixture, "release retries an early close on quit too")
{
    portal.requestDelayMs = 150;
    player.play(STATION);
    REQUIRE(held());
    inhibitor->release();
    CHECK(portal.closed.size() == 1);
}

TEST_CASE_METHOD(PortalFixture, "release closes the portal request on quit")
{
    player.play(STATION);
    REQUIRE(held());
    inhibitor->release();
    // Выход ждёт доставки: к возврату из release() запрос уже закрыт.
    CHECK(portal.closed.size() == 1);
}

// MARK: - Служба питания

TEST_CASE_METHOD(LegacyFixture, "without a portal the power service is used")
{
    player.play(STATION);
    REQUIRE(held());
    CHECK(inhibitor->backend() == SleepInhibitor::Backend::PowerManagement);
    CHECK(service.inhibited == 1);
}

TEST_CASE_METHOD(LegacyFixture, "stop releases the lock with a uint cookie")
{
    player.play(STATION);
    REQUIRE(held());
    player.stop();
    CHECK_FALSE(inhibitor->isHeld());
    CHECK(released({7}));
}

TEST_CASE_METHOD(LegacyFixture, "the lock is held while the player stays active")
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

TEST_CASE_METHOD(LegacyFixture, "repeated state changes do not stack locks")
{
    player.play(STATION);
    REQUIRE(held());
    emit mpv->coreIdleChanged(false);
    emit mpv->pausedForCacheChanged(true);

    QTest::qWait(100);
    CHECK(service.inhibited == 1);
}

TEST_CASE_METHOD(LegacyFixture, "a late cookie is released immediately")
{
    // Ответ пришёл, когда играть уже перестали — cookie нельзя оставлять висеть.
    // Первый цикл выясняет, что портала нет; второй проверяет поздний ответ службы.
    player.play(STATION);
    REQUIRE(held());
    player.stop();
    REQUIRE(released({7}));

    player.play(STATION);
    player.stop();
    CHECK(released({7, 8}));
    CHECK_FALSE(inhibitor->isHeld());
}

TEST_CASE_METHOD(LegacyFixture, "playback stopped before the portal answers takes no lock")
{
    // Пока выяснялось, что портала нет, играть перестали: брать блокировку
    // у службы питания уже незачем.
    player.play(STATION);
    player.stop();
    QTest::qWait(200);
    CHECK(service.inhibited == 0);
    CHECK_FALSE(inhibitor->isHeld());
}

TEST_CASE_METHOD(LegacyFixture, "release drops the lock on quit")
{
    player.play(STATION);
    REQUIRE(held());
    inhibitor->release();
    CHECK(service.released == QList<uint>{7});
}

TEST_CASE_METHOD(PowerFixture, "missing power services do not break playback")
{
    start();
    player.play(STATION);
    QTest::qWait(200);
    CHECK_FALSE(inhibitor->isHeld());
    CHECK(player.isActive());
}
