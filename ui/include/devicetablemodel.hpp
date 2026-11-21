#pragma once

#include "dashboard_models.hpp"

#include <QAbstractTableModel>
#include <QHash>
#include <QVector>

namespace data_extraction::dashboard {

class DeviceTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit DeviceTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void set_initial_devices(const QList<DeviceStatus>& devices);
    void upsert_device(const DeviceStatus& device);
    int connected_count() const;
    int reconnecting_count() const;
    int disconnected_count() const;

private:
    void sort_devices();

    QVector<DeviceStatus> devices_;
    QHash<QString, int> row_by_id_;
};

}  // namespace data_extraction::dashboard
