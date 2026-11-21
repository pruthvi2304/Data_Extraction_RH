#pragma once

#include "dashboard_models.hpp"
#include "ui_config.hpp"

#include <QByteArray>
#include <QObject>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QAbstractSocket;
class QTcpSocket;
QT_END_NAMESPACE

namespace data_extraction::dashboard {

class ModbusDeviceWorker final : public QObject {
    Q_OBJECT

public:
    explicit ModbusDeviceWorker(UiDeviceConfig device_config, int request_timeout_ms, QObject* parent = nullptr);
    ~ModbusDeviceWorker() override;

public slots:
    void start();
    void stop();

signals:
    void sensor_data_received(const data_extraction::dashboard::SensorRecord& record);
    void device_status_changed(const data_extraction::dashboard::DeviceStatus& status);
    void worker_alert(const QString& message);
    void worker_trace(const QString& message);

private slots:
    void connect_device();
    void on_connected();
    void on_disconnected();
    void on_ready_read();
    void on_error_occurred(int socket_error);
    void on_request_timeout();

private:
    DeviceStatus build_status(ConnectionState state, const QDateTime& last_seen = QDateTime {}) const;
    void process_frames();
    bool handle_frame(const QByteArray& payload);
    void schedule_reconnect(const QString& reason);
    void emit_status(ConnectionState state, const QDateTime& last_seen = QDateTime {});
    void cleanup_socket();

    UiDeviceConfig device_config_;
    int request_timeout_ms_ {2000};
    QTcpSocket* socket_ {nullptr};
    QTimer* reconnect_timer_ {nullptr};
    QTimer* inactivity_timer_ {nullptr};
    QByteArray read_buffer_;
    int reconnect_attempts_ {0};
    int current_backoff_ms_ {1000};
    bool running_ {false};
    bool suppress_disconnect_reconnect_ {false};
};

}  // namespace data_extraction::dashboard
