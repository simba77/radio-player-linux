#pragma once

// Загрузка каталогов станций: встроенная копия → кэш на диске → сеть.
//
// Приложение обязано работать сразу и без сети, поэтому встроенная копия каталога
// всегда есть в программе, а сетевое обновление лишь уточняет её. Ошибка загрузки
// никогда не приводит к пустому списку станций.

#include "models.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace radio {

namespace catalog {

// Откуда обновлять встроенные каталоги. Порядок важен: при коллизии id станций
// побеждает первый. Функция, а не глобальная константа: её читают и при
// инициализации других глобальных объектов, порядок которой не определён.
QStringList defaultCatalogUrls();

// Каталоги, встроенные в программу. Если и они битые — это баг сборки, но не падение.
QList<Catalog> loadBundled();

// Ранее скачанные каталоги — только для перечисленных источников.
//
// Читаем строго по списку, а не всё, что лежит в кэше: иначе каталог,
// удалённый пользователем из настроек, продолжал бы поставлять станции
// из своего файла, и убрать их было бы нечем. Порядок — как в `urls`,
// от него зависит, кто побеждает при совпадении id станций.
QList<Catalog> loadCached(const QStringList &urls);

// Каталоги для старта: кэш, если он есть, иначе встроенные копии.
//
// Кэш заведомо не старше встроенной копии — он появляется только после
// успешной загрузки из сети. Но встроенная копия могла обогнать кэш
// после обновления приложения: побеждает больший `revision`.
QList<Catalog> loadInitial(const QStringList &urls = defaultCatalogUrls());

// Сколько секунд прошло с последнего обновления; пусто, если не обновляли.
std::optional<double> secondsSinceUpdate();

// Удаляет из кэша файлы источников, которых больше нет в списке.
//
// Кэш — производная от списка каталогов, а не самостоятельное хранилище:
// осиротевший файл иначе лежал бы вечно, занимая место и сбивая с толку.
void pruneCache(const QStringList &urls);

// Файл кэша именуется по адресу источника, а не по id каталога: имя должно
// однозначно отвечать, из какого источника взят файл.
QString cachePath(const QString &url);
QString etagsPath();
// ETag последней загрузки по адресу источника.
QHash<QString, QString> etags();

}  // namespace catalog

// Фоновое обновление каталогов по HTTP с валидацией ETag.
//
// Список источников — встроенные каталоги плюс добавленные пользователем.
// Дополнительные меняются через `setExtraUrls`: список правится в настройках
// уже после запуска, а копия, снятая при создании, об этих правках не узнала бы.
class CatalogUpdater : public QObject {
    Q_OBJECT
public:
    explicit CatalogUpdater(const QStringList &extraUrls = {},
                            const QStringList &builtinUrls = catalog::defaultCatalogUrls(),
                            QObject *parent = nullptr);

    QStringList builtinUrls() const { return builtinUrls_; }
    // Все источники: встроенные, затем пользовательские.
    QStringList urls() const { return builtinUrls_ + extraUrls_; }

    // Заменяет список пользовательских каталогов.
    //
    // Уже отправленные запросы не отменяет: текущий цикл дойдёт до конца
    // со старым списком, а новый начнётся с обновлённым.
    void setExtraUrls(const QStringList &urls);

    bool isRunning() const { return pending_ > 0; }
    void updateNow();
    void cancel();

signals:
    // Загрузка завершилась: каталоги для списка станций и признак, что
    // хоть один источник принёс новую версию.
    void finished(const QList<radio::Catalog> &catalogs, bool changed);

private:
    void fetch(const QString &url);
    void handleReply(const QString &url, QNetworkReply *reply);
    void store(const QString &url, QNetworkReply *reply);
    void finish();

    QStringList builtinUrls_;
    QStringList extraUrls_;
    QNetworkAccessManager *network_;
    QSet<QNetworkReply *> replies_;
    int pending_ = 0;
    bool changed_ = false;
};

}  // namespace radio
