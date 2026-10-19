#include "now_playing.hpp"

#include "icons.hpp"
#include "player.hpp"

#include <QClipboard>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

// Сколько показывать подтверждение после копирования названия трека.
constexpr int COPIED_HINT_MS = 1500;
// Цвет ошибки. В палитре Qt нет роли для неё, а красный должен читаться в обеих темах.
const QString ERROR_STYLE = u"color: #e05252;"_s;

QPainterPath roundedSquare(int side, qreal ratio)
{
    QPainterPath path;
    path.addRoundedRect(0, 0, side, side, 6 * ratio, 6 * ratio);
    return path;
}

QPixmap roundedPixmap(const QImage &image, qreal ratio)
{
    const int side = int(ARTWORK_SIZE * ratio);
    const QImage scaled = image.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipPath(roundedSquare(side, ratio));
    // Кадрируем по центру: обложки бывают не квадратные.
    painter.drawImage(-(scaled.width() - side) / 2, -(scaled.height() - side) / 2, scaled);
    painter.end();

    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

// Заглушка на случай, когда обложки нет — у части станций её не бывает.
QPixmap placeholderPixmap(qreal ratio)
{
    const int side = int(ARTWORK_SIZE * ratio);
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillPath(roundedSquare(side, ratio), Qt::gray);
    const QIcon icon = icons::get(icons::radio());
    if (!icon.isNull()) {
        const QPixmap overlay = icon.pixmap(side / 2, side / 2);
        painter.drawPixmap((side - overlay.width()) / 2, (side - overlay.height()) / 2, overlay);
    }
    painter.end();

    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

}  // namespace

void ClickableLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QLabel::mousePressEvent(event);
}

NowPlayingWidget::NowPlayingWidget(PlayerManager *player, QWidget *parent)
    : QWidget(parent), player_(player), artwork_(new QLabel), title_(new ClickableLabel),
      subtitle_(new QLabel), error_(new QLabel), volume_(new QSlider(Qt::Horizontal)),
      copiedTimer_(new QTimer(this))
{
    artwork_->setFixedSize(ARTWORK_SIZE, ARTWORK_SIZE);
    artwork_->setAlignment(Qt::AlignCenter);

    title_->setObjectName(u"title"_s);
    title_->setWordWrap(true);
    title_->setTextFormat(Qt::PlainText);
    title_->setStyleSheet(u"font-weight: 600;"_s);
    title_->setCursor(Qt::PointingHandCursor);
    title_->setToolTip(tr("Click to copy the track name"));
    connect(title_, &ClickableLabel::clicked, this, &NowPlayingWidget::copyTrack);

    subtitle_->setObjectName(u"subtitle"_s);
    subtitle_->setTextFormat(Qt::PlainText);
    // Приглушаем прозрачностью, а не фиксированным цветом: так подпись
    // остаётся читаемой и в светлой, и в тёмной теме.
    QPalette palette = subtitle_->palette();
    QColor muted = palette.color(QPalette::WindowText);
    muted.setAlphaF(0.65f);
    palette.setColor(QPalette::WindowText, muted);
    subtitle_->setPalette(palette);

    error_->setObjectName(u"error"_s);
    error_->setWordWrap(true);
    error_->setStyleSheet(ERROR_STYLE);
    error_->hide();

    QPushButton *previous = toolButton(icons::PREVIOUS, tr("Previous station"));
    connect(previous, &QPushButton::clicked, player, &PlayerManager::playPrevious);
    playButton_ = toolButton(icons::PLAY, tr("Play"));
    playButton_->setObjectName(u"playButton"_s);
    connect(playButton_, &QPushButton::clicked, player, &PlayerManager::toggle);
    QPushButton *next = toolButton(icons::NEXT, tr("Next station"));
    connect(next, &QPushButton::clicked, player, &PlayerManager::playNext);

    volume_->setObjectName(u"volume"_s);
    volume_->setRange(0, 100);
    volume_->setValue(player->volume());
    volume_->setFixedWidth(90);
    volume_->setToolTip(tr("Volume"));
    connect(volume_, &QSlider::valueChanged, player, &PlayerManager::setVolume);
    // Громкость меняют и снаружи — медиаклавишами и апплетом через MPRIS.
    connect(player, &PlayerManager::volumeChanged, this, &NowPlayingWidget::onVolumeChanged);

    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->addWidget(previous);
    controls->addWidget(playButton_);
    controls->addWidget(next);
    controls->addStretch();
    controls->addWidget(volume_);

    auto *textColumn = new QVBoxLayout;
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(2);
    textColumn->addWidget(title_);
    textColumn->addWidget(subtitle_);
    textColumn->addWidget(error_);
    textColumn->addStretch();
    textColumn->addLayout(controls);

    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(artwork_, 0, 0, Qt::AlignTop);
    layout->addLayout(textColumn, 0, 1);
    layout->setColumnStretch(1, 1);
    layout->setHorizontalSpacing(12);

    copiedTimer_->setSingleShot(true);
    copiedTimer_->setInterval(COPIED_HINT_MS);
    connect(copiedTimer_, &QTimer::timeout, this, &NowPlayingWidget::refresh);

    connect(player, &PlayerManager::stateChanged, this, &NowPlayingWidget::refresh);
    connect(player, &PlayerManager::trackChanged, this, &NowPlayingWidget::refresh);
    connect(player, &PlayerManager::stationChanged, this, &NowPlayingWidget::refresh);
    connect(player, &PlayerManager::artworkChanged, this, &NowPlayingWidget::updateArtwork);

    updateArtwork();
    refresh();
}

