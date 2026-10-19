#pragma once

// То, что плеер требует от libmpv.
//
// Интерфейс, а не сам libmpv: логика состояний и реконнекта проверяется на
// двойнике, без звука. Все сигналы приходят в потоке объекта — переброску из
// потока mpv реализация берёт на себя.

#include <QObject>
#include <QString>
#include <QVariantMap>

// Объявление из <mpv/client.h>: сам заголовок нужен только реализации.
struct mpv_handle;

namespace radio {

class MpvBackend : public QObject {
    Q_OBJECT
public:
    // Почему mpv закончил файл (`mpv_end_file_reason`).
    enum class EndReason { Eof, Stop, Quit, Error, Redirect, Unknown };
    Q_ENUM(EndReason)

    using QObject::QObject;

    // Открывает поток. Пустая строка — команда принята, иначе текст ошибки.
    // Принятая команда ещё не звук: о нём сообщит `coreIdleChanged(false)`.
    virtual QString play(const QString &url) = 0;
    virtual void stop() = 0;
    virtual void setVolume(int volume) = 0;
    // Освобождает libmpv. После этого вызовы ничего не делают.
    virtual void terminate() = 0;

signals:
    // `core-idle`: false — звук пошёл.
    void coreIdleChanged(bool idle);
    // `paused-for-cache`: true — поток буферизуется.
    void pausedForCacheChanged(bool paused);
    void endFile(radio::MpvBackend::EndReason reason);
    // `metadata` текущего потока: ICY-теги прямых потоков.
    void metadataChanged(const QVariantMap &metadata);
};

// Настоящий libmpv. Если создать его не удалось, `play()` возвращает ошибку —
// приложение остаётся живым и может сказать об этом пользователю.
class LibMpv : public MpvBackend {
    Q_OBJECT
public:
    explicit LibMpv(QObject *parent = nullptr);
    ~LibMpv() override;

    QString play(const QString &url) override;
    void stop() override;
    void setVolume(int volume) override;
    void terminate() override;

private:
    void drainEvents();

    mpv_handle *mpv_ = nullptr;
};

}  // namespace radio
