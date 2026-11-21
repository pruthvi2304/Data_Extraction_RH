#pragma once

#include "dashboard_models.hpp"

#include <QFrame>
#include <QHash>

#include <deque>

QT_BEGIN_NAMESPACE
class QLabel;
class QChart;
class QChartView;
class QDateTimeAxis;
class QLineSeries;
class QValueAxis;
QT_END_NAMESPACE

namespace data_extraction::dashboard {

class SensorChartWidget final : public QFrame {
    Q_OBJECT

public:
    explicit SensorChartWidget(QWidget* parent = nullptr);

public slots:
    void append_record(const data_extraction::dashboard::SensorRecord& record);
    void set_device_filter(const QString& device_id);
    void set_updates_paused(bool paused);

private:
    void refresh_chart();

    QHash<QString, std::deque<SensorRecord>> records_by_device_;
    QString active_device_id_;
    bool paused_ {false};
    QLabel* title_label_ {nullptr};
    QChart* chart_ {nullptr};
    QChartView* chart_view_ {nullptr};
    QLineSeries* temperature_series_ {nullptr};
    QLineSeries* humidity_series_ {nullptr};
    QDateTimeAxis* time_axis_ {nullptr};
    QValueAxis* value_axis_ {nullptr};
};

}  // namespace data_extraction::dashboard
