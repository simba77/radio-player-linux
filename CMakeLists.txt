cmake_minimum_required(VERSION 3.22)
project(radio-player VERSION 1.0.0 LANGUAGES CXX
        DESCRIPTION "Internet radio player for Linux"
        HOMEPAGE_URL "https://github.com/simba77/radio-player-linux")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RADIO_BUILD_TESTS "Собирать тесты" ON)
option(RADIO_SANITIZE "Собирать с AddressSanitizer и UndefinedBehaviorSanitizer" OFF)
option(RADIO_COVERAGE "Собирать с измерением покрытия (gcov)" OFF)

# Qt 6.4 — то, что есть в Ubuntu 24.04. CI собирает там, поэтому API новее
# использовать нельзя, даже если локально стоит свежая версия.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Gui LinguistTools Network Widgets)
qt_standard_project_setup()
find_package(PkgConfig REQUIRED)
# libmpv 0.36 — клиентский API 2.1.
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv>=2.1)

add_compile_options(-Wall -Wextra -Wpedantic)
if(RADIO_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()
if(RADIO_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()
add_compile_definitions(QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING)

configure_file(src/core/app_info.hpp.in ${CMAKE_BINARY_DIR}/generated/app_info.hpp @ONLY)

# Слои — отдельные библиотеки, у каждой свой каталог заголовков. Слою видно только
# то, с чем он связан, поэтому запрещённая зависимость ломает сборку, а не ждёт ревью.
add_library(radio_core STATIC
    src/core/catalog.cpp
    src/core/json_values.cpp
    src/core/models.cpp
    src/core/paths.cpp
    src/core/playlist_io.cpp
    src/core/settings.cpp
    src/core/settings_store.cpp
    src/core/station_store.cpp
    src/core/storage.cpp
)
target_include_directories(radio_core PUBLIC src/core ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(radio_core PUBLIC Qt6::Core Qt6::Network)
# Встроенная копия каталога: приложение работает сразу и без сети.
qt_add_resources(radio_core "catalogs"
    PREFIX "/catalogs"
    BASE radio_player/resources/catalogs
    FILES radio_player/resources/catalogs/record.json
)

# Воспроизведение: libmpv, состояния плеера, метаданные и обложки. Окон не знает.
add_library(radio_playback STATIC
    src/playback/artwork.cpp
    src/playback/metadata/hls_daterange.cpp
    src/playback/metadata/icy.cpp
    src/playback/metadata/provider.cpp
    src/playback/mpv_backend.cpp
    src/playback/player.cpp
)
target_include_directories(radio_playback PUBLIC src/playback)
target_link_libraries(radio_playback PUBLIC radio_core Qt6::Gui PRIVATE PkgConfig::MPV)

# Интеграция с рабочим столом: MPRIS, запрет засыпания, сеть, single instance,
# автозапуск. Всё здесь обязано деградировать молча и не знает об окнах.
add_library(radio_desktop STATIC
    src/desktop/autostart.cpp
    src/desktop/mpris.cpp
    src/desktop/network.cpp
    src/desktop/power.cpp
    src/desktop/single_instance.cpp
)
target_include_directories(radio_desktop PUBLIC src/desktop)
target_link_libraries(radio_desktop PUBLIC radio_playback Qt6::DBus)

# Интерфейс: окно, трей, настройки, язык. Единственный слой, знающий о виджетах.
add_library(radio_ui STATIC
    src/ui/i18n.cpp
    src/ui/icons.cpp
    src/ui/main_window.cpp
    src/ui/now_playing.cpp
    src/ui/settings/catalogs_tab.cpp
    src/ui/settings/general_tab.cpp
    src/ui/settings/settings_dialog.cpp
    src/ui/settings/stations_tab.cpp
    src/ui/tray.cpp
)
target_include_directories(radio_ui PUBLIC src/ui)
target_link_libraries(radio_ui PUBLIC radio_desktop Qt6::Widgets)
# Иконка приложения на случай запуска без установки: в теме её ещё нет.
qt_add_resources(radio_ui "icons"
    PREFIX "/icons"
    BASE radio_player/resources/icons/hicolor/scalable/apps
    FILES radio_player/resources/icons/hicolor/scalable/apps/io.github.simba77.RadioPlayer.svg
)

# Переводы собираются при сборке и встраиваются в программу. Английский каталог
# содержит только формы множественного числа: исходные строки уже английские.
set(RADIO_TS_RU ${CMAKE_SOURCE_DIR}/translations/cpp/radio_player_ru.ts)
set(RADIO_TS_EN ${CMAKE_SOURCE_DIR}/translations/cpp/radio_player_en.ts)
qt_add_lrelease(radio_ui TS_FILES ${RADIO_TS_RU} ${RADIO_TS_EN} QM_FILES_OUTPUT_VARIABLE RADIO_QM_FILES)
qt_add_resources(radio_ui "translations"
    PREFIX "/i18n"
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${RADIO_QM_FILES}
)
add_custom_target(i18n_update
    COMMAND Qt6::lupdate -no-obsolete ${CMAKE_SOURCE_DIR}/src -ts ${RADIO_TS_RU}
    COMMAND Qt6::lupdate -no-obsolete -pluralonly ${CMAKE_SOURCE_DIR}/src -ts ${RADIO_TS_EN}
    COMMENT "Обновление каталогов переводов строками из кода"
    VERBATIM
)

# Точка входа: разбор аргументов, связки подсистем, сигналы Unix.
add_library(radio_app STATIC
    src/app/cli.cpp
    src/app/unix_signals.cpp
    src/app/wiring.cpp
)
target_include_directories(radio_app PUBLIC src/app)
target_link_libraries(radio_app PUBLIC radio_ui)

add_executable(radio-player src/app/main.cpp)
target_link_libraries(radio-player PRIVATE radio_app)

# Установка. Пути внутри файлов от префикса не зависят: `.desktop` зовёт
# команду по имени, поэтому годится и /usr, и ~/.local.
include(GNUInstallDirs)
install(TARGETS radio-player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES radio_player/resources/io.github.simba77.RadioPlayer.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# Иконки темы hicolor: без них оболочка покажет приложение безымянным квадратом.
install(DIRECTORY radio_player/resources/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
# AppStream: по нему приложение видно в Discover и GNOME Software.
install(FILES packaging/appstream/io.github.simba77.RadioPlayer.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
# Debian Policy §12.5 и §12.7: без copyright и changelog пакет не примет
# ни один репозиторий. -n: без метки времени, иначе пакет не воспроизводим побайтово.
find_program(GZIP gzip REQUIRED)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/changelog.Debian.gz
    COMMAND sh -c "'${GZIP}' -9n -c '${CMAKE_SOURCE_DIR}/packaging/deb/changelog' > '${CMAKE_BINARY_DIR}/changelog.Debian.gz'"
    DEPENDS ${CMAKE_SOURCE_DIR}/packaging/deb/changelog
    VERBATIM
)
add_custom_target(debian_changelog ALL DEPENDS ${CMAKE_BINARY_DIR}/changelog.Debian.gz)
install(FILES packaging/deb/copyright ${CMAKE_BINARY_DIR}/changelog.Debian.gz DESTINATION ${CMAKE_INSTALL_DOCDIR})

include(packaging/CPackConfig.cmake)

if(RADIO_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED)
    find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
    include(CTest)
    include(Catch)
    add_executable(radio-tests
        tests/dbus_bus.cpp
        tests/http_server.cpp
        tests/main.cpp
        tests/support.cpp
        tests/test_artwork.cpp
        tests/test_autostart.cpp
        tests/test_catalog.cpp
        tests/test_cli.cpp
        tests/test_hls_provider.cpp
        tests/test_i18n.cpp
        tests/test_icy_provider.cpp
        tests/test_libmpv.cpp
        tests/test_models.cpp
        tests/test_mpris.cpp
        tests/test_player.cpp
        tests/test_playlist_io.cpp
        tests/test_power.cpp
        tests/test_settings.cpp
        tests/test_settings_store.cpp
        tests/test_single_instance.cpp
        tests/test_startup.cpp
        tests/test_station_store.cpp
        tests/test_storage.cpp
        tests/test_ui.cpp
        tests/test_version.cpp
        tests/test_wiring.cpp
    )
    target_compile_definitions(radio-tests PRIVATE
        RADIO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        RADIO_LUPDATE="$<TARGET_FILE:Qt6::lupdate>"
        RADIO_APP_BINARY="$<TARGET_FILE:radio-player>"
    )
    target_link_libraries(radio-tests PRIVATE radio_app Catch2::Catch2 Qt6::Test)
    # Тест запуска стартует настоящую программу.
    add_dependencies(radio-tests radio-player)
    # Код 4 — тест пропущен (например, без dbus-daemon): для ctest это не падение.
    catch_discover_tests(radio-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        SKIP_RETURN_CODE 4)
endif()
