#pragma once

#include "dashboard_models.hpp"

#include <QAbstractTableModel>
#include <QHash>
#include <QVector>

#include <deque>

namespace data_extraction::dashboard {

class SensorTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit SensorTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void append_record(const SensorRecord& record);
    void set_filter_device_id(const QString& device_id);
    QString filter_device_id() const;
    void set_paused(bool paused);
    bool is_paused() const;
    bool export_visible_to_csv(const QString& file_path) const;
    int device_count() const;
    int total_visible_records() const;

signals:
    void data_summary_changed();

private:
    void rebuild_visible_records();

    QHash<QString, std::deque<SensorRecord>> records_by_device_;
    QVector<SensorRecord> visible_records_;
    QString filter_device_id_;
    bool paused_ {false};
};

}  // namespace data_extraction::dashboard
