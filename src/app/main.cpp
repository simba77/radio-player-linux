// Точка входа: собирает подсистемы, связывает их и владеет их временем жизни.
// Больше нигде две подсистемы друг с другом не связываются.

#include "app_info.hpp"
#include "catalog.hpp"
#include "cli.hpp"
#include "i18n.hpp"
#include "main_window.hpp"
#include "mpris.hpp"
#include "mpv_backend.hpp"
#include "network.hpp"
#include "paths.hpp"
#include "player.hpp"
#include "power.hpp"
#include "settings_store.hpp"
#include "single_instance.hpp"
#include "station_store.hpp"
#include "tray.hpp"
#include "unix_signals.hpp"
#include "wiring.hpp"

#include <QApplication>
#include <QDebug>
#include <QSystemTrayIcon>

#include <cstdio>
#include <memory>

using namespace Qt::StringLiterals;
using namespace radio;

int main(int argc, char *argv[])
{
    QStringList arguments;
    for (int index = 0; index < argc; ++index)
        arguments.append(QString::fromLocal8Bit(argv[index]));
    const cli::Options options = cli::parse(arguments);
    switch (options.action) {
    case cli::Options::Action::Version:
        std::printf("%s\n", cli::versionText().toLocal8Bit().constData());
        return 0;
    case cli::Options::Action::Help:
        std::printf("%s", cli::usage().toLocal8Bit().constData());
        return 0;
    case cli::Options::Action::Error:
        std::fprintf(stderr, "%s\n\n%s", options.error.toLocal8Bit().constData(),
                     cli::usage().toLocal8Bit().constData());
        return 2;
    case cli::Options::Action::Run:
        break;
    }
    cli::installLogging(options.logLevel);

    if (instance::notifyRunningInstance()) {
        qInfo().noquote() << u"%1 уже запущен — показали окно существующего экземпляра."_s.arg(
            QLatin1String(APP_NAME));
        return 0;
    }

    paths::ensureDirs();

    SettingsStore settings;
    // Язык выбирается до QApplication: свой переводчик Qt ставит при создании
    // приложения, и позже его уже не сменить.
    i18n::prepare(settings.language());

    QApplication app(argc, argv);
    QApplication::setApplicationName(QLatin1String(APP_NAME));
    QApplication::setApplicationVersion(QLatin1String(APP_VERSION));
    // Имя `.desktop`-файла: по нему Wayland связывает окно с иконкой приложения.
    QGuiApplication::setDesktopFileName(QLatin1String(APP_RDNS_ID));
    // Окно прячется в трей — Qt не должен завершать приложение при закрытии последнего окна.
    QApplication::setQuitOnLastWindowClosed(false);
    // Переводчики ставятся до первого виджета: строки берутся при создании.
    i18n::install(&app, settings.language());

    // Сразу после приложения: сеанс может завершаться, пока мы стартуем. Сигнал,
    // пришедший до цикла событий, подождёт в сокете и закроет нас штатно.
    UnixSignals unixSignals;
    QObject::connect(&unixSignals, &UnixSignals::terminationRequested, &app, &QApplication::quit);

    // Обновление создаётся первым: оно владеет списком источников, а стартовый
    // набор каталогов читается из кэша ровно по этому списку.
    CatalogUpdater updater(settings.extraCatalogUrls());
    StationStore store(catalog::loadInitial(updater.urls()));
    PlayerManager player(&store, new LibMpv);
    installNetworkMonitor(&player);

    QObject::connect(&updater, &CatalogUpdater::finished, &store,
                     [&store](const QList<Catalog> &catalogs, bool changed) {
                         if (changed)
                             store.setCatalogs(catalogs);
                     });
    wireCatalogSources(&settings, &updater, &store);

    MainWindow window(&player, &store, &settings, &updater);

    std::unique_ptr<TrayIcon> tray;
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray = std::make_unique<TrayIcon>(&player);
        QObject::connect(tray.get(), &TrayIcon::toggleWindowRequested, &window,
                         &MainWindow::toggleVisibility);
        QObject::connect(tray.get(), &TrayIcon::settingsRequested, &window, &MainWindow::openSettings);
        QObject::connect(tray.get(), &TrayIcon::quitRequested, &app, &QApplication::quit);
        tray->show();
    } else {
        // Без лотка (например, GNOME без AppIndicator) прятать окно некуда.
        qWarning().noquote() << u"Системный лоток недоступен — работаем только окном."_s;
        QApplication::setQuitOnLastWindowClosed(true);
        window.allowClose();
    }

    InstanceServer server;
    QObject::connect(&server, &InstanceServer::showRequested, &window,
                     [&window](const QString &token) { window.showAndRaise(token); });
    QObject::connect(&app, &QApplication::aboutToQuit, &server, &InstanceServer::close);
    QObject::connect(&app, &QApplication::aboutToQuit, &player, &PlayerManager::shutdown);

    SleepInhibitor inhibitor(&player);
    QObject::connect(&app, &QApplication::aboutToQuit, &inhibitor, &SleepInhibitor::release);

    MprisServer mpris(&player);
    QObject::connect(&mpris, &MprisServer::raiseRequested, &window, [&window] { window.showAndRaise(); });
    QObject::connect(&mpris, &MprisServer::quitRequested, &app, &QApplication::quit);
    if (mpris.registerService()) {
        QObject::connect(&app, &QApplication::aboutToQuit, &mpris, &MprisServer::unregisterService);
    } else {
        // Без MPRIS остаются окно и трей — медиаклавиши просто не работают.
        qWarning().noquote() << u"MPRIS недоступен: медиаклавиши и виджет плеера работать не будут."_s;
    }

    scheduleCatalogUpdate(&updater, settings);
    QObject::connect(&app, &QApplication::aboutToQuit, &updater, &CatalogUpdater::cancel);

    window.show();

    return QApplication::exec();
}
