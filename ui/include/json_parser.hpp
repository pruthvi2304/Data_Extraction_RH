#pragma once

#include "dashboard_models.hpp"

#include <QByteArray>
#include <QString>

#include <optional>

namespace data_extraction::dashboard {

class JsonParser {
public:
    struct Result {
        enum class Type {
            sensor_data,
            device_update,
            nats_status,
            publish_status,
            invalid
        };

        Type type {Type::invalid};
        std::optional<SensorRecord> sensor_record;
        std::optional<DeviceStatus> device_status;
        std::optional<NatsConnectionStatus> nats_status;
        std::optional<PublishMetrics> publish_metrics;
        QString error_message;
    };

    Result parse_message(const QByteArray& payload) const;
};

}  // namespace data_extraction::dashboard
