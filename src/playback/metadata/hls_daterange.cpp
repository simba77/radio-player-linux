#include "metadata/hls_daterange.hpp"

#include "json_values.hpp"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <utility>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

// Размер обложки. 300×300 хватает и для окна, и для MPRIS.
const QStringList COVER_KEYS{u"coverImageUrl300"_s, u"coverImageUrl600"_s, u"coverImageUrl100"_s};

const QRegularExpression DATERANGE(u"^#EXT-X-DATERANGE:(.+)$"_s, QRegularExpression::MultilineOption);
const QRegularExpression ATTRIBUTE(u"([A-Za-z0-9-]+)=\"([^\"]*)\""_s);
const QRegularExpression STREAM_INF(u"^#EXT-X-STREAM-INF:.*$\\r?\\n^(?!#)(\\S+)\\s*$"_s,
                                    QRegularExpression::MultilineOption);

// Таймаут одного запроса к серверу потока.
constexpr int TRANSFER_TIMEOUT_MS = 10'000;

QString resolve(const QString &base, const QString &relative)
{
    return QUrl(base).resolved(QUrl(relative)).toString();
}

}  // namespace

namespace hls {

std::optional<QHash<QString, QString>> parseDaterange(const QString &playlist)
{
    auto ranges = DATERANGE.globalMatch(playlist);
    while (ranges.hasNext()) {
        QHash<QString, QString> attributes;
        auto pairs = ATTRIBUTE.globalMatch(ranges.next().captured(1));
        while (pairs.hasNext()) {
            const QRegularExpressionMatch pair = pairs.next();
            attributes.insert(pair.captured(1), pair.captured(2));
        }
        if (attributes.contains(u"X-TITLE"_s) || attributes.contains(u"X-ARTIST"_s))
            return attributes;
    }
    return std::nullopt;
}

QString variantUri(const QString &master)
{
    return STREAM_INF.match(master).captured(1);
}

QString extractCover(const QByteArray &body)
{
    const QJsonObject current = QJsonDocument::fromJson(body).object().value("current"_L1).toObject();
    for (const QString &key : COVER_KEYS) {
        const QString url = json::text(current.value(key));
        if (!url.isEmpty())
            return url;
    }
    return {};
}

}  // namespace hls

HlsDaterangeProvider::HlsDaterangeProvider(QObject *parent, int pollIntervalMs)
    : MetadataProvider(parent), network_(new QNetworkAccessManager(this)), timer_(new QTimer(this))
{
    timer_->setObjectName(u"pollTimer"_s);
    timer_->setInterval(pollIntervalMs);
    connect(timer_, &QTimer::timeout, this, &HlsDaterangeProvider::pollVariant);
}

// MARK: - Жизненный цикл

void HlsDaterangeProvider::start(const Station &station)
{
    stop();
    masterUrl_ = station.url;
    fetchMaster();
}

void HlsDaterangeProvider::stop()
{
    timer_->stop();
    // Отсоединяемся до abort(): он сообщает о завершении синхронно, а ответ
    // остановленного провайдера уже никому не нужен.
    for (QNetworkReply *reply : std::exchange(replies_, {})) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }

    masterUrl_.clear();
    variantUrl_.clear();
    failures_ = 0;
    emptyPolls_ = 0;
    if (!lastRangeId_.isEmpty()) {
        lastRangeId_.clear();
        emit trackCleared();
    }
}

// MARK: - Запросы

void HlsDaterangeProvider::get(const QString &url, Handler handler)
{
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(TRANSFER_TIMEOUT_MS);
    QNetworkReply *reply = network_->get(request);
    replies_.insert(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, handler = std::move(handler)] {
        replies_.remove(reply);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            handler(std::nullopt);
            return;
        }
        handler(QString::fromUtf8(reply->readAll()));
    });
}

void HlsDaterangeProvider::fetchMaster()
{
    if (masterUrl_.isEmpty())
        return;
    get(masterUrl_, [this](const std::optional<QString> &body) { handleMaster(body); });
}

void HlsDaterangeProvider::handleMaster(const std::optional<QString> &body)
{
    if (masterUrl_.isEmpty())
        return;
    if (!body) {
        // Сеть или сервер подвели — не мешаем воспроизведению, пробуем позже.
        timer_->start();
        return;
    }

    // Ссылка на variant обязательно с параметром hlssid: без него DATERANGE не отдаётся.
    // Если STREAM-INF нет, значит нам сразу дали media playlist.
    const QString variant = hls::variantUri(*body);
    variantUrl_ = variant.isEmpty() ? masterUrl_ : resolve(masterUrl_, variant);
    failures_ = 0;
    emptyPolls_ = 0;
    pollVariant();
    timer_->start();
}

void HlsDaterangeProvider::pollVariant()
{
    if (variantUrl_.isEmpty())
        return;
    get(variantUrl_, [this](const std::optional<QString> &body) { handleVariant(body); });
}

void HlsDaterangeProvider::handleVariant(const std::optional<QString> &body)
{
    if (variantUrl_.isEmpty())
        return;
    if (!body) {
        if (++failures_ >= HLS_FAILURES_BEFORE_REFRESH) {
            failures_ = 0;
            timer_->stop();
            fetchMaster();
        }
        return;
    }

    failures_ = 0;
    const auto attributes = hls::parseDaterange(*body);
    if (!attributes) {
        // Станция не из hostingradio либо метаданных сейчас нет — это нормально.
        if (++emptyPolls_ >= HLS_EMPTY_POLLS_BEFORE_GIVING_UP) {
            timer_->stop();
            qDebug().noquote() << u"Метаданных в манифесте нет — прекращаем опрос"_s << variantUrl_;
        }
        return;
    }
    emptyPolls_ = 0;

    const QString rangeId = attributes->value(u"ID"_s);
    if (rangeId.isEmpty() || rangeId == lastRangeId_)
        return;
    lastRangeId_ = rangeId;

    const TrackInfo track{
        attributes->value(u"X-TITLE"_s).trimmed(), attributes->value(u"X-ARTIST"_s).trimmed(), {}};
    if (track.title.isEmpty())
        return;
    // Название отдаём сразу, обложку — когда (и если) догрузится.
    emit trackChanged(track);

    const QString metadataUri = attributes->value(u"X-METADATA-URI"_s);
    if (!metadataUri.isEmpty())
        fetchArtwork(resolve(variantUrl_, metadataUri), rangeId, track);
}

void HlsDaterangeProvider::fetchArtwork(const QString &url, const QString &rangeId, const TrackInfo &track)
{
    get(url, [this, rangeId, track](const std::optional<QString> &body) {
        if (!body || rangeId != lastRangeId_)
            return;  // Трек уже сменился — обложка неактуальна.
        const QString cover = hls::extractCover(body->toUtf8());
        if (!cover.isEmpty())
            emit trackChanged({track.title, track.artist, cover});
    });
}

}  // namespace radio
