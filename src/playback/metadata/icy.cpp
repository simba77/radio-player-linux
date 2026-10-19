#include "metadata/icy.hpp"

#include "mpv_backend.hpp"

using namespace Qt::StringLiterals;

namespace radio {

IcyProvider::IcyProvider(MpvBackend *mpv, QObject *parent) : MetadataProvider(parent), mpv_(mpv) {}

void IcyProvider::start(const Station &)
{
    // Источник тот же, что и поток: станция нужна провайдеру HLS, не этому.
    stop();
    subscription_ = connect(mpv_, &MpvBackend::metadataChanged, this, &IcyProvider::handleMetadata);
}

void IcyProvider::stop()
{
    disconnect(subscription_);
    if (!lastRaw_.isEmpty()) {
        lastRaw_.clear();
        emit trackCleared();
    }
}

void IcyProvider::handleMetadata(const QVariantMap &metadata)
{
    QString raw = metadata.value(u"icy-title"_s).toString();
    if (raw.isEmpty())
        raw = metadata.value(u"title"_s).toString();
    if (raw.isEmpty() || raw == lastRaw_)
        return;
    const auto track = splitIcyTitle(raw);
    if (!track)
        return;
    lastRaw_ = raw;
    emit trackChanged(*track);
}

}  // namespace radio
