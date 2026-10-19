#pragma once

// Загрузка и кэширование обложек.
//
// Файловый кэш нужен не только ради экономии запросов: MPRIS отдаёт обложку
// через `mpris:artUrl`, то есть путём к локальному файлу.

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace radio {

// Потолок кэша на диске. Радио меняет трек раз в несколько минут, то есть
// сотни обложек в сутки непрерывного эфира: без потолка каталог рос бы вечно.
inline constexpr qint64 MAX_ARTWORK_CACHE_BYTES = 64 * 1024 * 1024;

// Удаляет самые старые обложки, пока кэш не уложится в лимит.
//
// Возвращает число удалённых файлов. Кэш расходный: потерянная обложка стоит
// одного запроса к сети, поэтому любые ошибки доступа здесь просто молчат.
int pruneArtworkCache(const QString &directory, qint64 maxBytes);

// Отдаёт обложку по URL: сначала из кэша на диске, иначе из сети.
class ArtworkLoader : public QObject {
    Q_OBJECT
public:
    explicit ArtworkLoader(QObject *parent = nullptr);

    void load(const QString &url);
    void cancelAll();

    // Файл кэша для адреса. Имя — хеш адреса; расширение нужно только тем,
    // кто открывает файл по пути (клиенты MPRIS).
    static QString cachePath(const QString &url);

signals:
    // Путь пуст, если записать кэш не удалось: картинка всё равно годится.
    void artworkReady(const QString &url, const QString &path, const QImage &image);

private:
    void handleReply(const QString &url, QNetworkReply *reply);

    QNetworkAccessManager *network_;
    QHash<QString, QNetworkReply *> pending_;
};

}  // namespace radio
