#include "settings/stations_tab.hpp"

#include "playlist_io.hpp"
#include "station_store.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

// Роль Qt для хранения id станции в строке списка.
constexpr int STATION_ID_ROLE = Qt::UserRole;
// Красный поверх приглушённой палитры — тот же цвет, что у ошибки плеера.
const QString ERROR_STYLE = u"color: #e05252;"_s;

}  // namespace

// MARK: - StationDialog

StationDialog::StationDialog(QWidget *parent, const QString &name, const QString &url)
    : QDialog(parent), name_(new QLineEdit(name)), url_(new QLineEdit(url)), hint_(new QLabel),
      buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel))
{
    setWindowTitle(tr("New station"));
    setMinimumWidth(380);

    name_->setPlaceholderText(tr("Station name"));
    url_->setPlaceholderText(u"https://example.org/stream.m3u8"_s);

    hint_->setWordWrap(true);
    // Подсказку приглушаем прозрачностью, а не фиксированным цветом: так она
    // читается и в светлой, и в тёмной теме.
    QPalette palette = hint_->palette();
    QColor muted = palette.color(QPalette::WindowText);
    muted.setAlphaF(0.65f);
    palette.setColor(QPalette::WindowText, muted);
    hint_->setPalette(palette);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow(tr("Name:"), name_);
    form->addRow(tr("Stream URL:"), url_);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint_);
    layout->addWidget(buttons_);

    connect(name_, &QLineEdit::textChanged, this, &StationDialog::validate);
    connect(url_, &QLineEdit::textChanged, this, &StationDialog::validate);
    validate();
}

QString StationDialog::name() const
{
    return name_->text().trimmed();
}

QString StationDialog::url() const
{
    return url_->text().trimmed();
}

void StationDialog::validate()
{
    const QString address = url();
    const bool badScheme = !address.isEmpty() && !isPlayableUrl(address);
    hint_->setStyleSheet(badScheme ? ERROR_STYLE : QString());
    hint_->setText(badScheme ? tr("The URL must start with http:// or https://")
                             : tr("A direct stream URL or an HLS playlist (.m3u8)."));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!name().isEmpty() && !address.isEmpty() && !badScheme);
}

// MARK: - StationsTab

StationsTab::StationsTab(StationStore *store, QWidget *parent)
    : QWidget(parent), store_(store), list_(new QListWidget),
      duplicateButton_(new QPushButton(tr("Duplicate"))), removeButton_(new QPushButton(tr("Remove")))
{
    list_->setObjectName(u"stations"_s);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    connect(list_->model(), &QAbstractItemModel::rowsMoved, this, &StationsTab::onRowsMoved);
    connect(list_, &QListWidget::itemChanged, this, &StationsTab::onItemChanged);
    connect(list_, &QListWidget::itemSelectionChanged, this, &StationsTab::updateButtons);

    auto *addButton = new QPushButton(tr("Add..."));
    connect(addButton, &QPushButton::clicked, this, &StationsTab::addStation);
    duplicateButton_->setToolTip(
        tr("Creates your own copy of a catalog station — its stream URL can be edited"));
    connect(duplicateButton_, &QPushButton::clicked, this, &StationsTab::duplicateStation);
    connect(removeButton_, &QPushButton::clicked, this, &StationsTab::removeStation);
    auto *importButton = new QPushButton(tr("Import..."));
    connect(importButton, &QPushButton::clicked, this, &StationsTab::importStations);
    auto *exportButton = new QPushButton(tr("Export..."));
    connect(exportButton, &QPushButton::clicked, this, &StationsTab::exportStations);
    auto *resetButton = new QPushButton(tr("Reset"));
    resetButton->setToolTip(tr("Return the list to the catalog state"));
    connect(resetButton, &QPushButton::clicked, this, &StationsTab::reset);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(addButton);
    buttons->addWidget(duplicateButton_);
    buttons->addWidget(removeButton_);
    buttons->addStretch();
    buttons->addWidget(importButton);
    buttons->addWidget(exportButton);

    auto *hint =
        new QLabel(tr("Uncheck a station to hide it. Rename with a double click, reorder by dragging."));
    hint->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);
    layout->addWidget(resetButton, 0, Qt::AlignRight);

    connect(store, &StationStore::stationsChanged, this, &StationsTab::reload);
    reload();
}

