#pragma once

// Виджет «Сейчас играет»: обложка, название трека, управление, громкость.

#include <QLabel>
#include <QWidget>

class QPushButton;
class QSlider;
class QTimer;

namespace radio {

class PlayerManager;

// Размер обложки в окне.
inline constexpr int ARTWORK_SIZE = 72;

// QLabel, реагирующий на клик. Нужен для копирования названия трека.
class ClickableLabel : public QLabel {
    Q_OBJECT
public:
    using QLabel::QLabel;

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
};

class NowPlayingWidget : public QWidget {
    Q_OBJECT
public:
    explicit NowPlayingWidget(PlayerManager *player, QWidget *parent = nullptr);

    void refresh();

private:
    QString titleText() const;
    // Станция показывается второй строкой, только когда первую занял трек.
    QString subtitleText() const;
    void updateArtwork();
    void onVolumeChanged(int value);
    void copyTrack();
    QPushButton *toolButton(const char *icon, const QString &tooltip);

    PlayerManager *player_;
    QLabel *artwork_;
    ClickableLabel *title_;
    QLabel *subtitle_;
    QLabel *error_;
    QPushButton *playButton_;
    QSlider *volume_;
    QTimer *copiedTimer_;
};

}  // namespace radio
