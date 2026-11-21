#pragma once

#include "dashboard_models.hpp"
#include "ui_config.hpp"

#include <QObject>
#include <QVector>

QT_BEGIN_NAMESPACE
class QThread;
QT_END_NAMESPACE

namespace data_extraction::dashboard {

class ModbusDeviceWorker;
class NatsPublisherService;

class ApplicationRuntime final : public QObject {
    Q_OBJECT

public:
    explicit ApplicationRuntime(UiAppConfig config, QObject* parent = nullptr);
    ~ApplicationRuntime() override;

    const UiAppConfig& config() const;

public slots:
    void start();
    void stop();

signals:
    void sensor_data_received(const data_extraction::dashboard::SensorRecord& record);
    void device_status_received(const data_extraction::dashboard::DeviceStatus& status);
    void nats_status_received(const data_extraction::dashboard::NatsConnectionStatus& status);
    void publish_metrics_received(const data_extraction::dashboard::PublishMetrics& metrics);
    void runtime_state_changed(data_extraction::dashboard::ConnectionState state, const QString& message);
    void alert_raised(const QString& message);

private:
    void create_workers();
    void connect_worker(ModbusDeviceWorker* worker, QThread* thread);

    UiAppConfig config_;
    bool started_ {false};
    QVector<QThread*> worker_threads_;
    QVector<ModbusDeviceWorker*> workers_;
    QThread* publisher_thread_ {nullptr};
    NatsPublisherService* publisher_ {nullptr};
};

}  // namespace data_extraction::dashboard
