// Точка входа тестов: приложение Qt на всё время прогона.
//
// Таймеры, сигналы и сетевые запросы без экземпляра приложения не живут, а он
// в процессе может быть только один. Создаётся `QApplication`, а не
// `QCoreApplication`: тестам интерфейса нужны виджеты. Платформа — offscreen,
// поэтому дисплей не требуется ни в CI, ни в контейнере.

#include <QApplication>

#include <catch2/catch_session.hpp>

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // Аргументы командной строки принадлежат Catch2 — Qt отдаём только имя программы.
    int qtArgc = 1;
    QApplication app(qtArgc, argv);
    return Catch::Session().run(argc, argv);
}
