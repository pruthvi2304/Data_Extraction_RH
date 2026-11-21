#include "nats_publisher_service.hpp"

#include "timestamp_utils.hpp"
#include "ui_file_logger.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QCoreApplication>
#include <QUrl>

#include <utility>

#ifdef HAVE_NATS
#if __has_include(<nats/nats.h>)
#include <nats/nats.h>
#elif __has_include(<nats.h>)
#include <nats.h>
#else
#error "HAVE_NATS is set, but no usable nats.c header was found."
#endif
#endif

namespace data_extraction::dashboard {
namespace {
void ensure_database_runtime() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;
#ifdef Q_OS_WIN
    const QString plugin_dir = QCoreApplication::applicationDirPath() + QStringLiteral("/sqldrivers");
    QCoreApplication::addLibraryPath(plugin_dir);
    qputenv("QT_PLUGIN_PATH", plugin_dir.toUtf8());
#endif
}

bool is_postgres_connection(const QString& value) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    return trimmed.startsWith(QStringLiteral("postgres://"), Qt::CaseInsensitive) ||
           trimmed.startsWith(QStringLiteral("postgresql://"), Qt::CaseInsensitive) ||
           trimmed.contains(QStringLiteral("dbname="), Qt::CaseInsensitive) ||
           trimmed.contains(QStringLiteral("host="), Qt::CaseInsensitive);
}

bool configure_database_connection(QSqlDatabase& database,
                                   const QString& connection_string,
                                   const QString& connection_name) {
    const QString driver = is_postgres_connection(connection_string) ? QStringLiteral("QPSQL") : QStringLiteral("QSQLITE");
    database = QSqlDatabase::addDatabase(driver, connection_name);
    if (driver == QLatin1String("QPSQL")) {
        const QUrl url(connection_string);
        database.setHostName(url.host().isEmpty() ? QStringLiteral("127.0.0.1") : url.host());
        database.setPort(url.port(5432));
        database.setDatabaseName(url.path().remove(0, 1));
        database.setUserName(url.userName().isEmpty() ? QStringLiteral("postgres") : url.userName());
        database.setPassword(url.password());
        database.setConnectOptions(QStringLiteral("sslmode=disable"));
    } else {
        database.setDatabaseName(connection_string);
        database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    }
    return database.open();
}

void close_and_remove_database(QSqlDatabase& database, const QString& connection_name) {
    if (database.isValid()) {
        database.close();
    }
    database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_name);
}

QString offline_database_connection(const UiNatsConfig& config) {
    // Force central DB for offline buffering to local SQLite file
    return QCoreApplication::applicationDirPath() + QStringLiteral("/logs/publisher_offline.sqlite");
}

QJsonValue contract_event_code(const QString& event_code) {
    const QString normalized = event_code.trimmed();
    if (normalized.compare(QStringLiteral("Start"), Qt::CaseInsensitive) == 0) {
        return 1;
    }
    if (normalized.compare(QStringLiteral("Stop"), Qt::CaseInsensitive) == 0) {
        return 2;
    }
    if (normalized.compare(QStringLiteral("Pause"), Qt::CaseInsensitive) == 0) {
        return 3;
    }
    if (normalized.compare(QStringLiteral("Login"), Qt::CaseInsensitive) == 0) {
        return 4;
    }
    if (normalized.compare(QStringLiteral("Logout"), Qt::CaseInsensitive) == 0) {
        return 5;
    }

    bool is_numeric = false;
    const int numeric_code = normalized.toInt(&is_numeric);
    return is_numeric ? QJsonValue(numeric_code) : QJsonValue(normalized);
}

bool is_contract_error_code(const QString& code) {
    return code == QLatin1String("ERR_TRANSFER_TIMEOUT") ||
           code == QLatin1String("ERR_INSTRUMENT_READ_FAIL") ||
           code == QLatin1String("ERR_AUTH_FAIL") ||
           code == QLatin1String("ERR_DEVICE_OFFLINE");
}

QString normalize_contract_error_code(const QString& code) {
    const QString normalized = code.trimmed();
    if (is_contract_error_code(normalized)) {
        return normalized;
    }
    return QStringLiteral("ERR_INSTRUMENT_READ_FAIL");
}

QString default_error_message_for_code(const QString& code) {
    if (code == QLatin1String("ERR_TRANSFER_TIMEOUT")) {
        return QStringLiteral("Data transfer to NATS timed out.");
    }
    if (code == QLatin1String("ERR_AUTH_FAIL")) {
        return QStringLiteral("Operator authentication failed.");
    }
    if (code == QLatin1String("ERR_DEVICE_OFFLINE")) {
        return QStringLiteral("Device lost network connectivity.");
    }
    return QStringLiteral("The instrument data reading failed.");
}