// MARK: - Обновление

void NowPlayingWidget::refresh()
{
    title_->setText(titleText());
    title_->setEnabled(player_->currentTrack().has_value());

    const QString subtitle = subtitleText();
    subtitle_->setText(subtitle);
    subtitle_->setVisible(!subtitle.isEmpty());

    error_->setText(player_->errorMessage());
    error_->setVisible(!player_->errorMessage().isEmpty());

    const bool stopState = player_->isActive();
    playButton_->setIcon(icons::get(QLatin1String(stopState ? icons::STOP : icons::PLAY)));
    playButton_->setToolTip(stopState ? tr("Stop") : tr("Play"));
}

QString NowPlayingWidget::titleText() const
{
    if (player_->isReconnecting())
        return tr("Reconnecting...");
    if (player_->isLoading())
        return tr("Loading...");
    if (const auto track = player_->currentTrack())
        return track->display();
    if (const auto station = player_->currentStation())
        return station->name;
    return tr("Not playing");
}

QString NowPlayingWidget::subtitleText() const
{
    const auto station = player_->currentStation();
    if (!station)
        return {};
    if (player_->currentTrack() || player_->isLoading() || player_->isReconnecting())
        return station->name;
    return {};
}

void NowPlayingWidget::updateArtwork()
{
    const QImage image = player_->artworkImage();
    const qreal ratio = artwork_->devicePixelRatioF();
    artwork_->setPixmap(image.isNull() ? placeholderPixmap(ratio) : roundedPixmap(image, ratio));
}

void NowPlayingWidget::onVolumeChanged(int value)
{
    if (volume_->value() == value)
        return;
    // Без блокировки слайдер отправил бы значение обратно плееру.
    const QSignalBlocker blocker(volume_);
    volume_->setValue(value);
}

void NowPlayingWidget::copyTrack()
{
    const auto track = player_->currentTrack();
    if (!track)
        return;
    QGuiApplication::clipboard()->setText(track->display());
    subtitle_->setText(tr("Copied"));
    subtitle_->show();
    copiedTimer_->start();
}

QPushButton *NowPlayingWidget::toolButton(const char *icon, const QString &tooltip)
{
    auto *button = new QPushButton;
    button->setIcon(icons::get(QLatin1String(icon)));
    button->setToolTip(tooltip);
    button->setFlat(true);
    button->setFixedSize(32, 28);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return button;
}

}  // namespace radio
