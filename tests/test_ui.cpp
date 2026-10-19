// Интерфейс: сборка окна и реакции на состояние плеера.
//
// Интерфейс собирается целиком под offscreen-платформой, без дисплея и без
// звука; клики не нужны — сигналы испускаются напрямую. Виджеты находятся по
// именам объектов, а не через приватные поля.

#include "catalog.hpp"
#include "fakes.hpp"
#include "icons.hpp"
#include "main_window.hpp"
#include "player.hpp"
#include "settings/settings_dialog.hpp"
#include "settings_store.hpp"
#include "station_store.hpp"
#include "support.hpp"
#include "tray.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const Station FIRST{u"Первая"_s, u"https://radio.invalid/first"_s, u"first"_s};
const Station SECOND{u"Вторая"_s, u"https://radio.invalid/second.m3u8"_s, u"second"_s};

// Приложение, собранное как в main(), но без сети, звука и шины.
struct AppFixture {
    StationStore store{{Catalog{u"test"_s, u"Тест"_s, 1, {FIRST, SECOND}}}};
    SettingsStore settings;
    test::FakeMpv *mpv = new test::FakeMpv;
    PlayerManager player{&store, mpv};
    CatalogUpdater updater{settings.extraCatalogUrls()};
    MainWindow window{&player, &store, &settings, &updater};

    ~AppFixture() { player.shutdown(); }

    template <typename T>
    T *find(const char *name) const
    {
        auto *found = window.findChild<T *>(QString::fromLatin1(name));
        REQUIRE(found != nullptr);
        return found;
    }

    QStringList rows() const
    {
        const auto *list = find<QListWidget>("stationList");
        QStringList out;
        for (int row = 0; row < list->count(); ++row)
            out.append(list->item(row)->text());
        return out;
    }

    void click(int row) const
    {
        auto *list = find<QListWidget>("stationList");
        emit list->itemClicked(list->item(row));
    }
};

}  // namespace

// MARK: - Главное окно

TEST_CASE_METHOD(AppFixture, "the window lists the stations")
{
    CHECK(rows() == QStringList{u"Первая"_s, u"Вторая"_s});
}

TEST_CASE("an empty list shows a placeholder")
{
    StationStore store;
    SettingsStore settings;
    PlayerManager player(&store, new test::FakeMpv);
    CatalogUpdater updater;
    MainWindow window(&player, &store, &settings, &updater);

    const auto *list = window.findChild<QListWidget *>(u"stationList"_s);
    REQUIRE(list->count() == 1);
    CHECK(list->item(0)->text() == "The station list is empty"_L1);
    player.shutdown();
}

TEST_CASE_METHOD(AppFixture, "clicking a station starts it")
{
    click(1);
    CHECK(player.currentStation() == SECOND);
    CHECK(mpv->played == QStringList{SECOND.url});
}

TEST_CASE_METHOD(AppFixture, "clicking the playing station stops it")
{
    click(0);
    emit mpv->coreIdleChanged(false);
    click(0);
    CHECK_FALSE(player.isActive());
}

TEST_CASE_METHOD(AppFixture, "new stations reach the window")
{
    store.addCustom(u"Своя"_s, u"https://radio.invalid/mine"_s);
    CHECK(rows().contains(u"Своя"_s));
}

// MARK: - «Сейчас играет»

TEST_CASE_METHOD(AppFixture, "now playing shows the station, then the track")
{
    player.play(FIRST);
    emit mpv->coreIdleChanged(false);
    CHECK(find<QLabel>("title")->text() == u"Первая"_s);

    emit mpv->metadataChanged({{u"icy-title"_s, u"Кто-то - Трек"_s}});
    CHECK(find<QLabel>("title")->text() == u"Кто-то — Трек"_s);
    CHECK(find<QLabel>("subtitle")->text() == u"Первая"_s);
}

TEST_CASE_METHOD(AppFixture, "now playing reports loading and reconnecting")
{
    player.play(FIRST);
    CHECK(find<QLabel>("title")->text() == "Loading..."_L1);

    emit mpv->coreIdleChanged(false);
    emit mpv->endFile(MpvBackend::EndReason::Error);
    CHECK(find<QLabel>("title")->text() == "Reconnecting..."_L1);
}

TEST_CASE_METHOD(AppFixture, "the play button follows the player")
{
    const auto *button = find<QPushButton>("playButton");
    CHECK(button->toolTip() == "Play"_L1);
    player.play(FIRST);
    // Кнопка показывает «стоп» уже во время загрузки: пользователь ждёт звука.
    CHECK(button->toolTip() == "Stop"_L1);
    player.stop();
    CHECK(button->toolTip() == "Play"_L1);
}

TEST_CASE_METHOD(AppFixture, "an error is shown and cleared")
{
    const auto *error = find<QLabel>("error");
    mpv->failOnPlay = true;
    player.play(FIRST);
    // isVisible() ложно для всего дерева, пока окно не показано, — спрашиваем
    // про сам виджет.
    CHECK_FALSE(error->isHidden());
    CHECK_FALSE(error->text().isEmpty());

    mpv->failOnPlay = false;
    player.play(FIRST);
    CHECK(error->text().isEmpty());
    CHECK(error->isHidden());
}

