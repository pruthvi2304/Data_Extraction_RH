#pragma once

#include "devicetablemodel.hpp"
#include "metricswidget.hpp"
#include "natsstatuswidget.hpp"
#include "sensorchartwidget.hpp"
#include "sensormodel.hpp"
#include "ui_config.hpp"

#include <QWidget>
#include <QHash>
#include <QVector>
#include <QStringList>

class QLabel;
class QTableView;
class QGridLayout;
class QDialog;
class SensorChartWidget;
class QResizeEvent;

namespace data_extraction::dashboard {

class ApplicationRuntime;

class DashboardWidget final : public QWidget {
    Q_OBJECT

public:
    explicit DashboardWidget(const UiAppConfig* app_config, QWidget* parent = nullptr);

    void attach_runtime(ApplicationRuntime* runtime);

protected:
    void resizeEvent(QResizeEvent* event) override;

signals:
    void status_message_changed(const QString& message);

private slots:
    void show_alert(const QString& message);
    void refresh_health_summary();
    void update_device_card(const data_extraction::dashboard::DeviceStatus& status);
    void update_device_reading(const data_extraction::dashboard::SensorRecord& record);

private:
    QWidget* create_card(const QString& title, QWidget* content);
    QWidget* build_health_card();
    QWidget* build_device_card();
    void open_device_details(const QString& device_id);
    void relayout_device_cards();

    DeviceTableModel* device_model_ {nullptr};
    SensorTableModel* sensor_model_ {nullptr};
    MetricsWidget* metrics_widget_ {nullptr};
    NatsStatusWidget* nats_status_widget_ {nullptr};
    QLabel* alert_label_ {nullptr};
    QLabel* health_summary_label_ {nullptr};
    QWidget* device_cards_container_ {nullptr};
    QGridLayout* device_cards_layout_ {nullptr};
    QHash<QString, QWidget*> device_cards_;
    QStringList device_card_order_;
    QHash<QString, QLabel*> device_reading_labels_;
    QHash<QString, QLabel*> device_status_labels_;
    QHash<QString, QDialog*> detail_dialogs_;
    QHash<QString, SensorTableModel*> detail_models_;
    QHash<QString, SensorChartWidget*> detail_charts_;
    QHash<QString, QVector<SensorRecord>> recent_records_;
};

}  // namespace data_extraction::dashboard
