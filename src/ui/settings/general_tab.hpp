#pragma once

// Вкладка «Основные»: автозапуск, поведение окна, обновление, язык.

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;

namespace radio {

class SettingsStore;

class GeneralTab : public QWidget {
    Q_OBJECT
public:
    explicit GeneralTab(SettingsStore *settings, QWidget *parent = nullptr);

private:
    void onAutostartToggled(bool enabled);
    void onLanguageChanged();

    SettingsStore *settings_;
    QCheckBox *autostart_;
    QComboBox *language_;
    QLabel *languageHint_;
};

}  // namespace radio
