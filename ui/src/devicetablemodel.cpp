#include "devicetablemodel.hpp"

#include <QBrush>

#include <algorithm>

namespace data_extraction::dashboard {

DeviceTableModel::DeviceTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int DeviceTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : devices_.size();
}

int DeviceTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 5;
}

QVariant DeviceTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= devices_.size()) {
        return {};
    }

    const auto& item = devices_.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0:
                return item.device_id;
            case 1:
                return item.ip;
            case 2:
                return item.port;
            case 3:
                return connection_state_to_string(item.status);
            case 4:
                return item.last_seen.isValid() ? item.last_seen.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : "Never";
            default:
                return {};
        }
    }

    if (role == Qt::ForegroundRole && index.column() == 3) {
        return QBrush(connection_state_to_color(item.status));
    }

    return {};
}

QVariant DeviceTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section) {
        case 0:
            return "Device";
        case 1:
            return "IP";
        case 2:
            return "Port";
        case 3:
            return "Status";
        case 4:
            return "Last Seen";
        default:
            return {};
    }
}

void DeviceTableModel::set_initial_devices(const QList<DeviceStatus>& devices) {
    beginResetModel();
    devices_ = QVector<DeviceStatus>(devices.begin(), devices.end());
    sort_devices();
    endResetModel();
}

void DeviceTableModel::upsert_device(const DeviceStatus& device) {
    beginResetModel();
    if (!row_by_id_.contains(device.device_id)) {
        devices_.push_back(device);
    } else {
        devices_[row_by_id_.value(device.device_id)] = device;
    }
    sort_devices();
    endResetModel();
}

int DeviceTableModel::connected_count() const {
    return std::count_if(devices_.cbegin(), devices_.cend(), [](const auto& item) {
        return item.status == ConnectionState::connected;
    });
}

int DeviceTableModel::reconnecting_count() const {
    return std::count_if(devices_.cbegin(), devices_.cend(), [](const auto& item) {
        return item.status == ConnectionState::reconnecting;
    });
}

int DeviceTableModel::disconnected_count() const {
    return std::count_if(devices_.cbegin(), devices_.cend(), [](const auto& item) {
        return item.status == ConnectionState::disconnected;
    });
}

void DeviceTableModel::sort_devices() {
    std::sort(devices_.begin(), devices_.end(), [](const DeviceStatus& left, const DeviceStatus& right) {
        return left.device_id.localeAwareCompare(right.device_id) < 0;
    });

    row_by_id_.clear();
    for (int row = 0; row < devices_.size(); ++row) {
        row_by_id_.insert(devices_.at(row).device_id, row);
    }
}

}  // namespace data_extraction::dashboard
