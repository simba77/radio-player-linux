// Настоящий libmpv: события доходят из его потока в поток Qt.
//
// Остальные тесты плеера идут на двойнике, а здесь проверяется сам мост —
// колбэк пробуждения и разбор очереди событий. Звук не нужен: битый поток
// заканчивается раньше, чем mpv дойдёт до аудиовывода.

#include "http_server.hpp"
#include "mpv_backend.hpp"
#include "support.hpp"

#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

TEST_CASE("libmpv reports a broken stream as an error")
{
    test::HttpServer server;
    LibMpv mpv;
    QSignalSpy ended(&mpv, &MpvBackend::endFile);

    CHECK(mpv.play(server.url(u"/missing.mp3"_s)).isEmpty());
    REQUIRE(ended.wait(10'000));
    CHECK(ended[0][0].value<MpvBackend::EndReason>() == MpvBackend::EndReason::Error);
}

TEST_CASE("terminated libmpv refuses to play")
{
    LibMpv mpv;
    mpv.terminate();
    mpv.terminate();
    CHECK_FALSE(mpv.play(u"https://radio.invalid/stream.mp3"_s).isEmpty());
    mpv.stop();
    mpv.setVolume(50);
}
