// Переводы.
//
// Главный тест здесь — про рассинхрон: `lupdate` запускается руками, поэтому
// новая строка в коде легко останется без перевода, и обнаружится это только
// глазами на чужом языке.

#include "i18n.hpp"
#include "settings.hpp"
#include "support.hpp"

#include <QFile>
#include <QProcess>
#include <QSet>
#include <QTranslator>
#include <QXmlStreamReader>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

QString catalogPath(const QString &language)
{
    return test::sourcePath(u"translations/radio_player_%1.ts"_s.arg(language));
}

struct Message {
    QString context;
    QString source;
    bool unfinished = false;
};

QList<Message> readCatalog(const QString &path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    // Разбор из готовых данных: читая из устройства, Qt 6.4 считает документ
    // оборванным после закрывающего тега — данные ведь могли ещё прийти.
    QXmlStreamReader xml(file.readAll());
    QList<Message> messages;
    QString context;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        if (xml.name() == "name"_L1) {
            context = xml.readElementText();
        } else if (xml.name() == "message"_L1) {
            messages.append({context, {}, false});
        } else if (xml.name() == "source"_L1) {
            messages.last().source = xml.readElementText();
        } else if (xml.name() == "translation"_L1) {
            messages.last().unfinished = xml.attributes().value("type"_L1) == "unfinished"_L1;
        }
    }
    INFO(xml.errorString().toStdString() << " at line " << xml.lineNumber());
    REQUIRE_FALSE(xml.hasError());
    return messages;
}

QSet<QString> keys(const QList<Message> &messages)
{
    QSet<QString> out;
    for (const Message &message : messages)
        out.insert(message.context + u" | "_s + message.source);
    return out;
}

}  // namespace

// MARK: - Выбор языка

TEST_CASE("an explicit language wins")
{
    const QString language = GENERATE(as<QString>{}, u"en"_s, u"ru"_s);
    CHECK(i18n::resolve(language) == language);
}

TEST_CASE("an unknown language falls back to a known one")
{
    // Мусор в settings.json не должен оставлять интерфейс пустым.
    CHECK(LANGUAGES.contains(i18n::resolve(u"klingon"_s)));
}

TEST_CASE("prepare announces the language to Qt")
{
    // Qt выбирает свои переводы по LANGUAGE ещё до создания приложения.
    const QByteArray old = qgetenv("LANGUAGE");
    i18n::prepare(u"ru"_s);
    CHECK(qgetenv("LANGUAGE") == "ru");
    qputenv("LANGUAGE", old);
}

// MARK: - Файлы перевода

TEST_CASE("the compiled translations are built in")
{
    const QString language = GENERATE(as<QString>{}, u"en"_s, u"ru"_s);
    CHECK(QFile::exists(u":/i18n/radio_player_%1.qm"_s.arg(language)));
}

TEST_CASE("the Russian translation loads and translates")
{
    QTranslator translator;
    REQUIRE(translator.load(u"radio_player_ru"_s, u":/i18n"_s));
    CHECK(translator.translate("radio::TrayIcon", "Quit") == u"Выход"_s);
    CHECK(translator.translate("radio::PlayerManager", "Could not open the stream: %1") ==
          u"Не удалось открыть поток: %1"_s);
}

TEST_CASE("the English translation has the plural forms")
{
    // Исходная строка «%n minute(s)» годится для извлечения, но не для показа.
    QTranslator translator;
    REQUIRE(translator.load(u"radio_player_en"_s, u":/i18n"_s));
    CHECK(translator.translate("radio::CatalogsTab", "%n minute(s)", nullptr, 1) == "%n minute"_L1);
    CHECK(translator.translate("radio::CatalogsTab", "%n minute(s)", nullptr, 5) == "%n minutes"_L1);
}

TEST_CASE("there are no unfinished translations")
{
    // Незаконченный перевод молча уходит в .qm исходной строкой.
    const QString language = GENERATE(as<QString>{}, u"en"_s, u"ru"_s);
    QStringList unfinished;
    for (const Message &message : readCatalog(catalogPath(language))) {
        if (message.unfinished)
            unfinished.append(message.context + u" | "_s + message.source);
    }
    CHECK(unfinished.isEmpty());
}

TEST_CASE("the translation catalog matches the code")
{
    // Каждая строка из tr() должна быть в .ts, и ничего лишнего, — иначе забыли
    // `make i18n-update`. Сверяемся с тем, что извлёк бы сам lupdate.
    const QString fresh = test::tempDir() + u"/radio_player_ru.ts"_s;
    QProcess lupdate;
    lupdate.start(QStringLiteral(RADIO_LUPDATE),
                  {u"-silent"_s, u"-no-obsolete"_s, test::sourcePath(u"src"_s), u"-ts"_s, fresh});
    REQUIRE(lupdate.waitForFinished(60'000));
    REQUIRE(lupdate.exitCode() == 0);

    const QSet<QString> code = keys(readCatalog(fresh));
    const QSet<QString> committed = keys(readCatalog(catalogPath(u"ru"_s)));
    CHECK((code - committed).values() == QStringList());
    CHECK((committed - code).values() == QStringList());
}
