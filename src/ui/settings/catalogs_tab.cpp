#include "settings/catalogs_tab.hpp"

#include "catalog.hpp"
#include "settings_store.hpp"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace radio {

CatalogsTab::CatalogsTab(SettingsStore *settings, CatalogUpdater *updater, QWidget *parent)
    : QWidget(parent), settings_(settings), updater_(updater), list_(new QListWidget), status_(new QLabel),
      removeButton_(new QPushButton(tr("Remove"))), updateButton_(new QPushButton(tr("Update now")))
{
    list_->setObjectName(u"catalogs"_s);
    // Ссылки длинные: сокращаем их посередине вместо горизонтальной прокрутки,
    // полный адрес остаётся в подсказке.
    list_->setTextElideMode(Qt::ElideMiddle);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    status_->setObjectName(u"status"_s);
    status_->setWordWrap(true);

    auto *addButton = new QPushButton(tr("Add catalog..."));
    connect(addButton, &QPushButton::clicked, this, &CatalogsTab::addCatalog);
    connect(removeButton_, &QPushButton::clicked, this, &CatalogsTab::removeCatalog);
    connect(updateButton_, &QPushButton::clicked, this, &CatalogsTab::updateNow);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(addButton);
    buttons->addWidget(removeButton_);
    buttons->addStretch();
    buttons->addWidget(updateButton_);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);
    layout->addWidget(status_);

    connect(updater, &CatalogUpdater::finished, this, &CatalogsTab::onUpdateFinished);
    connect(list_, &QListWidget::itemSelectionChanged, this, &CatalogsTab::updateButtons);
    reload();
}

void CatalogsTab::reload()
{
    list_->clear();
    for (const QString &url : updater_->builtinUrls()) {
        auto *item = new QListWidgetItem(url);
        item->setToolTip(url + u"\n\n"_s + tr("Built-in catalog — cannot be removed"));
        item->setFlags(Qt::ItemIsEnabled);
        list_->addItem(item);
    }
    for (const QString &url : settings_->extraCatalogUrls()) {
        auto *item = new QListWidgetItem(url);
        item->setToolTip(url);
        list_->addItem(item);
    }
    updateStatus();
    updateButtons();
}

void CatalogsTab::updateButtons()
{
    const QListWidgetItem *item = list_->currentItem();
    removeButton_->setEnabled(item != nullptr && settings_->extraCatalogUrls().contains(item->text()));
    updateButton_->setEnabled(!updater_->isRunning());
}

void CatalogsTab::updateStatus()
{
    if (updater_->isRunning()) {
        status_->setText(tr("Updating..."));
        return;
    }
    const auto elapsed = catalog::secondsSinceUpdate();
    status_->setText(elapsed ? tr("Last update: %1 ago.").arg(humanize(*elapsed))
                             : tr("The catalogs have never been updated."));
}

QString CatalogsTab::humanize(double seconds) const
{
    const int minutes = int(seconds / 60);
    if (minutes < 1)
        return tr("less than a minute");
    if (minutes < 60)
        return tr("%n minute(s)", "", minutes);
    const int hours = minutes / 60;
    if (hours < 24)
        return tr("%n hour(s)", "", hours);
    return tr("%n day(s)", "", hours / 24);
}

void CatalogsTab::addCatalog()
{
    const QString title = tr("New catalog");
    bool ok = false;
    const QString url = QInputDialog::getText(this, title, tr("Link to a JSON catalog:"), QLineEdit::Normal,
                                              u"https://"_s, &ok)
                            .trimmed();
    if (!ok || url.isEmpty())
        return;
    if (!isAcceptableCatalogUrl(url)) {
        QMessageBox::warning(this, title, tr("The link must start with https://"));
        return;
    }
    if (updater_->builtinUrls().contains(url) || !settings_->addCatalogUrl(url))
        return;
    reload();
    // Добавленный каталог нужен пользователю сейчас, а не после перезапуска.
    updateNow();
}

void CatalogsTab::removeCatalog()
{
    const QListWidgetItem *item = list_->currentItem();
    if (item != nullptr && settings_->removeCatalogUrl(item->text()))
        reload();
}

void CatalogsTab::updateNow()
{
    updater_->updateNow();
    updateStatus();
    updateButtons();
}

void CatalogsTab::onUpdateFinished(const QList<Catalog> &, bool changed)
{
    updateStatus();
    updateButtons();
    if (!changed)
        status_->setText(status_->text() + u' ' + tr("No changes."));
}

}  // namespace radio
