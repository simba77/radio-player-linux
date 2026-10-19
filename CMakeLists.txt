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

# Qt 6.4 — то, что есть в Ubuntu 24.04. CI собирает там, поэтому API новее
# использовать нельзя, даже если локально стоит свежая версия.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network)
qt_standard_project_setup()

add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_definitions(QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING)

configure_file(src/core/app_info.hpp.in ${CMAKE_BINARY_DIR}/generated/app_info.hpp @ONLY)

# Слои — отдельные библиотеки, у каждой свой каталог заголовков. Слою видно только
# то, с чем он связан, поэтому запрещённая зависимость ломает сборку, а не ждёт ревью.
add_library(radio_core STATIC
    src/core/json_values.cpp
    src/core/models.cpp
    src/core/paths.cpp
    src/core/playlist_io.cpp
    src/core/settings.cpp
    src/core/storage.cpp
)
target_include_directories(radio_core PUBLIC src/core ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(radio_core PUBLIC Qt6::Core Qt6::Network)

if(RADIO_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED)
    find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
    include(CTest)
    include(Catch)
    add_executable(radio-tests
        tests/main.cpp
        tests/support.cpp
        tests/test_models.cpp
        tests/test_playlist_io.cpp
        tests/test_settings.cpp
        tests/test_storage.cpp
    )
    target_compile_definitions(radio-tests PRIVATE RADIO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(radio-tests PRIVATE radio_core Catch2::Catch2 Qt6::Test Qt6::Widgets)
    catch_discover_tests(radio-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