QString message_id_from_payload(const QString& payload) {
    QJsonParseError parse_error{};
    const auto document = QJsonDocument::fromJson(payload.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    const QJsonValue id_value = document.object().value(QStringLiteral("id"));
    if (id_value.isDouble()) {
        return QString::number(static_cast<qint64>(id_value.toDouble()));
    }
    if (id_value.isString()) {
        return id_value.toString();
    }
    return {};
}

QString create_table_id_column(const QSqlDatabase& database) {
    return database.driverName() == QLatin1String("QPSQL") ? QStringLiteral("BIGSERIAL PRIMARY KEY")
                                                        : QStringLiteral("INTEGER PRIMARY KEY AUTOINCREMENT");
}

bool persist_payload_to_sqlite(const UiNatsConfig& config, const QString& payload, const QString& subject) {
    ensure_database_runtime();
    const QString database_path = offline_database_connection(config);
    const QString connection_name = QStringLiteral("sensor_payloads_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase database;
    if (!configure_database_connection(database, database_path, connection_name)) {
        return false;
    }

    bool success = false;
    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS sensor_payloads (id %1, created_at TEXT NOT NULL, payload TEXT NOT NULL, subject TEXT, device_id TEXT, instrument_id TEXT, data_mode TEXT)").arg(create_table_id_column(database)))) {
            close_and_remove_database(database, connection_name);
            return false;
        }

        QString device_id;
        QString instrument_id;
        QString data_mode;
        QJsonParseError parse_error{};
        const auto document = QJsonDocument::fromJson(payload.toUtf8(), &parse_error);
        if (!parse_error.error && document.isObject()) {
            const auto object = document.object();
            device_id = object.value(QStringLiteral("devc")).toString();
            instrument_id = object.value(QStringLiteral("instr")).toString();
            const QStringList tokens = subject.split(QChar('.'));
            if (!tokens.isEmpty()) {
                data_mode = tokens.last();
            }
        }

        if (!query.prepare(QStringLiteral("INSERT INTO sensor_payloads (created_at, payload, subject, device_id, instrument_id, data_mode) VALUES (?, ?, ?, ?, ?, ?)"))) {
            close_and_remove_database(database, connection_name);
            return false;
        }

        const QString timestamp = format_ist_iso_timestamp(QDateTime::currentDateTime());
        query.addBindValue(timestamp);
        query.addBindValue(payload);
        query.addBindValue(subject);
        query.addBindValue(device_id);
        query.addBindValue(instrument_id);
        query.addBindValue(data_mode);
        success = query.exec();
    }

    close_and_remove_database(database, connection_name);
    return success;
}

bool buffer_to_sqlite(const UiNatsConfig& config, const QString& payload, const QString& subject) {
    ensure_database_runtime();
    const QString database_path = offline_database_connection(config);
    const QString connection_name = QStringLiteral("publisher_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase database;
    if (!configure_database_connection(database, database_path, connection_name)) {
        return false;
    }

    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS offline_events (id %1, created_at TEXT NOT NULL, payload TEXT NOT NULL, subject TEXT, device_id TEXT, instrument_id TEXT, data_mode TEXT)").arg(create_table_id_column(database)))) {
            close_and_remove_database(database, connection_name);
            return false;
        }
        query.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS offline_events_subject_payload_unique ON offline_events(subject, payload)"));

        QString device_id;
        QString instrument_id;
        QString data_mode;
        QJsonParseError parse_error{};
        const auto document = QJsonDocument::fromJson(payload.toUtf8(), &parse_error);
        if (!parse_error.error && document.isObject()) {
            const auto object = document.object();
            device_id = object.value(QStringLiteral("devc")).toString();
            instrument_id = object.value(QStringLiteral("instr")).toString();
            const QStringList tokens = subject.split(QChar('.'));
            if (!tokens.isEmpty()) {
                data_mode = tokens.last();
            }
        }

        if (!query.prepare(QStringLiteral("INSERT INTO offline_events (created_at, payload, subject, device_id, instrument_id, data_mode) VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT DO NOTHING"))) {
            close_and_remove_database(database, connection_name);
            return false;
        }

        const QString timestamp = format_ist_iso_timestamp(QDateTime::currentDateTime());
        query.addBindValue(timestamp);
        query.addBindValue(payload);
        query.addBindValue(subject);
        query.addBindValue(device_id);
        query.addBindValue(instrument_id);
        query.addBindValue(data_mode);
        if (!query.exec()) {
            close_and_remove_database(database, connection_name);
            return false;
        }

        if (config.retention_minutes > 0) {
            const QString cutoff = format_ist_iso_timestamp(QDateTime::currentDateTime().addSecs(-(config.retention_minutes * 60)));
            QSqlQuery prune_query(database);
            prune_query.prepare(QStringLiteral("DELETE FROM offline_events WHERE created_at < ?"));
            prune_query.addBindValue(cutoff);
            prune_query.exec();
        }
    }

    close_and_remove_database(database, connection_name);
    return true;
}

struct BufferedEvent {
    qint64 id {0};
    QString payload;
    QString subject;
};

QVector<BufferedEvent> load_buffered_events(const UiNatsConfig& config) {
    ensure_database_runtime();
    QVector<BufferedEvent> events;
    const QString connection_name = QStringLiteral("offline_replay_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase database;
    if (!configure_database_connection(database, offline_database_connection(config), connection_name)) {
        return events;
    }
    {
        QSqlQuery query(database);
        if (query.exec(QStringLiteral("SELECT id, payload, subject FROM offline_events ORDER BY id ASC"))) {
            while (query.next()) {
                events.push_back({query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString()});
            }
        }
    }
    close_and_remove_database(database, connection_name);
    return events;
}

bool delete_buffered_event(const UiNatsConfig& config, qint64 id) {
    ensure_database_runtime();
    const QString connection_name = QStringLiteral("offline_replay_delete_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase database;
    if (!configure_database_connection(database, offline_database_connection(config), connection_name)) {
        return false;
    }
    bool success = false;
    {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("DELETE FROM offline_events WHERE id = ?"));
        query.addBindValue(id);
        success = query.exec();
    }
    close_and_remove_database(database, connection_name);
    return success;
}
} // namespace

