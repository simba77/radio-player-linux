#include "settings/general_tab.hpp"

#include "autostart.hpp"
#include "settings_store.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

// Названия языков не переводятся: каждый пишется на себе самом.
QString languageName(const QString &code)
{
    return code == "ru"_L1 ? u"Русский"_s : u"English"_s;
}

}  // namespace

GeneralTab::GeneralTab(SettingsStore *settings, QWidget *parent)
    : QWidget(parent), settings_(settings), autostart_(new QCheckBox(tr("Start at login"))),
      language_(new QComboBox), languageHint_(new QLabel(tr("The language will change after a restart.")))
{
    autostart_->setChecked(autostart::isEnabled());
    connect(autostart_, &QCheckBox::toggled, this, &GeneralTab::onAutostartToggled);

    auto *closeToTray = new QCheckBox(tr("Closing the window hides it in the tray"));
    closeToTray->setChecked(settings->closeToTray());
    connect(closeToTray, &QCheckBox::toggled, settings, &SettingsStore::setCloseToTray);

    auto *interval = new QSpinBox;
    interval->setRange(MIN_UPDATE_INTERVAL_HOURS, MAX_UPDATE_INTERVAL_HOURS);
    interval->setValue(settings->updateIntervalHours());
    interval->setSuffix(tr(" h"));
    connect(interval, &QSpinBox::valueChanged, settings, &SettingsStore::setUpdateIntervalHours);

    language_->addItem(tr("System"), QLatin1String(LANGUAGE_AUTO));
    for (const QString &code : LANGUAGES)
        language_->addItem(languageName(code), code);
    language_->setCurrentIndex(std::max(0, language_->findData(settings->language())));
    connect(language_, &QComboBox::currentIndexChanged, this, &GeneralTab::onLanguageChanged);

    languageHint_->setWordWrap(true);
    languageHint_->hide();

    auto *form = new QFormLayout;
    form->addRow(autostart_);
    form->addRow(closeToTray);
    form->addRow(tr("Update catalogs every"), interval);
    form->addRow(tr("Interface language"), language_);
    form->addRow(QString(), languageHint_);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addStretch();
}

void GeneralTab::onAutostartToggled(bool enabled)
{
    const bool actual = autostart::setEnabled(enabled);
    if (actual == enabled)
        return;
    // Не удалось записать `.desktop` — возвращаем флажок к правде.
    {
        const QSignalBlocker blocker(autostart_);
        autostart_->setChecked(actual);
    }
    QMessageBox::warning(this, tr("Start at login"), tr("Could not change the startup setting."));
}

void GeneralTab::onLanguageChanged()
{
    settings_->setLanguage(language_->currentData().toString());
    // Перерисовать уже собранный интерфейс нечем — предупреждаем честно.
    languageHint_->show();
}

}  // namespace radio
