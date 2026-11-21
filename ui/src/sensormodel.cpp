#include "sensormodel.hpp"

#include <QFile>
#include <QTextStream>

#include <algorithm>

namespace data_extraction::dashboard {

SensorTableModel::SensorTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int SensorTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : visible_records_.size();
}

int SensorTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 4;
}

QVariant SensorTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= visible_records_.size()) {
        return {};
    }

    const auto& item = visible_records_.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0:
                return item.device_id;
            case 1:
                return item.readings.size() > 0 ? QString::number(item.readings[0].value, 'f', 1) : "N/A";
            case 2:
                return item.readings.size() > 1 ? QString::number(item.readings[1].value, 'f', 1) : "N/A";
            case 3:
                return item.timestamp.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
            default:
                return {};
        }
    }

    return {};
}

QVariant SensorTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section) {
        case 0:
            return "Device";
        case 1:
            return "Humidity";
        case 2:
            return "Temperature";
        case 3:
            return "Timestamp";
        default:
            return {};
    }
}

void SensorTableModel::append_record(const SensorRecord& record) {
    auto& records = records_by_device_[record.device_id];
    records.push_back(record);
    while (records.size() > 100U) {
        records.pop_front();
    }

    if (!paused_) {
        rebuild_visible_records();
    }
    emit data_summary_changed();
}

void SensorTableModel::set_filter_device_id(const QString& device_id) {
    filter_device_id_ = device_id.trimmed();
    rebuild_visible_records();
}

QString SensorTableModel::filter_device_id() const {
    return filter_device_id_;
}

void SensorTableModel::set_paused(bool paused) {
    paused_ = paused;
    if (!paused_) {
        rebuild_visible_records();
    }
}

bool SensorTableModel::is_paused() const {
    return paused_;
}

bool SensorTableModel::export_visible_to_csv(const QString& file_path) const {
    QFile file(file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream output(&file);
    output << "event_id,device_id,temperature,humidity,timestamp,status\n";
    for (const auto& record : visible_records_) {
        const QString humidity = record.readings.size() > 0 ? QString::number(record.readings[0].value, 'f', 1) : "N/A";
        const QString temperature = record.readings.size() > 1 ? QString::number(record.readings[1].value, 'f', 1) : "N/A";
        output << record.event_id << ','
               << record.device_id << ','
               << temperature << ','
               << humidity << ','
               << record.timestamp.toUTC().toString(Qt::ISODate) << ','
               << record.device_health_status << '\n';
    }
    return true;
}

int SensorTableModel::device_count() const {
    return records_by_device_.size();
}

int SensorTableModel::total_visible_records() const {
    return visible_records_.size();
}

void SensorTableModel::rebuild_visible_records() {
    beginResetModel();
    visible_records_.clear();

    for (auto it = records_by_device_.cbegin(); it != records_by_device_.cend(); ++it) {
        if (!filter_device_id_.isEmpty() && !it.key().contains(filter_device_id_, Qt::CaseInsensitive)) {
            continue;
        }

        for (const auto& record : it.value()) {
            visible_records_.push_back(record);
        }
    }

    std::sort(visible_records_.begin(), visible_records_.end(), [](const SensorRecord& left, const SensorRecord& right) {
        return left.timestamp > right.timestamp;
    });

    endResetModel();
    emit data_summary_changed();
}

}  // namespace data_extraction::dashboard
