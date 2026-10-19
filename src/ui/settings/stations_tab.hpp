#pragma once

// Вкладка «Станции»: видимость, названия, порядок, свои станции, импорт.

#include <QDialog>
#include <QWidget>

#include <functional>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace radio {

class StationStore;

// Название и адрес потока в одном окне.
//
// Оба поля видны сразу, «ОК» неактивна, пока введено не всё, а причина
// написана прямо под полями: два диалога подряд не давали вернуться
// к названию, а отмена на втором шаге теряла введённое.
class StationDialog : public QDialog {
    Q_OBJECT
public:
    explicit StationDialog(QWidget *parent = nullptr, const QString &name = {}, const QString &url = {});

    QString name() const;
    QString url() const;

private:
    void validate();

    QLineEdit *name_;
    QLineEdit *url_;
    QLabel *hint_;
    QDialogButtonBox *buttons_;
};

class StationsTab : public QWidget {
    Q_OBJECT
public:
    explicit StationsTab(StationStore *store, QWidget *parent = nullptr);

private:
    void reload();
    void addRow(const QString &stationId, const QString &name, const QString &url, bool visible);
    QString selectedId() const;
    void updateButtons();

    void onItemChanged(QListWidgetItem *item);
    // Порядок задаётся видимыми строками — их и отправляем в хранилище.
    void onRowsMoved();
    // Применяет правки, отложив перестроение списка до выхода из обработчика:
    // перестроение уничтожило бы тот самый элемент, из которого мы пришли.
    void batch(const std::function<void()> &edits);

    void addStation();
    void duplicateStation();
    void removeStation();
    void importStations();
    void exportStations();
    void reset();

    StationStore *store_;
    QListWidget *list_;
    QPushButton *duplicateButton_;
    QPushButton *removeButton_;
    bool applying_ = false;
};

}  // namespace radio
