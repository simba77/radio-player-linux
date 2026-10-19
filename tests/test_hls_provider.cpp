// Метаданные HLS: разбор манифеста и опрос по сети.
//
// Разбор — чистые функции. Опрос проверяется на локальном HTTP-сервере с коротким
// интервалом: так видно и то, из-за чего провайдер зря нагружал чужие серверы, —
// он выбирается по расширению `.m3u8`, а тег `#EXT-X-DATERANGE` отдаёт только
// hostingradio.

#include "http_server.hpp"
#include "metadata/hls_daterange.hpp"
#include "support.hpp"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const QString VARIANT_PLAYLIST = uR"(#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:6
#EXT-X-PROGRAM-DATE-TIME:2026-08-10T20:09:11.371Z
#EXT-X-DATERANGE:ID="6a7a2f97",CLASS="ru.hostingradio.meta",START-DATE="2026-08-10T20:07:50.657Z",END-ON-NEXT=YES,X-ARTIST="BLOND_ISH",X-TITLE="$MONEY",X-METADATA-URI="metadata/6a7a2f97.json"
#EXT-X-DISCONTINUITY
#EXTINF:5.990748,
l0_6a7a2ff0.aac
)"_s;

const QByteArray PLAIN_MANIFEST = "#EXTM3U\n#EXT-X-TARGETDURATION:6\nsegment1.ts\n";
const QByteArray MASTER = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=128000\nvariant.m3u8?hlssid=xyz\n";

QByteArray manifestWithTrack(const char *id, const char *metadataUri = nullptr)
{
    QByteArray manifest = "#EXTM3U\n#EXT-X-DATERANGE:ID=\"" + QByteArray(id) +
                          "\",CLASS=\"ru.hostingradio.meta\",X-ARTIST=\"Кто-то\",X-TITLE=\"Что-то\"";
    if (metadataUri != nullptr)
        manifest += ",X-METADATA-URI=\"" + QByteArray(metadataUri) + "\"";
    return manifest + "\nsegment1.ts\n";
}

constexpr int POLL_MS = 20;

// Провайдер, опрашивающий станцию на локальном сервере.
struct ProviderFixture {
    test::HttpServer server;
    HlsDaterangeProvider provider{nullptr, POLL_MS};
    QSignalSpy tracks{&provider, &MetadataProvider::trackChanged};

    ProviderFixture()
    {
        server.respond(u"/live/playlist.m3u8"_s, {200, MASTER, {}});
        server.respond(u"/live/variant.m3u8?hlssid=xyz"_s, {200, PLAIN_MANIFEST, {}});
    }
    ~ProviderFixture() { provider.stop(); }

    void start() { provider.start({u"Станция"_s, server.url(u"/live/playlist.m3u8"_s), u"s"_s}); }
    void serveVariant(const QByteArray &body)
    {
        server.respond(u"/live/variant.m3u8?hlssid=xyz"_s, {200, body, {}});
    }
    qsizetype polls() const { return server.requests(u"/live/variant.m3u8?hlssid=xyz"_s).size(); }
    bool polling() const { return test::timer(&provider, u"pollTimer"_s)->isActive(); }
};

}  // namespace

// MARK: - Разбор

TEST_CASE("daterange gives the track attributes")
{
    const auto attributes = hls::parseDaterange(VARIANT_PLAYLIST);
    REQUIRE(attributes);
    CHECK(attributes->value(u"ID"_s) == "6a7a2f97"_L1);
    CHECK(attributes->value(u"X-ARTIST"_s) == "BLOND_ISH"_L1);
    CHECK(attributes->value(u"X-TITLE"_s) == "$MONEY"_L1);
    CHECK(attributes->value(u"X-METADATA-URI"_s) == "metadata/6a7a2f97.json"_L1);
}

TEST_CASE("a daterange without track info is ignored")
{
    CHECK_FALSE(
        hls::parseDaterange(u"#EXTM3U\n#EXT-X-DATERANGE:ID=\"x\",CLASS=\"com.apple.hls.interstitial\"\n"_s));
}

TEST_CASE("a playlist without daterange has no track")
{
    CHECK_FALSE(hls::parseDaterange(u"#EXTM3U\n#EXTINF:6.0,\nseg.aac\n"_s));
}

TEST_CASE("the master playlist gives the variant")
{
    CHECK(hls::variantUri(QString::fromUtf8(MASTER)) == "variant.m3u8?hlssid=xyz"_L1);
    CHECK(hls::variantUri(u"#EXTM3U\r\n#EXT-X-STREAM-INF:BANDWIDTH=1\r\nv.m3u8\r\n"_s) == "v.m3u8"_L1);
    CHECK(hls::variantUri(QString::fromUtf8(PLAIN_MANIFEST)).isEmpty());
}

TEST_CASE("the cover prefers 300 pixels")
{
    CHECK(hls::extractCover(R"({"current": {"title": "X",
        "coverImageUrl100": "https://example.org/100.jpg",
        "coverImageUrl300": "https://example.org/300.jpg",
        "coverImageUrl600": "https://example.org/600.jpg"}})") == "https://example.org/300.jpg"_L1);
}

