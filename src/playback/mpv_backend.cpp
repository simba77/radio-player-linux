#include "mpv_backend.hpp"

#include <QDebug>
#include <QMetaObject>

#include <mpv/client.h>

#include <clocale>
#include <utility>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

MpvBackend::EndReason endReason(mpv_end_file_reason reason)
{
    switch (reason) {
    case MPV_END_FILE_REASON_EOF:
        return MpvBackend::EndReason::Eof;
    case MPV_END_FILE_REASON_STOP:
        return MpvBackend::EndReason::Stop;
    case MPV_END_FILE_REASON_QUIT:
        return MpvBackend::EndReason::Quit;
    case MPV_END_FILE_REASON_ERROR:
        return MpvBackend::EndReason::Error;
    case MPV_END_FILE_REASON_REDIRECT:
        return MpvBackend::EndReason::Redirect;
    }
    return MpvBackend::EndReason::Unknown;
}

// Метаданные приходят словарём строк; другие типы ICY не бывают.
QVariantMap metadataMap(const mpv_node *node)
{
    QVariantMap result;
    if (node == nullptr || node->format != MPV_FORMAT_NODE_MAP)
        return result;
    const mpv_node_list *list = node->u.list;
    for (int index = 0; index < list->num; ++index) {
        const mpv_node &value = list->values[index];
        if (value.format == MPV_FORMAT_STRING)
            result.insert(QString::fromUtf8(list->keys[index]), QString::fromUtf8(value.u.string));
    }
    return result;
}

}  // namespace

LibMpv::LibMpv(QObject *parent) : MpvBackend(parent)
{
    // libmpv падает при не-C числовой локали, а Qt выставляет системную.
    std::setlocale(LC_NUMERIC, "C");

    mpv_ = mpv_create();
    if (mpv_ == nullptr) {
        qWarning().noquote() << u"libmpv не создался — воспроизведение недоступно."_s;
        return;
    }
    mpv_set_option_string(mpv_, "video", "no");
    mpv_set_option_string(mpv_, "ytdl", "no");
    if (mpv_initialize(mpv_) < 0) {
        qWarning().noquote() << u"libmpv не инициализировался — воспроизведение недоступно."_s;
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return;
    }

    mpv_observe_property(mpv_, 0, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "metadata", MPV_FORMAT_NODE);

    // Колбэк зовётся из потока mpv, где трогать ни mpv, ни Qt-объекты нельзя:
    // он лишь ставит разбор очереди событий в очередь потока Qt.
    mpv_set_wakeup_callback(
        mpv_,
        [](void *context) {
            QMetaObject::invokeMethod(static_cast<LibMpv *>(context), &LibMpv::drainEvents,
                                      Qt::QueuedConnection);
        },
        this);
}

LibMpv::~LibMpv()
{
    terminate();
}

QString LibMpv::play(const QString &url)
{
    if (mpv_ == nullptr)
        return u"libmpv недоступен"_s;
    const QByteArray target = url.toUtf8();
    const char *command[] = {"loadfile", target.constData(), "replace", nullptr};
    const int error = mpv_command(mpv_, command);
    return error < 0 ? QString::fromUtf8(mpv_error_string(error)) : QString();
}

void LibMpv::stop()
{
    if (mpv_ == nullptr)
        return;
    const char *command[] = {"stop", nullptr};
    mpv_command(mpv_, command);
}

void LibMpv::setVolume(int volume)
{
    if (mpv_ == nullptr)
        return;
    double value = volume;
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &value);
}

void LibMpv::terminate()
{
    if (mpv_ == nullptr)
        return;
    // Наблюдатели и колбэк снимаются вместе с mpv; колбэк — заранее, чтобы
    // поток mpv не разбудил объект, который уже уходит.
    mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    mpv_terminate_destroy(std::exchange(mpv_, nullptr));
}

void LibMpv::drainEvents()
{
    // Обработчик сигнала может завершить mpv (выход из приложения), поэтому
    // ручка проверяется на каждом шаге, а событие не трогается после emit.
    while (mpv_ != nullptr) {
        const mpv_event *event = mpv_wait_event(mpv_, 0);
        switch (event->event_id) {
        case MPV_EVENT_NONE:
            return;
        case MPV_EVENT_PROPERTY_CHANGE: {
            const auto *property = static_cast<const mpv_event_property *>(event->data);
            const QByteArray name(property->name);
            if (property->format == MPV_FORMAT_FLAG) {
                const bool value = *static_cast<const int *>(property->data) != 0;
                if (name == "core-idle")
                    emit coreIdleChanged(value);
                else if (name == "paused-for-cache")
                    emit pausedForCacheChanged(value);
            } else if (name == "metadata") {
                const auto *node = property->format == MPV_FORMAT_NODE
                                       ? static_cast<const mpv_node *>(property->data)
                                       : nullptr;
                emit metadataChanged(metadataMap(node));
            }
            break;
        }
        case MPV_EVENT_END_FILE:
            emit endFile(endReason(static_cast<const mpv_event_end_file *>(event->data)->reason));
            break;
        default:
            break;
        }
    }
}

}  // namespace radio
