#pragma once

// Иконки.
//
// Берём из системной темы — так они автоматически совпадают со светлым и тёмным
// оформлением. Там, где темы нет (минимальные окружения, не-KDE системы),
// подставляем встроенные в Qt стандартные пиктограммы.

#include <QIcon>
#include <QString>

namespace radio::icons {

inline constexpr char PLAY[] = "media-playback-start";
inline constexpr char STOP[] = "media-playback-stop";
inline constexpr char NEXT[] = "media-skip-forward";
inline constexpr char PREVIOUS[] = "media-skip-backward";
inline constexpr char SETTINGS[] = "configure";
// Иконка самого приложения. Ставится в hicolor, поэтому находится по теме;
// при запуске из каталога сборки её там ещё нет — тогда берём встроенную.
QString radio();

QIcon get(const QString &name);

}  // namespace radio::icons
