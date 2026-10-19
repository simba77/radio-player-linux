#pragma once

// Провайдеры метаданных текущего трека.
//
// Почему их два — см. `docs/architecture.md`. Коротко: libmpv отдаёт ICY для прямых
// потоков, но для HLS-потоков hostingradio не отдаёт ничего, и там метаданные
// приходится забирать самим из тега `#EXT-X-DATERANGE` в манифесте.

#include "models.hpp"

#include <QObject>

namespace radio {

class MpvBackend;

// Источник информации о текущем треке.
//
// Провайдер обязан молчать, если метаданных нет: отсутствие трека — штатная
// ситуация, а не ошибка, и оно не должно влиять на воспроизведение.
class MetadataProvider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void start(const Station &station) = 0;
    virtual void stop() = 0;

signals:
    void trackChanged(const radio::TrackInfo &track);
    // Метаданных больше нет (сменилась станция, поток остановлен).
    void trackCleared();
};

// Выбирает провайдера по типу потока. Провайдер принадлежит `parent`.
MetadataProvider *makeProvider(const Station &station, MpvBackend *mpv, QObject *parent);

}  // namespace radio