NatsPublisherService::NatsPublisherService(UiNatsConfig config, int queue_capacity, QObject* parent)
    : QObject(parent), config_(std::move(config)), queue_capacity_(queue_capacity), central_store_(config_.central_db_path) {
}

NatsPublisherService::~NatsPublisherService() {
    stop();
    if (drain_timer_ != nullptr) {
        drain_timer_->stop();
        drain_timer_->deleteLater();
    }
}

void NatsPublisherService::start() {
    if (running_) {
        return;
    }

    running_ = true;
    // This dashboard streams live controller readings and has no separate
    // capture-session control, so begin publishing as soon as it starts.
    capture_session_active_ = true;
    load_event_counters();
    if (config_.enable_central_persistence && !central_store_.initialize()) {
        UiFileLogger::warn(QStringLiteral("Unable to initialize central event store at %1").arg(config_.central_db_path), QStringLiteral("nats"));
    }
    UiFileLogger::info(QString("Starting NATS publisher for %1").arg(config_.url), QStringLiteral("nats"));
    initialize_timer();
    // The failed metric represents payloads still awaiting NATS delivery.
    failed_messages_ = static_cast<quint64>(load_buffered_events(config_).size());
    if (ensure_connected()) {
        replay_buffered_events();
    }
    emit_metrics();
}

void NatsPublisherService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    capture_session_active_ = false;
    capture_paused_ = false;
    if (drain_timer_ != nullptr) {
        drain_timer_->stop();
    }
    close_connection();
    emit_status(ConnectionState::disconnected);
}

void NatsPublisherService::set_capture_session_active(bool active) {
    capture_session_active_ = active;
    if (!capture_session_active_) {
        if (drain_timer_ != nullptr) {
            drain_timer_->stop();
        }
        process_queue();
        return;
    }

    if (drain_timer_ != nullptr && !drain_timer_->isActive() && !queue_.isEmpty()) {
        drain_timer_->start(0);
    }
}

void NatsPublisherService::set_capture_paused(bool paused) {
    capture_paused_ = paused;
    if (paused) {
        if (drain_timer_ != nullptr) {
            drain_timer_->stop();
        }
        return;
    }

    if (capture_session_active_ && drain_timer_ != nullptr && !drain_timer_->isActive() && !queue_.isEmpty()) {
        drain_timer_->start(0);
    }
}

void NatsPublisherService::enqueue_record(SensorRecord record) {
    enqueue_record_internal(std::move(record), false);
}

void NatsPublisherService::enqueue_manual_record(SensorRecord record) {
    enqueue_record_internal(std::move(record), true);
}

void NatsPublisherService::enqueue_record_internal(SensorRecord record, bool force_publish) {
    if (!running_) {
        return;
    }

    if (!force_publish && !record.publish_to_nats) {
        UiFileLogger::trace(QString("Skipping NATS publish for %1 due to validation gating.").arg(record.device_id),
                            QStringLiteral("nats"));
        return;
    }

    record.event_id = next_event_id(record.device_id);

    const QString payload = serialize_record(record);
    const QString subject = subject_for_record(record);
    const bool is_event = record.data_mode == QLatin1String("event") || !record.event_code.isEmpty();
    const bool is_error = !record.error_code.trimmed().isEmpty() ||
                          record.device_health_status.trimmed().compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0;
    if (!force_publish && !is_event && !is_error && (!capture_session_active_ || capture_paused_)) {
        return;
    }
    if (is_event) {
        UiFileLogger::trace(
            QString("Queued event publish subject=%1 payload=%2").arg(subject, payload),
            QStringLiteral("nats"));
        if (record.event_code.compare(QStringLiteral("Stop"), Qt::CaseInsensitive) == 0) {
            UiFileLogger::info(
                QString("Stop event queued for publish: subject=%1 device=%2 instrument=%3")
                    .arg(subject, record.device_id, record.instrument_id),
                QStringLiteral("nats"));
        }
    }
    persist_payload_to_central_store(payload, subject, record.device_id, record.instrument_id,
                                     record.data_mode == QLatin1String("event") || !record.event_code.isEmpty() ? QStringLiteral("event") : QStringLiteral("sensor"),
                                     dedupe_key_for_record(record));

    // Log the data in new format
    UiFileLogger::data(payload);

    if (queue_.size() >= queue_capacity_) {
        if (buffer_locally(serialize_record(record), subject_for_record(record))) {
            ++failed_messages_;
        }
        emit_metrics();
        emit alert_raised(QString("NATS queue full. Buffered record locally for %1.").arg(record.device_id));
        UiFileLogger::warn(QString("NATS queue full; buffered record for %1").arg(record.device_id), QStringLiteral("nats"));
        return;
    }

    queue_.enqueue({std::move(record), force_publish});
    if (drain_timer_ != nullptr && !drain_timer_->isActive()) {
        drain_timer_->start(0);
    }
}

