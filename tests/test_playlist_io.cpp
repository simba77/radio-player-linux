// Импорт и экспорт списков станций.

#include "playlist_io.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const QString M3U = uR"(#EXTM3U
#EXTINF:-1,Radio Record — Main
https://example.org/record/playlist.m3u8
# просто комментарий
#EXTINF:-1,Станция, с запятой
https://example.org/two.mp3
https://example.org/three.aac
/local/file.mp3
)"_s;

const QString PLS = uR"([playlist]
NumberOfEntries=2
File1=https://example.org/one.mp3
Title1=Первая
Length1=-1
File2=https://example.org/two.mp3
Title2=Вторая
)"_s;

const QString MACOS_JSON = uR"([
  {"id": "5F5E1E9A-0000-0000-0000-000000000001", "name": "Lady Waks", "urlString": "https://example.org/lw.m3u8"},
  {"id": "5F5E1E9A-0000-0000-0000-000000000002", "name": "Main", "urlString": "https://example.org/main.m3u8"}
])"_s;

QStringList names(const QList<Station> &stations)
{
    QStringList out;
    for (const Station &station : stations)
        out.append(station.name);
    return out;
}

QStringList urls(const QList<Station> &stations)
{
    QStringList out;
    for (const Station &station : stations)
        out.append(station.url);
    return out;
}

}  // namespace

TEST_CASE("m3u gives names and urls")
{
    const QList<Station> stations = playlist::parse(M3U);
    CHECK(names(stations) ==
          QStringList{u"Radio Record — Main"_s, u"Станция, с запятой"_s, u"example.org"_s});
    CHECK(stations[0].url == "https://example.org/record/playlist.m3u8"_L1);
}

TEST_CASE("m3u skips local files")
{
    // Плеер работает с сетевыми потоками — локальные пути не импортируем.
    const QStringList found = urls(playlist::parse(M3U));
    CHECK(std::all_of(found.cbegin(), found.cend(),
                      [](const QString &url) { return url.startsWith("http"_L1); }));
}

TEST_CASE("pls pairs titles with urls")
{
    const QList<Station> stations = playlist::parse(PLS);
    CHECK(names(stations) == QStringList{u"Первая"_s, u"Вторая"_s});
    CHECK(urls(stations) == QStringList{u"https://example.org/one.mp3"_s, u"https://example.org/two.mp3"_s});
}

TEST_CASE("pls is detected by extension without a header")
{
    const QList<Station> stations =
        playlist::parse(u"File1=https://example.org/x.mp3\nTitle1=X\n"_s, u"list.pls"_s);
    CHECK(names(stations) == QStringList{u"X"_s});
}

TEST_CASE("json of the macOS version is read")
{
    const QList<Station> stations = playlist::parse(MACOS_JSON);
    CHECK(names(stations) == QStringList{u"Lady Waks"_s, u"Main"_s});
    CHECK(stations[0].url == "https://example.org/lw.m3u8"_L1);
}

TEST_CASE("imported stations become local")
{
    // Чужие id не должны притворяться станциями каталога.
    const QList<Station> stations = playlist::parse(MACOS_JSON);
    CHECK(std::all_of(stations.cbegin(), stations.cend(), [](const Station &s) { return s.isCustom(); }));
}

TEST_CASE("broken input gives nothing")
{
    CHECK(playlist::parse(u"{не json"_s).isEmpty());
    CHECK(playlist::parse(QString()).isEmpty());
}

TEST_CASE("m3u export survives a roundtrip")
{
    const QList<Station> original{
        Station::local(u"Первая"_s, u"https://example.org/one.mp3"_s),
        Station::local(u"Вторая"_s, u"https://example.org/two.m3u8"_s),
    };
    const QList<Station> restored = playlist::parse(playlist::exportM3u(original));
    CHECK(names(restored) == names(original));
    CHECK(urls(restored) == urls(original));
}

TEST_CASE("a missing file gives nothing")
{
    CHECK(playlist::readFile(test::tempDir() + u"/нет.m3u"_s).isEmpty());
}