// MARK: - Наполнение

void StationsTab::reload()
{
    // Пока применяется правка, перестраивать список нельзя: он уничтожит
    // тот самый элемент, из обработчика которого мы пришли.
    if (applying_)
        return;
    {
        const QSignalBlocker blocker(list_);
        list_->clear();
        for (const Station &station : store_->stations())
            addRow(station.id, station.name, station.url, true);
        for (const Station &station : store_->hiddenStations())
            addRow(station.id, station.name, station.url, false);
    }
    updateButtons();
}

void StationsTab::addRow(const QString &stationId, const QString &name, const QString &url, bool visible)
{
    auto *item = new QListWidgetItem(name);
    item->setData(STATION_ID_ROLE, stationId);
    item->setToolTip(url);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
    item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    list_->addItem(item);
}

QString StationsTab::selectedId() const
{
    const QListWidgetItem *item = list_->currentItem();
    return item != nullptr ? item->data(STATION_ID_ROLE).toString() : QString();
}

void StationsTab::updateButtons()
{
    const auto station = store_->find(selectedId());
    duplicateButton_->setEnabled(station && !station->isCustom());
    removeButton_->setEnabled(station && station->isCustom());
}

// MARK: - Действия

void StationsTab::onItemChanged(QListWidgetItem *item)
{
    const QString stationId = item->data(STATION_ID_ROLE).toString();
    if (stationId.isEmpty() || applying_)
        return;
    // Считываем всё до изменений: после первой правки элемент будет уничтожен.
    const QString name = item->text();
    const bool visible = item->checkState() == Qt::Checked;

    batch([&] {
        store_->rename(stationId, name);
        if (visible)
            store_->unhide(stationId);
        else
            store_->hide(stationId);
    });
}

void StationsTab::onRowsMoved()
{
    if (applying_)
        return;
    QStringList order;
    for (int row = 0; row < list_->count(); ++row) {
        const QListWidgetItem *item = list_->item(row);
        if (item->checkState() == Qt::Checked)
            order.append(item->data(STATION_ID_ROLE).toString());
    }
    batch([&] { store_->setOrder(order); });
}

void StationsTab::batch(const std::function<void()> &edits)
{
    applying_ = true;
    edits();
    applying_ = false;
    QTimer::singleShot(0, this, &StationsTab::reload);
}

void StationsTab::addStation()
{
    StationDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    // Пустых значений диалог не выпускает — проверять их здесь уже не за чем.
    store_->addCustom(dialog.name(), dialog.url());
}

void StationsTab::duplicateStation()
{
    const QString stationId = selectedId();
    if (!stationId.isEmpty())
        store_->duplicate(stationId);
}

void StationsTab::removeStation()
{
    const auto station = store_->find(selectedId());
    if (!station)
        return;
    const auto answer =
        QMessageBox::question(this, tr("Remove"), tr("Remove the station «%1»?").arg(station->name));
    if (answer == QMessageBox::Yes)
        store_->removeCustom(station->id);
}

void StationsTab::importStations()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import stations"), QString(), tr("Playlists (*.m3u *.m3u8 *.pls *.json);;All files (*)"));
    if (path.isEmpty())
        return;
    const QList<Station> stations = playlist::readFile(path);
    if (stations.isEmpty()) {
        QMessageBox::warning(this, tr("Import"), tr("No stations were found in the file."));
        return;
    }
    for (const Station &station : stations)
        store_->addCustom(station.name, station.url);
    QMessageBox::information(this, tr("Import"), tr("%n station(s) added.", "", int(stations.size())));
}

void StationsTab::exportStations()
{
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Export stations"), u"stations.m3u"_s, tr("Playlist (*.m3u)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const bool saved = file.open(QIODevice::WriteOnly) &&
                       file.write(playlist::exportM3u(store_->stations()).toUtf8()) >= 0 && file.commit();
    if (!saved)
        QMessageBox::warning(this, tr("Export"), tr("Could not save the file: %1").arg(file.errorString()));
}

void StationsTab::reset()
{
    const auto answer = QMessageBox::question(
        this, tr("Reset"), tr("Return the station list to the catalog state? Your edits will be lost."));
    if (answer == QMessageBox::Yes)
        store_->reset();
}

}  // namespace radio