void NatsPublisherService::process_queue() {
    if (!running_) {
        return;
    }

    if (!replay_buffered_events()) {
        return;
    }

    while (!queue_.isEmpty()) {
        const auto& next_item = queue_.head();
        const bool is_event = next_item.record.data_mode == QLatin1String("event") || !next_item.record.event_code.isEmpty();
        const bool is_error = !next_item.record.error_code.trimmed().isEmpty() ||
                              next_item.record.device_health_status.trimmed().compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0;
        if (!next_item.force_publish && !is_event && !is_error && !capture_session_active_) {
            queue_.dequeue();
            continue;
        }
        if (!next_item.force_publish && !is_event && !is_error && capture_paused_) {
            return;
        }

        const auto item = queue_.dequeue();
        const auto& record = item.record;
        const auto payload = serialize_record(record);
        const auto subject = subject_for_record(record);
        if (!publish_payload(payload, subject)) {
            ++retry_count_;
            if (buffer_locally(payload, subject)) {
                ++failed_messages_;
            }
            emit_metrics();
            emit alert_raised(QString("Failed to publish %1 to NATS; buffered locally.").arg(record.device_id));
            UiFileLogger::error(QString("Publish failed for %1; buffered locally.").arg(record.device_id), QStringLiteral("nats"));
            if (drain_timer_ != nullptr) {
                drain_timer_->start(qMin(config_.retry_backoff_ms * static_cast<int>(++consecutive_failures_), 30000));
            }
            return;
        }

        consecutive_failures_ = 0;
        if (!persist_payload_to_sqlite(config_, payload, subject)) {
            UiFileLogger::warn(QStringLiteral("NATS-published payload could not be persisted to sensor_payloads."),
                               QStringLiteral("nats"));
        }
        ++total_sent_;
        if (is_event) {
            ++event_sent_;
        } else if (record.data_mode == QLatin1String("continuous")) {
            ++continuous_sent_;
        }
        last_sent_ = record.timestamp;
        emit_metrics();
        if (record.data_mode == QLatin1String("event") &&
            record.event_code.compare(QStringLiteral("Stop"), Qt::CaseInsensitive) == 0) {
            UiFileLogger::info(
                QString("Stop event published to NATS: subject=%1 device=%2 instrument=%3")
                    .arg(subject, record.device_id, record.instrument_id),
                QStringLiteral("nats"));
        }
        UiFileLogger::trace(QString("Published sensor payload for %1").arg(record.device_id), QStringLiteral("nats"));
    }
}

