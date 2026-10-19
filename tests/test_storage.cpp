// Чтение и запись пользовательских файлов.
//
// Главный тест здесь — про независимых писателей `state.json`. Ошибка не видна
// глазами: файл остаётся валидным, приложение работает, просто чужой ключ тихо
// исчезает, и каталоги начинают обновляться при каждом запуске.

#include "paths.hpp"
#include "storage.hpp"
#include "support.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

void writeDocument(const QJsonObject &data)
{
    test::writeFile(paths::stateFile(), QJsonDocument(data).toJson());
}

}  // namespace

TEST_CASE("load state returns an empty object without the file")
{
    CHECK(storage::loadState().isEmpty());
}

TEST_CASE("load state survives a broken file")
{
    test::writeFile(paths::stateFile(), "{ это не json");
    CHECK(storage::loadState().isEmpty());
}

TEST_CASE("update state creates the file")
{
    CHECK(storage::updateState({{"volume"_L1, 70}}));
    CHECK(storage::loadState() == QJsonObject{{"volume"_L1, 70}});
}

TEST_CASE("update state keeps untouched keys")
{
    storage::updateState({{"volume"_L1, 70}});
    storage::updateState({{"last_station_id"_L1, "record-main"_L1}});
    CHECK(storage::loadState() == QJsonObject{{"volume"_L1, 70}, {"last_station_id"_L1, "record-main"_L1}});
}

TEST_CASE("independent writers do not clobber each other")
{
    // Плеер и обновление каталогов пишут в файл, ничего не зная друг о друге.
    // Плеер раньше держал копию состояния с момента запуска и сохранял её
    // целиком, стирая метку обновления каталогов, поставленную позже.
    storage::updateState({{"volume"_L1, 70}});
    storage::updateState({{"catalogs_updated_at"_L1, 1234567890.0}});
    storage::updateState({{"volume"_L1, 71}});

    CHECK(storage::loadState() == QJsonObject{{"volume"_L1, 71}, {"catalogs_updated_at"_L1, 1234567890.0}});
}

TEST_CASE("write json is atomic and leaves no temporary file")
{
    const QString path = test::tempDir() + u"/data.json"_s;
    CHECK(storage::writeJson(path, QJsonObject{{"a"_L1, 1}}));
    CHECK(storage::readJson(path) == QJsonObject{{"a"_L1, 1}});
    CHECK(QDir(test::tempDir()).entryList(QDir::Files) == QStringList{u"data.json"_s});
}

TEST_CASE("write json reports failure")
{
    // Внутрь файла записать нельзя — родитель не каталог.
    const QString blocker = test::tempDir() + u"/blocker"_s;
    test::writeFile(blocker, {});
    CHECK_FALSE(storage::writeJson(blocker + u"/data.json"_s, QJsonObject{{"a"_L1, 1}}));
}

// MARK: - Версия схемы

TEST_CASE("document of a known schema is read")
{
    writeDocument({{"schema"_L1, 1}, {"close_to_tray"_L1, false}});
    CHECK(storage::readDocument(paths::stateFile(), 1) ==
          QJsonObject{{"schema"_L1, 1}, {"close_to_tray"_L1, false}});
}

TEST_CASE("document without a schema is read")
{
    // Файлы ранних версий поля не имели — терять их не за что.
    writeDocument({{"close_to_tray"_L1, false}});
    CHECK(storage::readDocument(paths::stateFile(), 1) == QJsonObject{{"close_to_tray"_L1, false}});
}

TEST_CASE("document from the future is set aside")
{
    // Откат на старую версию не должен тихо стирать настройки новой.
    writeDocument({{"schema"_L1, 99}, {"future_option"_L1, true}});
    const QString path = paths::stateFile();

    CHECK(storage::readDocument(path, 1).isEmpty());
    CHECK_FALSE(QFile::exists(path));
    const QString backup = paths::configDir() + u"/state.bak-v99"_s;
    CHECK(storage::readJson(backup).toObject().value("future_option"_L1).toBool());
}

TEST_CASE("missing document is empty")
{
    CHECK(storage::readDocument(paths::stateFile(), 1).isEmpty());
}
