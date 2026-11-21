#include "json_parser.hpp"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace data_extraction::dashboard {
namespace {

QDateTime parse_iso8601(const QJsonValue& value) {
    const auto timestamp = QDateTime::fromString(value.toString(), Qt::ISODate);
    return timestamp.isValid() ? timestamp.toUTC() : QDateTime {};
}

JsonParser::Result invalid_result(const QString& message) {
    JsonParser::Result result;
    result.type = JsonParser::Result::Type::invalid;
    result.error_message = message;
    return result;
}

}  // namespace

QString connection_state_to_string(ConnectionState state) {
    switch (state) {
        case ConnectionState::connected:
            return "Connected";
        case ConnectionState::reconnecting:
            return "Reconnecting";
        case ConnectionState::disconnected:
        default:
            return "Disconnected";
    }
}

QColor connection_state_to_color(ConnectionState state) {
    switch (state) {
        case ConnectionState::connected:
            return QColor("#4caf50");
        case ConnectionState::reconnecting:
            return QColor("#ffb300");
        case ConnectionState::disconnected:
        default:
            return QColor("#e53935");
    }
}

ConnectionState connection_state_from_string(const QString& value) {
    const auto normalized = value.trimmed().toLower();
    if (normalized == "connected") {
        return ConnectionState::connected;
    }
    if (normalized == "reconnecting") {
        return ConnectionState::reconnecting;
    }
    return ConnectionState::disconnected;
}

JsonParser::Result JsonParser::parse_message(const QByteArray& payload) const {
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return invalid_result("Invalid JSON message.");
    }

    const auto object = document.object();
    const auto type = object.value("type").toString();
    if (type.isEmpty()) {
        return invalid_result("JSON message missing type field.");
    }

    if (type == "sensor_data") {
        if (!object.contains("device_id") || !object.contains("event_id") || 
            !object.contains("ts_utc")) {
            return invalid_result("sensor_data missing required fields.");
        }

        SensorRecord record;
        record.device_id = object.value("device_id").toString();
        record.event_id = object.value("event_id").toInt();
        record.timestamp = parse_iso8601(object.value("ts_utc"));
        record.device_health_status = object.value("device_health").toObject().value("status").toString("ok");
        
        // Parse readings
        const auto readings_array = object.value("readings").toArray();
        for (const auto& reading_val : readings_array) {
            const auto reading_obj = reading_val.toObject();
            Reading reading;
            reading.value = reading_obj.value("value").toDouble();
            reading.unit = reading_obj.value("unit").toString();
            record.readings.append(reading);
        }
        
        // Parse error if present
        const auto error_obj = object.value("error").toObject();
        if (!error_obj.isEmpty()) {
            record.error_code = error_obj.value("code").toString();
            record.error_message = error_obj.value("message").toString();
        }
        
        // Parse optional fields
        record.order_id = object.value("order_id").toString();
        record.batch_id = object.value("batch_id").toString();
        
        if (record.device_id.isEmpty() || !record.timestamp.isValid()) {
            return invalid_result("sensor_data contains invalid device_id or timestamp.");
        }

        Result result;
        result.type = Result::Type::sensor_data;
        result.sensor_record = record;
        return result;
    }

    if (type == "device_update") {
        if (!object.contains("device_id") || !object.contains("status") || !object.contains("ip") ||
            !object.contains("port") || !object.contains("last_seen")) {
            return invalid_result("device_update missing required fields.");
        }

        DeviceStatus status;
        status.device_id = object.value("device_id").toString();
        status.ip = object.value("ip").toString();
        status.port = static_cast<quint16>(object.value("port").toInt(502));
        status.status = connection_state_from_string(object.value("status").toString());
        status.last_seen = parse_iso8601(object.value("last_seen"));
        if (status.device_id.isEmpty() || status.ip.isEmpty()) {
            return invalid_result("device_update contains invalid identifiers.");
        }

        Result result;
        result.type = Result::Type::device_update;
        result.device_status = status;
        return result;
    }

    if (type == "nats_status") {
        if (!object.contains("status") || !object.contains("url") || !object.contains("retries")) {
            return invalid_result("nats_status missing required fields.");
        }

        NatsConnectionStatus status;
        status.status = connection_state_from_string(object.value("status").toString());
        status.url = object.value("url").toString();
        status.retries = object.value("retries").toInt();
        status.latency_ms = object.contains("latency_ms") ? object.value("latency_ms").toInt(-1) : -1;
        status.updated_at = QDateTime::currentDateTimeUtc();

        Result result;
        result.type = Result::Type::nats_status;
        result.nats_status = status;
        return result;
    }

    if (type == "publish_status") {
        if (!object.contains("total_sent") || !object.contains("failed") || !object.contains("retries") ||
            !object.contains("last_sent")) {
            return invalid_result("publish_status missing required fields.");
        }

        PublishMetrics metrics;
        metrics.total_sent = static_cast<quint64>(object.value("total_sent").toDouble());
        metrics.failed = static_cast<quint64>(object.value("failed").toDouble());
        metrics.retries = static_cast<quint64>(object.value("retries").toDouble());
        metrics.last_sent = parse_iso8601(object.value("last_sent"));

        Result result;
        result.type = Result::Type::publish_status;
        result.publish_metrics = metrics;
        return result;
    }

    return invalid_result("Unsupported message type: " + type);
}

}  // namespace data_extraction::dashboard
