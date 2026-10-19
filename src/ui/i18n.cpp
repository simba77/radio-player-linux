#include "i18n.hpp"

#include "settings.hpp"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

using namespace Qt::StringLiterals;

namespace radio::i18n {

namespace {

// Переводчик принадлежит приложению и живёт, пока живо оно.
bool load(QCoreApplication *app, const QString &name, const QString &directory)
{
    auto *translator = new QTranslator(app);
    if (!translator->load(name, directory)) {
        delete translator;
        return false;
    }
    return QCoreApplication::installTranslator(translator);
}

}  // namespace

QString resolve(const QString &language)
{
    if (LANGUAGES.contains(language))
        return language;
    const QString system = QLocale::system().name().section(u'_', 0, 0);
    return LANGUAGES.contains(system) ? system : u"en"_s;
}

QString prepare(const QString &language)
{
    const QString resolved = resolve(language);
    qputenv("LANGUAGE", resolved.toUtf8());
    return resolved;
}

QString install(QCoreApplication *app, const QString &language)
{
    const QString resolved = resolve(language);
    // Отсюда берётся и формат чисел с датами, а не только язык сообщений.
    QLocale::setDefault(QLocale(resolved));

    if (resolved != "en"_L1) {
        // Свои стандартные кнопки и файловый диалог Qt переводит сам — без этого
        // «Открыть» и «Отмена» остались бы английскими посреди русского интерфейса.
        load(app, u"qtbase_"_s + resolved, QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    }
    // Английский тоже переводится: в коде формы числа записаны как «%n day(s)»,
    // показывать это пользователю нельзя.
    load(app, u"radio_player_"_s + resolved, u":/i18n"_s);
    return resolved;
}

}  // namespace radio::i18n
