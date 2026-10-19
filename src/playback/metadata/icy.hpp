#pragma once

// ICY-метаданные прямых Icecast/Shoutcast-потоков.
//
// libmpv отдаёт их в свойстве `metadata` ключом `icy-title`:
//
//     {'icy-title': 'TIESTO/BIA/21 SAVAGE - Both (Record Mix)', 'icy-url': ..., 'icy-pub': '1'}

#include "metadata/provider.hpp"

#include <QVariantMap>

namespace radio {

class IcyProvider : public MetadataProvider {
    Q_OBJECT
public:
    IcyProvider(MpvBackend *mpv, QObject *parent = nullptr);

    void start(const Station &station) override;
    void stop() override;

private:
    void handleMetadata(const QVariantMap &metadata);

    MpvBackend *mpv_;
    QMetaObject::Connection subscription_;
    QString lastRaw_;
};

}  // namespace radio
