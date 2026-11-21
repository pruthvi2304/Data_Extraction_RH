#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

class QColor;

namespace data_extraction::dashboard {

enum class ConnectionState {
    connected,
    disconnected,
    reconnecting
};

struct Reading {
    double value {0.0};
    QString unit;
    QString parameter_id;
    QString ispid;
};

struct SensorRecord {
    QString device_id;
    QString instrument_id;
    QString data_mode {"discrete"};
    QString plant_id;
    QString work_center;
    QString subject_type;
    QDateTime timestamp;
    QVector<Reading> readings;
    QStringList device_parameter_ids;
    QStringList selected_parameter_ids;
    QString device_health_status {"ok"}; // "ok" or "error"
    QString error_code;
    QString error_message;
    QString order_id;
    QString batch_id;
    QString operator_id;
    QString operation_id;
    QString event_code;
    bool publish_to_nats {true};
    qint64 event_id {0};
};

struct DeviceStatus {
    QString device_id;
    QString instrument_id;
    QString ip;
    quint16 port {502};
    ConnectionState status {ConnectionState::disconnected};
    QDateTime last_seen;
    QString certification;
};

struct NatsConnectionStatus {
    ConnectionState status {ConnectionState::disconnected};
    QString url;
    int retries {0};
    int latency_ms {-1};
    QDateTime updated_at;
};

struct PublishMetrics {
    quint64 total_sent {0};
    quint64 continuous_sent {0};
    quint64 event_sent {0};
    quint64 health_sent {0};
    quint64 failed {0};
    quint64 retries {0};
    QDateTime last_sent;
};

QString connection_state_to_string(ConnectionState state);
QColor connection_state_to_color(ConnectionState state);
ConnectionState connection_state_from_string(const QString& value);

}  // namespace data_extraction::dashboard

Q_DECLARE_METATYPE(data_extraction::dashboard::ConnectionState)
Q_DECLARE_METATYPE(data_extraction::dashboard::SensorRecord)
Q_DECLARE_METATYPE(data_extraction::dashboard::DeviceStatus)
Q_DECLARE_METATYPE(data_extraction::dashboard::NatsConnectionStatus)
Q_DECLARE_METATYPE(data_extraction::dashboard::PublishMetrics)
