#pragma once

// MPRIS2 — управление плеером извне: медиаклавиши, виджет «Медиаплеер» в Plasma.
// Спецификация: https://specifications.freedesktop.org/mpris-spec/latest/
//
// Живое радио не перематывается и не имеет длительности, поэтому `CanSeek` = false,
// `Position` всегда 0, а `mpris:length` не публикуется вовсе.

#include "models.hpp"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace radio {

class PlayerManager;

// Спецификация просит, чтобы имя на шине совпадало с именем `.desktop`-файла.
QString mprisBusName();
inline constexpr char MPRIS_OBJECT_PATH[] = "/org/mpris/MediaPlayer2";

// Метаданные текущего трека в формате MPRIS. Без трека заголовком служит
// название станции; `xesam:artist` — всегда список, даже пустой.
QVariantMap mprisMetadata(const std::optional<Station> &station, const std::optional<TrackInfo> &track,
                          const QString &artworkPath);

// Интерфейс `org.mpris.MediaPlayer2`: сведения о приложении.
//
// Имена методов и свойств заданы спецификацией MPRIS и потому идут в CamelCase:
// по ним приложение находят на шине, переименовать их нельзя.
class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)
public:
    explicit MprisRootAdaptor(QObject *parent);

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const;
    // Имя `.desktop`-файла без расширения: по нему оболочка находит иконку
    // и название приложения для апплета «Медиаплеер».
    QString desktopEntry() const;
    QStringList supportedUriSchemes() const;
    QStringList supportedMimeTypes() const;

public slots:
    void Raise();
    void Quit();

signals:
    void raiseRequested();
    void quitRequested();
};

// Интерфейс `org.mpris.MediaPlayer2.Player`: воспроизведение и метаданные.
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoNext)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
public:
    MprisPlayerAdaptor(QObject *parent, PlayerManager *player);

    // Буферизация и переподключение — тоже «Playing»: для клиента важно
    // намерение, иначе апплет мигал бы «Stopped» на каждой заминке потока.
    QString playbackStatus() const;
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double value);
    qlonglong position() const { return 0; }
    double rate() const { return 1.0; }
    bool canGoNext() const;
    bool canPlay() const;
    bool canPause() const { return true; }
    bool canSeek() const { return false; }
    bool canControl() const { return true; }

public slots:
    void Play();
    // Живой поток не ставится на паузу — останавливаем.
    void Pause();
    void PlayPause();
    void Stop();
    void Next();
    void Previous();
    // Живое радио не перематывается и не открывает чужие адреса.
    void Seek(qlonglong) {}
    void SetPosition(const QDBusObjectPath &, qlonglong) {}
    void OpenUri(const QString &) {}

private:
    PlayerManager *player_;
};

// Регистрирует плеер на шине и рассылает изменения свойств.
class MprisServer : public QObject {
    Q_OBJECT
public:
    // Шина — параметр: тесты поднимают свою, чтобы не трогать сеанс пользователя.
    MprisServer(PlayerManager *player, const QDBusConnection &connection = QDBusConnection::sessionBus(),
                QObject *parent = nullptr);

    // Занимает имя на шине. False — если шины нет или имя уже занято.
    bool registerService();
    void unregisterService();

signals:
    // Клиент просит показать окно или выйти.
    void raiseRequested();
    void quitRequested();

private:
    void emitPlaybackStatus();
    void emitMetadata();
    void emitVolume();
    void emitProperties(const QVariantMap &changed);

    PlayerManager *player_;
    QDBusConnection connection_;
    MprisPlayerAdaptor *playerAdaptor_;
    bool registered_ = false;
};

}  // namespace radio