QString NatsPublisherService::serialize_record(const SensorRecord& record) const {
    QJsonObject object;
    object.insert(QStringLiteral("id"), record.event_id);
    object.insert(QStringLiteral("ts"), format_ist_iso_timestamp(record.timestamp));
    object.insert(QStringLiteral("devc"), record.device_id);
    object.insert(QStringLiteral("instr"), record.instrument_id.isEmpty() ? record.device_id : record.instrument_id);

    const QString raw_error_code = record.error_code.trimmed();
    const bool has_error_code = !raw_error_code.isEmpty();
    const bool status_requests_error = record.device_health_status.trimmed().compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0;
    const bool is_error = has_error_code || status_requests_error;
    const bool is_event = record.data_mode == QLatin1String("event") || !record.event_code.isEmpty();
    if (is_event) {
        object.insert(QStringLiteral("opr"), record.operator_id);
        object.insert(QStringLiteral("po"), record.order_id);
        object.insert(QStringLiteral("lot"), record.batch_id.isEmpty() ? QJsonValue::Null : QJsonValue(record.batch_id));
        object.insert(QStringLiteral("opid"), record.operation_id);
        object.insert(QStringLiteral("evt"), record.event_code.isEmpty()
                                               ? QJsonValue(QStringLiteral("event"))
                                               : contract_event_code(record.event_code));
        object.insert(QStringLiteral("d"), QJsonArray());
    } else {
        if (record.data_mode == QLatin1String("continuous")) {
            object.insert(QStringLiteral("opr"), record.operator_id);
            object.insert(QStringLiteral("po"), record.order_id);
            object.insert(QStringLiteral("lot"), record.batch_id.isEmpty() ? QJsonValue::Null : QJsonValue(record.batch_id));
            object.insert(QStringLiteral("opid"), record.operation_id);
        }

        QJsonArray readings;
        if (!is_error) {
            const bool has_device_parameter_mapping = record.data_mode == QLatin1String("continuous") &&
                                                      !record.device_parameter_ids.isEmpty();
            const int output_count = has_device_parameter_mapping
                                         ? qMax(record.readings.size(), record.device_parameter_ids.size())
                                         : record.readings.size();
            for (int index = 0; index < output_count; ++index) {
                if (record.readings.isEmpty()) break;
                const int reading_index = qMin(index, record.readings.size() - 1);
                const auto& reading = record.readings.at(reading_index);
                QJsonObject r;
                const QString pid = has_device_parameter_mapping && index < record.device_parameter_ids.size()
                                        ? record.device_parameter_ids.at(index)
                                        : reading.parameter_id;
                r.insert(QStringLiteral("pid"), pid);
                r.insert(QStringLiteral("val"), reading.value);
                r.insert(QStringLiteral("uom"), reading.unit);
                if (record.data_mode == QLatin1String("continuous")) {
                    const QString ispid = index < record.selected_parameter_ids.size()
                                              ? record.selected_parameter_ids.at(index)
                                              : reading.ispid;
                    r.insert(QStringLiteral("ispid"), ispid);
                }
                readings.append(r);
            }
        }
        object.insert(QStringLiteral("d"), readings);
    }
    object.insert(QStringLiteral("stts"), is_error ? QStringLiteral("error") : QStringLiteral("ok"));

    if (is_error) {
        const QString error_code = normalize_contract_error_code(raw_error_code);
        const QString error_message = record.error_message.trimmed().isEmpty()
                                          ? default_error_message_for_code(error_code)
                                          : record.error_message.trimmed();
        QJsonObject error;
        error.insert(QStringLiteral("code"), error_code);
        error.insert(QStringLiteral("msg"), error_message);
        object.insert(QStringLiteral("err"), error);
    } else {
        object.insert(QStringLiteral("err"), QJsonValue::Null);
    }

    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void NatsPublisherService::enqueue_health_status(DeviceStatus status) {
    if (!running_) {
        return;
    }

    const qint64 event_id = next_event_id(status.device_id);
    const QString payload = serialize_health_status(status, event_id);
    const QString subject = subject_for_health_status(status);
    persist_payload_to_central_store(payload, subject, status.device_id, status.instrument_id, QStringLiteral("health"), QStringLiteral("%1:%2").arg(status.device_id).arg(event_id));
    if (!publish_payload(payload, subject)) {
        ++retry_count_;
        if (buffer_locally(payload, subject)) {
            ++failed_messages_;
        }
        emit_metrics();
        UiFileLogger::warn(QString("Buffered health status for %1 after NATS publish failure.").arg(status.device_id),
                           QStringLiteral("nats"));
        return;
    }

    if (!persist_payload_to_sqlite(config_, payload, subject)) {
        UiFileLogger::warn(QStringLiteral("NATS-published health payload could not be persisted to sensor_payloads."),
                           QStringLiteral("nats"));
    }
    ++total_sent_;
    ++health_sent_;
    last_sent_ = QDateTime::currentDateTimeUtc();
    emit_metrics();
}

QString NatsPublisherService::serialize_health_status(const DeviceStatus& status, qint64 event_id) const {
    QJsonObject object;
    object.insert(QStringLiteral("id"), static_cast<qint64>(event_id));
    object.insert(QStringLiteral("ts"), format_ist_iso_timestamp(QDateTime::currentDateTime()));
    object.insert(QStringLiteral("devc"), status.device_id);
    object.insert(QStringLiteral("instr"), status.instrument_id.isEmpty() ? status.device_id : status.instrument_id);

    int health_code = 0;
    if (status.status == ConnectionState::connected) {
        health_code = 1;
    } else if (status.status == ConnectionState::reconnecting) {
        health_code = 2;
    }
    object.insert(QStringLiteral("stts"), health_code);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString NatsPublisherService::subject_for_record(const SensorRecord& record) const {
    const QString instrument = record.instrument_id.isEmpty() ? record.device_id : record.instrument_id;
    const bool is_event = record.data_mode == QLatin1String("event") || !record.event_code.isEmpty();
    QString subject_type = record.subject_type.trimmed();
    if (is_event) {
        subject_type = QStringLiteral("event");
    } else if (subject_type.isEmpty()) {
        subject_type = record.data_mode == QLatin1String("continuous") ? QStringLiteral("continuous")
                      : QStringLiteral("discrete");
    }
    const QString plant_id = record.plant_id.trimmed().isEmpty() ? config_.plant : record.plant_id.trimmed();
    const QString work_center = record.work_center.trimmed().isEmpty() ? config_.work_center : record.work_center.trimmed();
    return QStringLiteral("qs.%1.%2.%3.%4")
        .arg(plant_id.isEmpty() ? QStringLiteral("plant") : plant_id)
        .arg(work_center.isEmpty() ? QStringLiteral("workcenter") : work_center)
        .arg(instrument.isEmpty() ? record.device_id : instrument)
        .arg(subject_type.isEmpty() ? QStringLiteral("data") : subject_type);
}

QString NatsPublisherService::subject_for_health_status(const DeviceStatus& status) const {
    const QString instrument = status.instrument_id.isEmpty() ? status.device_id : status.instrument_id;
    return QStringLiteral("qs.%1.%2.%3.health").arg(config_.plant, config_.work_center, instrument);
}

bool NatsPublisherService::ensure_connected() {
#ifdef HAVE_NATS
    if (connection_handle_ != nullptr) {
        return true;
    }

    // Prepare TLS options if certificate files are present under certs/publisher
    natsOptions* opts = nullptr;
    natsStatus status = natsOptions_Create(&opts);
    if (status != NATS_OK) {
        connection_handle_ = nullptr;
        emit_status(ConnectionState::disconnected);
        UiFileLogger::error(QString("Unable to create NATS options: %1").arg(QString::fromUtf8(natsStatus_GetText(status))),
                           QStringLiteral("nats"));
        return false;
    }

    // Set the URL and connect timeout
    natsOptions_SetURL(opts, config_.url.toUtf8().constData());
    natsOptions_SetTimeout(opts, config_.connect_timeout_ms);

    // Enable TLS when requested or when the URL begins with tls://
    const bool tls_required = config_.tls_enabled || config_.url.startsWith(QStringLiteral("tls://"));
    if (tls_required) {
        natsOptions_SetSecure(opts, true);
    }

    // TLS authenticates the server; NKey signs the client challenge.
    const QString certPath = config_.client_cert;
    const QString keyPath = config_.client_key;
    const QString caPath = config_.ca_cert;
    const QString nkeyPublic = config_.nkey_public;
    const QString nkeySeedFile = !config_.nkey_seed_file.isEmpty()
            ? config_.nkey_seed_file
            : QDir(QCoreApplication::applicationDirPath()).filePath("certs/nats/user.seed");

    if (!certPath.isEmpty() && !keyPath.isEmpty() && QFile::exists(certPath) && QFile::exists(keyPath)) {
        status = natsOptions_LoadCertificatesChain(opts, certPath.toUtf8().constData(), keyPath.toUtf8().constData());
        if (status != NATS_OK) {
            natsOptions_Destroy(opts);
            connection_handle_ = nullptr;
            emit_status(ConnectionState::disconnected);
            UiFileLogger::error(
                QString("Unable to load client certificates: %1").arg(QString::fromUtf8(natsStatus_GetText(status))),
                QStringLiteral("nats"));
            return false;
        }
    }

    if (!caPath.isEmpty() && !QFile::exists(caPath)) {
        natsOptions_Destroy(opts);
        connection_handle_ = nullptr;
        emit_status(ConnectionState::disconnected);
        UiFileLogger::error(QString("NATS CA certificate file does not exist: %1").arg(caPath), QStringLiteral("nats"));
        return false;
    }

    if (!caPath.isEmpty()) {
        status = natsOptions_LoadCATrustedCertificates(opts, caPath.toUtf8().constData());
        if (status != NATS_OK) {
            natsOptions_Destroy(opts);
            connection_handle_ = nullptr;
            emit_status(ConnectionState::disconnected);
            UiFileLogger::error(
                QString("Unable to load CA certificate: %1").arg(QString::fromUtf8(natsStatus_GetText(status))),
                QStringLiteral("nats"));
            return false;
        }
    }

    if (!tls_required) {
        natsOptions_Destroy(opts);
        connection_handle_ = nullptr;
        emit_status(ConnectionState::disconnected);
        UiFileLogger::error(QStringLiteral("NATS TLS is required for NKey authentication."), QStringLiteral("nats"));
        return false;
    }

    if (!nkeyPublic.isEmpty()) {
        if (nkeySeedFile.isEmpty() || !QFile::exists(nkeySeedFile)) {
            natsOptions_Destroy(opts);
            connection_handle_ = nullptr;
            emit_status(ConnectionState::disconnected);
            UiFileLogger::error(
                QString("NATS NKey authentication requires a readable seed file: %1").arg(nkeySeedFile),
                QStringLiteral("nats"));
            return false;
        }

        status = natsOptions_SetNKeyFromSeed(opts, nkeyPublic.toUtf8().constData(), nkeySeedFile.toUtf8().constData());
        if (status != NATS_OK) {
            natsOptions_Destroy(opts);
            connection_handle_ = nullptr;
            emit_status(ConnectionState::disconnected);
            UiFileLogger::error(
                QString("Unable to configure NKey authentication: %1").arg(QString::fromUtf8(natsStatus_GetText(status))),
                QStringLiteral("nats"));
            return false;
        }
    }

    if (tls_required) {
        UiFileLogger::info(QString("Enforcing TLS for NATS connection %1 with minimum version %2").arg(config_.url, config_.tls_min_protocol), QStringLiteral("nats"));
    }

    natsConnection* connection = nullptr;
    status = natsConnection_Connect(&connection, opts);
    natsOptions_Destroy(opts);

    if (status != NATS_OK) {
        connection_handle_ = nullptr;
        emit_status(ConnectionState::disconnected);
        const char* detail = nats_GetLastError(nullptr);
        const QString detail_text = detail == nullptr ? QString() : QString::fromUtf8(detail).trimmed();
        UiFileLogger::error(
            QString("Unable to connect to NATS %1: %2%3")
                .arg(config_.url,
                     QString::fromUtf8(natsStatus_GetText(status)),
                     detail_text.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(detail_text)),
            QStringLiteral("nats"));
        return false;
    }

    connection_handle_ = connection;
    if (config_.jetstream_enabled) {
        jsOptions options;
        jsOptions_Init(&options);
        options.Wait = config_.connect_timeout_ms;
        jsCtx* context = nullptr;
        status = natsConnection_JetStream(&context, connection, &options);
        if (status != NATS_OK) {
            UiFileLogger::warn(QString("JetStream unavailable for %1: %2. Falling back to plain NATS.")
                                   .arg(config_.url, QString::fromUtf8(natsStatus_GetText(status))),
                               QStringLiteral("nats"));
            config_.jetstream_enabled = false;
        } else {
            jetstream_context_ = context;
            if (!ensure_jetstream_stream()) {
                UiFileLogger::warn(QString("JetStream stream %1 unavailable; falling back to plain NATS.")
                                       .arg(config_.jetstream_stream),
                                   QStringLiteral("nats"));
                jsCtx_Destroy(context);
                jetstream_context_ = nullptr;
                config_.jetstream_enabled = false;
            }
        }
    }
    emit_status(ConnectionState::connected);
    return true;
#else
    emit_status(ConnectionState::disconnected);
    UiFileLogger::warn(QString("nats.c not available; publisher running in buffer-only mode for %1").arg(config_.url),
                       QStringLiteral("nats"));
    return false;
#endif
}

bool NatsPublisherService::publish_payload(const QString& payload, const QString& subject) {
#ifdef HAVE_NATS
    UiFileLogger::trace(
        QString("Publishing payload to subject %1: %2").arg(subject, payload),
        QStringLiteral("nats"));
    if (!ensure_connected()) {
        return false;
    }

    auto* connection = static_cast<natsConnection*>(connection_handle_);
    const auto started = QDateTime::currentMSecsSinceEpoch();
    const auto payload_bytes = payload.toUtf8();
    const auto subject_bytes = subject.toUtf8();
    const QString message_id = message_id_from_payload(payload);
    const auto message_id_bytes = message_id.toUtf8();
    natsStatus status = NATS_OK;

    natsMsg* msg = nullptr;
    status = natsMsg_Create(&msg, subject_bytes.constData(), nullptr, payload_bytes.constData(), payload_bytes.size());
    if (status == NATS_OK) {
        status = natsMsgHeader_Set(msg, "Nats-Msg-Id", message_id_bytes.constData());
    }

    if (status == NATS_OK) {
        if (config_.jetstream_enabled) {
            jsPubOptions options;
            jsPubOptions_Init(&options);
            options.MaxWait = config_.connect_timeout_ms;
            options.MsgId = message_id_bytes.constData();
            jsPubAck* ack = nullptr;
            jsErrCode error_code = static_cast<jsErrCode>(0);
            status = js_PublishMsg(&ack, static_cast<jsCtx*>(jetstream_context_), msg, &options, &error_code);
            jsPubAck_Destroy(ack);
        } else {
            status = natsConnection_PublishMsg(connection, msg);
        }
    }

    natsMsg_Destroy(msg);

    if (status != NATS_OK) {
        close_connection();
        emit_status(ConnectionState::reconnecting);
        return false;
    }

    if (!config_.jetstream_enabled && natsConnection_FlushTimeout(connection, config_.connect_timeout_ms) != NATS_OK) {
        close_connection();
        emit_status(ConnectionState::reconnecting);
        return false;
    }
    emit_status(ConnectionState::connected, static_cast<int>(QDateTime::currentMSecsSinceEpoch() - started));
    return true;
#else
    Q_UNUSED(payload);
    Q_UNUSED(subject);
    return false;
#endif
}

bool NatsPublisherService::ensure_jetstream_stream() {
#ifdef HAVE_NATS
    if (!config_.jetstream_enabled || jetstream_context_ == nullptr) {
        return true;
    }
    auto* context = static_cast<jsCtx*>(jetstream_context_);
    jsStreamInfo* info = nullptr;
    jsErrCode error_code = static_cast<jsErrCode>(0);
    natsStatus status = js_GetStreamInfo(&info, context, config_.jetstream_stream.toUtf8().constData(), nullptr, &error_code);
    if (status == NATS_OK) {
        jsStreamInfo_Destroy(info);
        return true;
    }
    if (!config_.jetstream_auto_provision) {
        UiFileLogger::error(QStringLiteral("Required JetStream stream %1 is unavailable.").arg(config_.jetstream_stream), QStringLiteral("nats"));
        return false;
    }
    const QByteArray stream_name = config_.jetstream_stream.toUtf8();
    const QByteArray subject_filter = QStringLiteral("qs.%1.%2.>").arg(config_.plant, config_.work_center).toUtf8();
    const char* subjects[] = {subject_filter.constData()};
    jsStreamConfig stream;
    jsStreamConfig_Init(&stream);
    stream.Name = stream_name.constData();
    stream.Subjects = subjects;
    stream.SubjectsLen = 1;
    stream.Storage = js_FileStorage;
    stream.MaxAge = static_cast<int64_t>(config_.retention_minutes) * 60 * 1000000000LL;
    status = js_AddStream(&info, context, &stream, nullptr, &error_code);
    if (status == NATS_OK) {
        jsStreamInfo_Destroy(info);
        return true;
    }
    UiFileLogger::error(QStringLiteral("Unable to provision JetStream stream %1: %2")
                            .arg(config_.jetstream_stream, QString::fromUtf8(natsStatus_GetText(status))), QStringLiteral("nats"));
    return false;
#else
    return false;
#endif
}

bool NatsPublisherService::replay_buffered_events() {
    const auto buffered_events = load_buffered_events(config_);
    for (const auto& event : buffered_events) {
        if (!publish_payload(event.payload, event.subject)) {
            return false;
        }
        if (!persist_payload_to_sqlite(config_, event.payload, event.subject)) {
            UiFileLogger::warn(QStringLiteral("Published buffered payload could not be persisted to sensor_payloads."),
                               QStringLiteral("nats"));
            return false;
        }
        if (!delete_buffered_event(config_, event.id)) {
            UiFileLogger::warn(QStringLiteral("Published buffered event could not be removed from the offline outbox."),
                               QStringLiteral("nats"));
            return false;
        }
        if (failed_messages_ > 0) {
            --failed_messages_;
        }
        ++total_sent_;
    }
    if (!buffered_events.isEmpty()) {
        last_sent_ = QDateTime::currentDateTimeUtc();
        emit_metrics();
        UiFileLogger::info(QStringLiteral("Replayed %1 buffered NATS event(s).").arg(buffered_events.size()),
                           QStringLiteral("nats"));
    }
    return true;
}

bool NatsPublisherService::buffer_locally(const QString& payload, const QString& subject) const {
    const QString database_path = offline_database_connection(config_);
    if (!is_postgres_connection(database_path)) {
        QFileInfo database_info(database_path);
        QDir().mkpath(database_info.absolutePath());
    }

    if (!buffer_to_sqlite(config_, payload, subject)) {
        UiFileLogger::warn(QStringLiteral("Unable to insert buffered payload into SQLite store."), QStringLiteral("nats"));
        return false;
    }
    return true;
}

qint64 NatsPublisherService::next_event_id(const QString& device_id) {
    Q_UNUSED(device_id);
    // Millisecond timestamp in the high digits, with a monotonic increment in
    // the low digits. Keep one counter across devices so IDs are globally unique.
    constexpr qint64 increments_per_millisecond = 1000;
    const qint64 timestamp_id = QDateTime::currentMSecsSinceEpoch() * increments_per_millisecond;
    const qint64 previous_id = event_counters_.value(QStringLiteral("global"), 0);
    const qint64 value = qMax(timestamp_id, previous_id + 1);
    event_counters_.insert(QStringLiteral("global"), value);
    save_event_counters();
    return value;
}

QString NatsPublisherService::dedupe_key_for_record(const SensorRecord& record) const {
    return QStringLiteral("%1:%2:%3").arg(record.device_id, record.instrument_id.isEmpty() ? record.device_id : record.instrument_id).arg(record.event_id);
}

void NatsPublisherService::persist_payload_to_central_store(const QString& payload,
                                                            const QString& subject,
                                                            const QString& device_id,
                                                            const QString& instrument_id,
                                                            const QString& event_type,
                                                            const QString& dedupe_key) {
    if (!config_.enable_central_persistence) {
        return;
    }

    if (!central_store_.persist_payload(payload, subject, device_id, instrument_id, event_type, dedupe_key)) {
        UiFileLogger::warn(QStringLiteral("Unable to persist payload into central event store."), QStringLiteral("nats"));
    }
}

void NatsPublisherService::load_event_counters() {
    event_counters_.clear();

    QFile file(event_counter_file());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return;
    }

    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        event_counters_.insert(it.key(), static_cast<qint64>(it.value().toDouble()));
    }
}

