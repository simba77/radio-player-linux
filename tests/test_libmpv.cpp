// Настоящий libmpv: события доходят из его потока в поток Qt.
//
// Остальные тесты плеера идут на двойнике, а здесь проверяется сам мост —
// колбэк пробуждения и разбор очереди событий. Звуковая карта не нужна: битый
// поток заканчивается раньше, чем mpv дойдёт до аудиовывода, а настоящий
// проигрывается в `ao=null`.

#include "http_server.hpp"
#include "mpv_backend.hpp"
#include "support.hpp"

#include <QSignalSpy>
#include <QtEndian>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

// Секунда тишины в WAV: 8 кГц, 16 бит, моно.
QByteArray silence()
{
    constexpr quint32 rate = 8000;
    constexpr quint32 dataSize = rate * 2;
    QByteArray wav;
    auto add32 = [&](quint32 value) {
        wav.append(reinterpret_cast<const char *>(&value), 4);
    };
    auto add16 = [&](quint16 value) {
        wav.append(reinterpret_cast<const char *>(&value), 2);
    };
    wav.append("RIFF");
    add32(qToLittleEndian<quint32>(36 + dataSize));
    wav.append("WAVEfmt ");
    add32(qToLittleEndian<quint32>(16));
    add16(qToLittleEndian<quint16>(1));  // PCM
    add16(qToLittleEndian<quint16>(1));  // моно
    add32(qToLittleEndian(rate));
    add32(qToLittleEndian<quint32>(rate * 2));
    add16(qToLittleEndian<quint16>(2));
    add16(qToLittleEndian<quint16>(16));
    wav.append("data");
    add32(qToLittleEndian(dataSize));
    wav.append(QByteArray(dataSize, '\0'));
    return wav;
}

}  // namespace

TEST_CASE("libmpv reports a broken stream as an error")
{
    test::HttpServer server;
    LibMpv mpv;
    QSignalSpy ended(&mpv, &MpvBackend::endFile);

    CHECK(mpv.play(server.url(u"/missing.mp3"_s)).isEmpty());
    REQUIRE(ended.wait(10'000));
    CHECK(ended[0][0].value<MpvBackend::EndReason>() == MpvBackend::EndReason::Error);
}

TEST_CASE("libmpv confirms the sound and reports the end of a stream")
{
    // На этих двух событиях держится состояние плеера: «играет» — только когда
    // mpv подтвердил звук, а конец живого потока — это обрыв.
    test::HttpServer server;
    server.respond(u"/silence.wav"_s, {200, silence(), {{"Content-Type", "audio/wav"}}});
    LibMpv mpv({{"ao", "null"}});
    QSignalSpy idle(&mpv, &MpvBackend::coreIdleChanged);
    QSignalSpy ended(&mpv, &MpvBackend::endFile);

    CHECK(mpv.play(server.url(u"/silence.wav"_s)).isEmpty());
    REQUIRE(test::waitUntil(
        [&] {
            return std::any_of(idle.cbegin(), idle.cend(),
                               [](const QList<QVariant> &args) { return !args[0].toBool(); });
        },
        10'000));
    REQUIRE(test::waitUntil([&] { return !ended.isEmpty(); }, 10'000));
    CHECK(ended[0][0].value<MpvBackend::EndReason>() == MpvBackend::EndReason::Eof);
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
