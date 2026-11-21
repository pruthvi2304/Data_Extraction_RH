#include "sensorchartwidget.hpp"

#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLegend>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <limits>

namespace data_extraction::dashboard {

SensorChartWidget::SensorChartWidget(QWidget* parent) : QFrame(parent) {
    setObjectName("chartCard");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    title_label_ = new QLabel("Live Trends", this);
    title_label_->setObjectName("cardTitle");
    layout->addWidget(title_label_);

    chart_ = new QChart();
    chart_->setBackgroundVisible(false);
    chart_->legend()->setVisible(true);
    chart_->legend()->setLabelColor(QColor("#f5f7fb"));

    temperature_series_ = new QLineSeries(chart_);
    humidity_series_ = new QLineSeries(chart_);
    temperature_series_->setName("Temperature");
    humidity_series_->setName("Humidity");
    temperature_series_->setColor(QColor("#4fc3f7"));
    humidity_series_->setColor(QColor("#81c784"));

    chart_->addSeries(temperature_series_);
    chart_->addSeries(humidity_series_);

    time_axis_ = new QDateTimeAxis(chart_);
    time_axis_->setFormat("HH:mm:ss");
    time_axis_->setLabelsColor(QColor("#9ea8b7"));

    value_axis_ = new QValueAxis(chart_);
    value_axis_->setLabelFormat("%.1f");
    value_axis_->setLabelsColor(QColor("#9ea8b7"));

    chart_->addAxis(time_axis_, Qt::AlignBottom);
    chart_->addAxis(value_axis_, Qt::AlignLeft);
    temperature_series_->attachAxis(time_axis_);
    temperature_series_->attachAxis(value_axis_);
    humidity_series_->attachAxis(time_axis_);
    humidity_series_->attachAxis(value_axis_);

    chart_view_ = new QChartView(chart_, this);
    chart_view_->setRenderHint(QPainter::Antialiasing, true);
    chart_view_->setMinimumHeight(280);
    layout->addWidget(chart_view_);
}

void SensorChartWidget::append_record(const SensorRecord& record) {
    auto& records = records_by_device_[record.device_id];
    records.push_back(record);
    while (records.size() > 100U) {
        records.pop_front();
    }

    if (active_device_id_.isEmpty()) {
        active_device_id_ = record.device_id;
    }

    if (!paused_ && (active_device_id_ == record.device_id || active_device_id_.trimmed().isEmpty())) {
        refresh_chart();
    }
}

void SensorChartWidget::set_device_filter(const QString& device_id) {
    const auto normalized = device_id.trimmed();
    if (normalized.isEmpty()) {
        active_device_id_.clear();
        if (!records_by_device_.isEmpty()) {
            active_device_id_ = records_by_device_.cbegin().key();
        }
    } else {
        active_device_id_ = normalized;
    }
    refresh_chart();
}

void SensorChartWidget::set_updates_paused(bool paused) {
    paused_ = paused;
    if (!paused_) {
        refresh_chart();
    }
}

void SensorChartWidget::refresh_chart() {
    temperature_series_->clear();
    humidity_series_->clear();

    if (active_device_id_.isEmpty() || !records_by_device_.contains(active_device_id_)) {
        title_label_->setText("Live Trends");
        return;
    }

    title_label_->setText("Live Trends - " + active_device_id_);
    const auto& records = records_by_device_.value(active_device_id_);
    if (records.empty()) {
        return;
    }

    qint64 min_time = std::numeric_limits<qint64>::max();
    qint64 max_time = std::numeric_limits<qint64>::min();
    double min_value = std::numeric_limits<double>::max();
    double max_value = std::numeric_limits<double>::lowest();

    for (const auto& record : records) {
        const auto msecs = record.timestamp.toMSecsSinceEpoch();
        // Controller records store humidity first and temperature second.
        if (record.readings.size() > 0) humidity_series_->append(msecs, record.readings[0].value);
        if (record.readings.size() > 1) temperature_series_->append(msecs, record.readings[1].value);
        min_time = std::min(min_time, msecs);
        max_time = std::max(max_time, msecs);
        double min_val = std::numeric_limits<double>::max();
        double max_val = std::numeric_limits<double>::lowest();
        for (const auto& reading : record.readings) {
            min_val = std::min(min_val, reading.value);
            max_val = std::max(max_val, reading.value);
        }
        min_value = std::min(min_value, min_val);
        max_value = std::max(max_value, max_val);
    }

    time_axis_->setRange(QDateTime::fromMSecsSinceEpoch(min_time), QDateTime::fromMSecsSinceEpoch(max_time));
    if (min_time == max_time) {
        time_axis_->setRange(
            QDateTime::fromMSecsSinceEpoch(min_time - 1000),
            QDateTime::fromMSecsSinceEpoch(max_time + 1000));
    }
    if (min_value == max_value) {
        min_value -= 1.0;
        max_value += 1.0;
    }
    value_axis_->setRange(min_value - 2.0, max_value + 2.0);
}

}  // namespace data_extraction::dashboard