TEST_CASE("a track without artwork has no cover")
{
    // Часть станций отдаёт current без обложки — это штатная ситуация.
    CHECK(hls::extractCover(R"({"current": {"artist": "BLOND_ISH", "title": "$MONEY"}})").isEmpty());
}

TEST_CASE("broken json has no cover")
{
    CHECK(hls::extractCover("не json").isEmpty());
}

// MARK: - Опрос

TEST_CASE_METHOD(ProviderFixture, "the variant with hlssid is polled")
{
    // Ссылка на variant несёт hlssid — без него DATERANGE не отдаётся.
    start();
    CHECK(test::waitUntil([&] { return polls() >= 2; }));
}

TEST_CASE_METHOD(ProviderFixture, "a media playlist without variants is polled directly")
{
    server.respond(u"/live/playlist.m3u8"_s, {200, manifestWithTrack("abc"), {}});
    start();
    REQUIRE(tracks.wait(3000));
    CHECK(test::waitUntil([&] { return server.requests(u"/live/playlist.m3u8"_s).size() >= 3; }));
}

TEST_CASE_METHOD(ProviderFixture, "the track is read from the manifest")
{
    serveVariant(manifestWithTrack("abc"));
    start();
    REQUIRE(tracks.wait(3000));
    const auto track = tracks[0][0].value<TrackInfo>();
    CHECK(track.artist == u"Кто-то"_s);
    CHECK(track.title == u"Что-то"_s);
    // Тот же DATERANGE в следующих манифестах — не новый трек.
    REQUIRE(test::waitUntil([&] { return polls() >= 4; }));
    CHECK(tracks.count() == 1);
}

TEST_CASE_METHOD(ProviderFixture, "the cover arrives after the title")
{
    server.respond(u"/live/metadata/abc.json"_s,
                   {200, R"({"current": {"coverImageUrl300": "https://example.org/300.jpg"}})", {}});
    serveVariant(manifestWithTrack("abc", "metadata/abc.json"));
    start();
    REQUIRE(test::waitUntil([&] { return tracks.count() >= 2; }));
    CHECK(tracks[0][0].value<TrackInfo>().artworkUrl.isEmpty());
    CHECK(tracks[1][0].value<TrackInfo>().artworkUrl == "https://example.org/300.jpg"_L1);
}

TEST_CASE_METHOD(ProviderFixture, "polling continues while metadata appears")
{
    serveVariant(manifestWithTrack("abc"));
    start();
    REQUIRE(test::waitUntil([&] { return polls() >= HLS_EMPTY_POLLS_BEFORE_GIVING_UP * 2; }));
    CHECK(polling());
}

TEST_CASE_METHOD(ProviderFixture, "polling stops when the station never has metadata")
{
    // Час прослушивания чужой HLS-станции — 720 бесполезных запросов.
    start();
    // Таймер запускается после ответа мастер-плейлиста — сперва дожидаемся опроса.
    REQUIRE(test::waitUntil([&] { return polling(); }));
    REQUIRE(test::waitUntil([&] { return !polling(); }));
    const qsizetype stopped = polls();
    CHECK(stopped >= HLS_EMPTY_POLLS_BEFORE_GIVING_UP);
    QTest::qWait(POLL_MS * 5);
    CHECK(polls() == stopped);
}

TEST_CASE_METHOD(ProviderFixture, "a single track resets the patience")
{
    // Метаданные могут пропадать между треками — это не повод сдаваться.
    start();
    REQUIRE(test::waitUntil([&] { return polls() >= HLS_EMPTY_POLLS_BEFORE_GIVING_UP - 2; }));
    serveVariant(manifestWithTrack("abc"));
    REQUIRE(tracks.wait(3000));
    serveVariant(PLAIN_MANIFEST);

    const qsizetype afterTrack = polls();
    REQUIRE(test::waitUntil([&] { return polls() >= afterTrack + HLS_EMPTY_POLLS_BEFORE_GIVING_UP - 2; }));
    CHECK(polling());
}

TEST_CASE_METHOD(ProviderFixture, "failing polls refresh the master playlist")
{
    // Параметр hlssid протухает: после нескольких неудач variant ищется заново.
    server.respond(u"/live/variant.m3u8?hlssid=xyz"_s, {500, "gone", {}});
    start();
    CHECK(test::waitUntil([&] { return server.requests(u"/live/playlist.m3u8"_s).size() >= 2; }));
}

TEST_CASE_METHOD(ProviderFixture, "stop clears the state")
{
    QSignalSpy cleared(&provider, &MetadataProvider::trackCleared);
    serveVariant(manifestWithTrack("abc"));
    start();
    REQUIRE(tracks.wait(3000));
    provider.stop();

    CHECK_FALSE(polling());
    CHECK(cleared.count() == 1);
    const qsizetype stopped = polls();
    QTest::qWait(POLL_MS * 5);
    CHECK(polls() == stopped);
}
