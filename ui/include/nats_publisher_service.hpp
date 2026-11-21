#pragma once

#include "central_event_store.hpp"
#include "dashboard_models.hpp"
#include "ui_config.hpp"

#include <QDateTime>
#include <QHash>
#include <QQueue>
#include <QTimer>

#include <QObject>

namespace data_extraction::dashboard {

class NatsPublisherService final : public QObject {
    Q_OBJECT

public:
    explicit NatsPublisherService(UiNatsConfig config, int queue_capacity, QObject* parent = nullptr);
    ~NatsPublisherService() override;

public slots:
    void start();
    void stop();
    void set_capture_session_active(bool active);
    void set_capture_paused(bool paused);
    void enqueue_record(SensorRecord record);
    void enqueue_manual_record(SensorRecord record);
    void enqueue_health_status(DeviceStatus status);
    void enqueue_event(const QString& event_code, const QString& device_id, const QString& instrument_id, const QString& operator_id, const QString& po, const QString& lot, const QString& operation_id);

signals:
    void status_changed(const data_extraction::dashboard::NatsConnectionStatus& status);
    void metrics_changed(const data_extraction::dashboard::PublishMetrics& metrics);
    void alert_raised(const QString& message);

private slots:
    void process_queue();
    void initialize_timer();

private:
    struct PendingRecord {
        SensorRecord record;
        bool force_publish {true};
    };

    void enqueue_record_internal(SensorRecord record, bool force_publish);
    QString serialize_record(const SensorRecord& record) const;
    QString serialize_health_status(const DeviceStatus& status, qint64 event_id) const;
    QString subject_for_record(const SensorRecord& record) const;
    QString subject_for_health_status(const DeviceStatus& status) const;
    bool ensure_connected();
    bool ensure_jetstream_stream();
    bool publish_payload(const QString& payload, const QString& subject);
    bool replay_buffered_events();
    bool buffer_locally(const QString& payload, const QString& subject) const;
    qint64 next_event_id(const QString& device_id);
    QString dedupe_key_for_record(const SensorRecord& record) const;
    void persist_payload_to_central_store(const QString& payload, const QString& subject, const QString& device_id, const QString& instrument_id, const QString& event_type, const QString& dedupe_key);
    void load_event_counters();
    void save_event_counters() const;
    QString event_counter_file() const;
    void emit_status(ConnectionState state, int latency_ms = -1);
    void emit_metrics();
    void close_connection();

    UiNatsConfig config_;
    int queue_capacity_ {5000};
    QQueue<PendingRecord> queue_;
    QTimer* drain_timer_ {nullptr};
    bool running_ {false};
    bool capture_session_active_ {false};
    bool capture_paused_ {false};
    quint32 consecutive_failures_ {0};
    quint64 total_sent_ {0};
    quint64 continuous_sent_ {0};
    quint64 event_sent_ {0};
    quint64 health_sent_ {0};
    quint64 failed_messages_ {0};
    quint64 retry_count_ {0};
    QDateTime last_sent_;
    void* connection_handle_ {nullptr};
    void* jetstream_context_ {nullptr};
    QHash<QString, qint64> event_counters_;
    CentralEventStore central_store_;
};

}  // namespace data_extraction::dashboard
