#include "icons.hpp"

#include "app_info.hpp"

#include <QApplication>
#include <QHash>
#include <QStyle>

using namespace Qt::StringLiterals;

namespace radio::icons {

namespace {

// Копия иконки приложения в ресурсах программы.
const QString BUNDLED_ICON = u":/icons/io.github.simba77.RadioPlayer.svg"_s;

QStyle::StandardPixmap fallback(const QString &name)
{
    static const QHash<QString, QStyle::StandardPixmap> fallbacks{
        {QLatin1String(PLAY), QStyle::SP_MediaPlay},
        {QLatin1String(STOP), QStyle::SP_MediaStop},
        {QLatin1String(NEXT), QStyle::SP_MediaSkipForward},
        {QLatin1String(PREVIOUS), QStyle::SP_MediaSkipBackward},
        {QLatin1String(SETTINGS), QStyle::SP_FileDialogDetailedView},
        {radio(), QStyle::SP_MediaVolume},
    };
    return fallbacks.value(name, QStyle::SP_CustomBase);
}

}  // namespace

QString radio()
{
    return QLatin1String(APP_RDNS_ID);
}

QIcon get(const QString &name)
{
    const QIcon themed = QIcon::fromTheme(name);
    if (!themed.isNull())
        return themed;
    if (name == radio()) {
        const QIcon bundled(BUNDLED_ICON);
        if (!bundled.isNull())
            return bundled;
    }
    const QStyle::StandardPixmap standard = fallback(name);
    if (standard == QStyle::SP_CustomBase)
        return {};
    // Стандартные пиктограммы живут в стиле QApplication.
    return QApplication::style()->standardIcon(standard);
}

}  // namespace radio::icons