TEST_CASE_METHOD(AppFixture, "the volume slider follows external changes")
{
    player.setVolume(37);
    CHECK(find<QSlider>("volume")->value() == 37);
}

TEST_CASE_METHOD(AppFixture, "the volume slider drives the player")
{
    find<QSlider>("volume")->setValue(64);
    CHECK(player.volume() == 64);
}

// MARK: - Показ и скрытие окна

TEST_CASE("a tray click hides the window being used")
{
    CHECK(trayClickAction(true, true) == TrayClick::Hide);
}

TEST_CASE("a tray click raises a minimized window")
{
    // Регрессия: свёрнутое окно пряталось в трей вместо того, чтобы вернуться.
    // На Wayland свёрнутое окно выглядит для Qt видимым и не минимизированным.
    CHECK(trayClickAction(true, false) == TrayClick::Raise);
}

TEST_CASE("a tray click shows a window hidden to the tray")
{
    CHECK(trayClickAction(false, false) == TrayClick::Raise);
}

TEST_CASE_METHOD(AppFixture, "closing the window hides it when the tray is there")
{
    window.show();
    window.close();
    CHECK(window.isHidden());
}

// MARK: - Настройки

TEST_CASE_METHOD(AppFixture, "the settings dialog builds all tabs")
{
    window.openSettings();
    REQUIRE(window.settingsDialog() != nullptr);
    const auto *tabs = window.settingsDialog()->findChild<QTabWidget *>();
    REQUIRE(tabs != nullptr);
    QStringList titles;
    for (int index = 0; index < tabs->count(); ++index)
        titles.append(tabs->tabText(index));
    CHECK(titles == QStringList{u"General"_s, u"Stations"_s, u"Catalogs"_s});
}

TEST_CASE_METHOD(AppFixture, "the settings window is reused")
{
    window.openSettings();
    SettingsDialog *first = window.settingsDialog();
    window.openSettings();
    CHECK(window.settingsDialog() == first);
}

TEST_CASE_METHOD(AppFixture, "toggling a checkbox reaches the settings")
{
    // Полный путь: щелчок по флажку — хранилище — файл на диске.
    window.openSettings();
    QCheckBox *checkbox = nullptr;
    for (QCheckBox *box : window.settingsDialog()->findChildren<QCheckBox *>()) {
        if (box->text() == "Closing the window hides it in the tray"_L1)
            checkbox = box;
    }
    REQUIRE(checkbox != nullptr);
    CHECK(checkbox->isChecked());

    checkbox->setChecked(false);
    CHECK_FALSE(settings.closeToTray());
    CHECK_FALSE(SettingsStore().closeToTray());
}

TEST_CASE_METHOD(AppFixture, "the stations tab lists visible and hidden stations")
{
    store.hide(SECOND.id);
    window.openSettings();
    const auto *stations = window.settingsDialog()->findChild<QListWidget *>(u"stations"_s);
    REQUIRE(stations != nullptr);
    REQUIRE(stations->count() == 2);
    CHECK(stations->item(0)->text() == u"Первая"_s);
    CHECK(stations->item(0)->checkState() == Qt::Checked);
    CHECK(stations->item(1)->text() == u"Вторая"_s);
    CHECK(stations->item(1)->checkState() == Qt::Unchecked);
}

TEST_CASE_METHOD(AppFixture, "unchecking a station in the settings hides it")
{
    window.openSettings();
    auto *stations = window.settingsDialog()->findChild<QListWidget *>(u"stations"_s);
    REQUIRE(stations != nullptr);
    stations->item(0)->setCheckState(Qt::Unchecked);
    CHECK(rows() == QStringList{u"Вторая"_s});
}

TEST_CASE_METHOD(AppFixture, "the catalogs tab lists the built-in catalog")
{
    window.openSettings();
    const auto *catalogs = window.settingsDialog()->findChild<QListWidget *>(u"catalogs"_s);
    REQUIRE(catalogs != nullptr);
    CHECK(catalogs->item(0)->text() == catalog::defaultCatalogUrls().first());
    CHECK(window.settingsDialog()->findChild<QLabel *>(u"status"_s)->text() ==
          "The catalogs have never been updated."_L1);
}

// MARK: - Трей

TEST_CASE_METHOD(AppFixture, "the tray reflects playback")
{
    TrayIcon tray(&player);
    const auto *play = tray.contextMenu()->findChild<QAction *>(u"playAction"_s);
    REQUIRE(play != nullptr);
    CHECK(play->text() == "Play"_L1);

    player.play(FIRST);
    emit mpv->coreIdleChanged(false);
    CHECK(play->text() == "Stop"_L1);
    CHECK(tray.toolTip() == u"Первая"_s);
}

// MARK: - Иконки

TEST_CASE("the application icon is found")
{
    // Иконка ставится в hicolor, а без установки берётся встроенная.
    CHECK_FALSE(icons::get(icons::radio()).isNull());
}

TEST_CASE("an unknown icon stays empty")
{
    CHECK(icons::get(u"совершенно-несуществующая-иконка"_s).isNull());
}
