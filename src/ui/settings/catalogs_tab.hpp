#pragma once

// Вкладка «Каталоги»: источники станций и их обновление.

#include "models.hpp"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace radio {

class CatalogUpdater;
class SettingsStore;

class CatalogsTab : public QWidget {
    Q_OBJECT
public:
    CatalogsTab(SettingsStore *settings, CatalogUpdater *updater, QWidget *parent = nullptr);

    // Прошедшее время словами. Формы числа расставляет Qt по правилам языка.
    QString humanize(double seconds) const;

private:
    void reload();
    void updateButtons();
    void updateStatus();
    void addCatalog();
    void removeCatalog();
    void updateNow();
    void onUpdateFinished(const QList<radio::Catalog> &catalogs, bool changed);

    SettingsStore *settings_;
    CatalogUpdater *updater_;
    QListWidget *list_;
    QLabel *status_;
    QPushButton *removeButton_;
    QPushButton *updateButton_;
};

}  // namespace radio
