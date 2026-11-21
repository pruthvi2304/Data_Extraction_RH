#pragma once

#include <QString>
#include <QVector>

namespace data_extraction::dashboard {

struct UiDeviceConfig {
    QString id;
    QString ip;
    quint16 port {502};
    QString type {"discrete"}; // "discrete" or "continuous"
};

struct UiNatsConfig {
    QString url {"nats://127.0.0.1:4222"};
    bool tls_enabled {true};
    QString tls_min_protocol {"1.3"};
    QString client_cert;
    QString client_key;
    QString ca_cert;
    QString nkey_public;
    QString nkey_seed_file;
    QString subject {"sensor.data"};
    bool jetstream_enabled {false};
    bool jetstream_auto_provision {false};
    QString jetstream_stream {"QS_PLANT_EVENTS"};
    int connect_timeout_ms {2000};
    int publish_retry_limit {5};
    int retry_backoff_ms {300};
    int retention_minutes {1440};
    QString local_buffer_file {"logs/publisher_buffer.ndjson"};
    QString central_db_path {"logs/central_events.sqlite"};
    bool enable_central_persistence {true};
    QString plant {"plant1"};
    QString work_center {"wc1"};
};

struct UiLoggingConfig {
    QString directory {"logs"};
    int max_file_size_mb {10};
    int max_files {5};
    int flush_every_ms {1000};
};

struct UiAppConfig {
    int request_timeout_ms {2000};
    int queue_capacity {5000};
    QVector<UiDeviceConfig> devices;
    UiNatsConfig nats;
    UiLoggingConfig logging;
};

}  // namespace data_extraction::dashboard
