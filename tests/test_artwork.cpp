// Обложки: загрузка, кэш на диске и его потолок.
//
// Радио меняет трек раз в несколько минут, то есть сотни обложек за сутки эфира:
// без потолка каталог рос бы всё время, пока приложением пользуются.

#include "artwork.hpp"
#include "http_server.hpp"
#include "paths.hpp"
#include "support.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

QString cacheDir()
{
    return test::tempDir() + u"/artwork"_s;
}

QString add(const QString &name, int size, int ageSeconds)
{
    const QString path = cacheDir() + u'/' + name;
    test::writeFile(path, QByteArray(size, 'x'));
    // Возраст задаём временем изменения — по нему и выбираются жертвы.
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadWrite));
    REQUIRE(file.setFileTime(QDateTime::fromSecsSinceEpoch(1'700'000'000 - ageSeconds),
                             QFileDevice::FileModificationTime));
    return path;
}

QByteArray png()
{
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

}  // namespace

// MARK: - Потолок кэша

TEST_CASE("a cache within the limit is untouched")
{
    const QString file = add(u"a.jpg"_s, 100, 1000);
    CHECK(pruneArtworkCache(cacheDir(), 1000) == 0);
    CHECK(QFile::exists(file));
}

TEST_CASE("the oldest artwork goes first")
{
    const QString old = add(u"old.jpg"_s, 400, 9000);
    const QString middle = add(u"middle.jpg"_s, 400, 5000);
    const QString fresh = add(u"fresh.jpg"_s, 400, 10);

    CHECK(pruneArtworkCache(cacheDir(), 800) == 1);
    CHECK_FALSE(QFile::exists(old));
    CHECK(QFile::exists(middle));
    CHECK(QFile::exists(fresh));
}

TEST_CASE("pruning stops at the limit")
{
    for (int index = 0; index < 10; ++index)
        add(u"%1.jpg"_s.arg(index), 100, 1000 - index);

    pruneArtworkCache(cacheDir(), 500);
    qint64 total = 0;
    for (const QFileInfo &entry : QDir(cacheDir()).entryInfoList(QDir::Files))
        total += entry.size();
    CHECK(total <= 500);
    // Свежие обложки нужны прямо сейчас: их показывает окно и отдаёт MPRIS.
    CHECK(QFile::exists(cacheDir() + u"/9.jpg"_s));
}

TEST_CASE("a missing cache directory is not an error")
{
    CHECK(pruneArtworkCache(test::tempDir() + u"/never-created"_s, 10) == 0);
}

// MARK: - Загрузка

TEST_CASE("artwork is downloaded once and then read from the cache")
{
    test::HttpServer server;
    server.respond(u"/cover.png"_s, {200, png(), {}});
    const QString url = server.url(u"/cover.png"_s);

    ArtworkLoader loader;
    QSignalSpy ready(&loader, &ArtworkLoader::artworkReady);
    loader.load(url);
    REQUIRE(ready.wait(5000));
    CHECK(ready[0][1].toString() == ArtworkLoader::cachePath(url));
    CHECK(QFile::exists(ArtworkLoader::cachePath(url)));
    CHECK_FALSE(ready[0][2].value<QImage>().isNull());

    loader.load(url);
    CHECK(ready.count() == 2);  // Из кэша — сразу, без сети.
    CHECK(server.requests(u"/cover.png"_s).size() == 1);
}

TEST_CASE("something that is not an image is not cached")
{
    test::HttpServer server;
    server.respond(u"/cover.png"_s, {200, "<html>not found</html>", {}});
    const QString url = server.url(u"/cover.png"_s);

    ArtworkLoader loader;
    QSignalSpy ready(&loader, &ArtworkLoader::artworkReady);
    loader.load(url);
    CHECK_FALSE(ready.wait(500));
    CHECK_FALSE(QFile::exists(ArtworkLoader::cachePath(url)));
}

TEST_CASE("the cache file keeps the webp extension")
{
    CHECK(ArtworkLoader::cachePath(u"https://example.org/a.webp?size=300"_s).endsWith(".webp"_L1));
    CHECK(ArtworkLoader::cachePath(u"https://example.org/a.jpeg"_s).endsWith(".jpg"_L1));
}

TEST_CASE("artwork cached by version 1.x is found")
{
    // Имя — sha1 адреса, как и раньше: обложки переживают обновление.
    CHECK(ArtworkLoader::cachePath(u"https://example.org/cover.jpg"_s)
              .endsWith(u"/5a901b9ca2726d9146ea5543c30ced6e4df57329.jpg"_s));
}
