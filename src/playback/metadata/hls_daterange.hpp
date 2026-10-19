#pragma once

// Метаданные HLS-потоков hostingradio (Radio Record) из тега `#EXT-X-DATERANGE`.
//
// ffmpeg этот тег пропускает, поэтому libmpv для таких потоков не отдаёт ничего —
// манифест приходится опрашивать самим. Подробности в `docs/architecture.md`.
//
//     #EXT-X-DATERANGE:ID="6a7a2f97…",CLASS="ru.hostingradio.meta",START-DATE="…",END-ON-NEXT=YES,
//                      X-ARTIST="BLOND_ISH",X-TITLE="$MONEY",X-METADATA-URI="metadata/6a7a2f97….json"

#include "metadata/provider.hpp"

#include <QHash>
#include <QSet>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace radio {

// Как часто опрашивать манифест. Сегменты по ~6 с, чаще смысла нет.
inline constexpr int HLS_POLL_INTERVAL_MS = 5000;
// После стольких подряд неудачных опросов перезапрашиваем мастер-плейлист:
// параметр hlssid в ссылке на variant протухает.
inline constexpr int HLS_FAILURES_BEFORE_REFRESH = 3;
// Сколько манифестов подряд можно прочитать без единого DATERANGE, прежде чем
// признать, что метаданных у станции нет. Провайдер выбирается по расширению
// `.m3u8`, а тег отдаёт только hostingradio: у остальных HLS-станций опрос
// каждые пять секунд не найдёт ничего никогда. При ~5 с это минута ожидания.
inline constexpr int HLS_EMPTY_POLLS_BEFORE_GIVING_UP = 12;

namespace hls {

// Атрибуты первого DATERANGE с метаданными трека; пусто, если такого нет.
std::optional<QHash<QString, QString>> parseDaterange(const QString &playlist);
// Ссылка на первый variant мастер-плейлиста; пусто, если это уже media playlist.
QString variantUri(const QString &master);
// URL обложки текущего трека из JSON по `X-METADATA-URI`. У части станций
// её нет — это штатно, и тогда возвращается пустая строка.
QString extractCover(const QByteArray &body);

}  // namespace hls

class HlsDaterangeProvider : public MetadataProvider {
    Q_OBJECT
public:
    // Интервал опроса меняют только тесты: ждать минуту ради двенадцати
    // пустых манифестов им незачем.
    explicit HlsDaterangeProvider(QObject *parent = nullptr, int pollIntervalMs = HLS_POLL_INTERVAL_MS);

    void start(const Station &station) override;
    void stop() override;

private:
    using Handler = std::function<void(const std::optional<QString> &body)>;

    void get(const QString &url, Handler handler);
    void fetchMaster();
    void handleMaster(const std::optional<QString> &body);
    void pollVariant();
    void handleVariant(const std::optional<QString> &body);
    void fetchArtwork(const QString &url, const QString &rangeId, const TrackInfo &track);

    QNetworkAccessManager *network_;
    QTimer *timer_;
    QSet<QNetworkReply *> replies_;

    QString masterUrl_;
    QString variantUrl_;
    QString lastRangeId_;
    int failures_ = 0;
    int emptyPolls_ = 0;
};

}  // namespace radio
