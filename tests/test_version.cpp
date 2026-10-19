// Версия продублирована в нескольких файлах, и разъезжаются они молча.
//
// Источник истины — `project(VERSION)` в CMakeLists.txt, откуда версия попадает
// в программу и в имя пакета. Расхождение проявляется поздно и неприятно:
// в `.deb` одна версия, в `--version` другая, а магазины приложений показывают
// третью из AppStream.

#include "app_info.hpp"
#include "support.hpp"

#include <QXmlStreamReader>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const QString VERSION = QLatin1String(APP_VERSION);

}  // namespace

TEST_CASE("the Debian changelog matches the version")
{
    const QString changelog =
        QString::fromUtf8(test::readFile(test::sourcePath(u"packaging/deb/changelog"_s)));
    CHECK(changelog.startsWith(u"radio-player (%1)"_s.arg(VERSION)));
}

TEST_CASE("AppStream lists the release first")
{
    // Свежий выпуск идёт первым: AppStream показывает список сверху вниз.
    QXmlStreamReader xml(test::readFile(
        test::sourcePath(u"packaging/appstream/io.github.simba77.RadioPlayer.metainfo.xml"_s)));
    QString first;
    while (!xml.atEnd() && first.isEmpty()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == "release"_L1)
            first = xml.attributes().value("version"_L1).toString();
    }
    CHECK(first == VERSION);
}

TEST_CASE("the changelog has a section for the version")
{
    const QString changelog = QString::fromUtf8(test::readFile(test::sourcePath(u"CHANGELOG.md"_s)));
    CHECK(changelog.contains(u"## [%1]"_s.arg(VERSION)));
}