void NatsPublisherService::save_event_counters() const {
    const QString path = event_counter_file();
    QFileInfo file_info(path);
    QDir().mkpath(file_info.absolutePath());

    QJsonObject object;
    for (auto it = event_counters_.constBegin(); it != event_counters_.constEnd(); ++it) {
        object.insert(it.key(), static_cast<qint64>(it.value()));
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString NatsPublisherService::event_counter_file() const {
    QFileInfo buffer_info(config_.local_buffer_file);
    const QDir directory = buffer_info.absoluteDir();
    return directory.filePath(QStringLiteral("publisher_event_counters.json"));
}

void NatsPublisherService::emit_status(ConnectionState state, int latency_ms) {
    NatsConnectionStatus status;
    status.status = state;
    status.url = config_.url;
    status.retries = static_cast<int>(consecutive_failures_);
    status.latency_ms = latency_ms;
    status.updated_at = QDateTime::currentDateTimeUtc();
    emit status_changed(status);
}

void NatsPublisherService::emit_metrics() {
    PublishMetrics metrics;
    metrics.total_sent = total_sent_;
    metrics.continuous_sent = continuous_sent_;
    metrics.event_sent = event_sent_;
    metrics.health_sent = health_sent_;
    metrics.failed = failed_messages_;
    metrics.retries = retry_count_;
    metrics.last_sent = last_sent_;
    emit metrics_changed(metrics);
}

void NatsPublisherService::close_connection() {
#ifdef HAVE_NATS
    auto* context = static_cast<jsCtx*>(jetstream_context_);
    if (context != nullptr) {
        jsCtx_Destroy(context);
        jetstream_context_ = nullptr;
    }
    auto* connection = static_cast<natsConnection*>(connection_handle_);
    if (connection != nullptr) {
        natsConnection_Destroy(connection);
        connection_handle_ = nullptr;
    }
#endif
}

void NatsPublisherService::enqueue_event(const QString& event_code,
                                          const QString& device_id,
                                          const QString& instrument_id,
                                          const QString& operator_id,
                                          const QString& po,
                                          const QString& lot,
                                          const QString& operation_id) {
    if (!running_) {
        return;
    }

    SensorRecord record;
    record.device_id = device_id;
    record.instrument_id = instrument_id;
    record.data_mode = QStringLiteral("event");
    record.timestamp = QDateTime::currentDateTimeUtc();
    record.operator_id = operator_id;
    record.order_id = po;
    record.batch_id = lot;
    record.operation_id = operation_id;
    record.event_code = event_code;
    enqueue_record(record);
}

void NatsPublisherService::initialize_timer() {
    if (drain_timer_ != nullptr) {
        return;
    }
    
    drain_timer_ = new QTimer(this);
    drain_timer_->setSingleShot(true);
    connect(drain_timer_, &QTimer::timeout, this, &NatsPublisherService::process_queue);
    UiFileLogger::trace(QStringLiteral("Initialized drain timer in publisher service thread"), QStringLiteral("nats"));
}

}  // namespace data_extraction::dashboard
