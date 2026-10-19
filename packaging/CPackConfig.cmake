# Пакеты для выпусков: .deb для Debian/Ubuntu и .tar.gz с тем же содержимым
# для остальных дистрибутивов.
set(CPACK_PACKAGE_NAME "radio-player")
set(CPACK_PACKAGE_VENDOR "simba77")
set(CPACK_PACKAGE_CONTACT "simba77 <itenterprise105@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Internet radio player")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "A minimal internet radio player: a system tray icon plus a regular window.
It plays HLS and direct HTTP(S) streams, shows the current track with cover
art, survives network drops on its own and integrates with the desktop
through MPRIS2.")
# Библиотеки, с которыми программа связана, перечислит dpkg-shlibdeps. Плагины
# Qt она не линкует, поэтому их приходится называть самим: без движка SVG
# пропадают иконки тем, без переводов Qt — русские кнопки стандартных диалогов,
# без плагина Wayland окно идёт через XWayland.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6svg6")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland, qt6-translations-l10n")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

include(CPack)
