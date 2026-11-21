#include "modbus_device_worker.hpp"

#include "ui_file_logger.hpp"

#include <QAbstractSocket>
#include <QDateTime>
#include <QRegularExpression>
#include <QThread>
#include <QTcpSocket>

#include <utility>

namespace data_extraction::dashboard {
namespace {

constexpr char kStx = 0x02;
constexpr char kEtx = 0x03;

QString trimmed_ascii(const QByteArray& payload) {
    return QString::fromLatin1(payload).trimmed();
}

QString sanitize_numeric_payload(const QString& payload) {
    static const QRegularExpression non_numeric_regex(QStringLiteral("[^0-9.+\\-]"));
    return payload.trimmed().remove(non_numeric_regex);
}

bool should_swap_temperature_and_humidity(const QString& first_token, const QString& second_token) {
    // Some controllers send temperature first and humidity second with marker characters.
    return first_token.startsWith('*') && second_token.endsWith('#');
}

}  // namespace

ModbusDeviceWorker::ModbusDeviceWorker(UiDeviceConfig device_config, int request_timeout_ms, QObject* parent)
    : QObject(parent),
      device_config_(std::move(device_config)),
      request_timeout_ms_(request_timeout_ms) {
    reconnect_timer_ = new QTimer(this);
    inactivity_timer_ = new QTimer(this);
    reconnect_timer_->setSingleShot(true);
    inactivity_timer_->setSingleShot(true);

    connect(reconnect_timer_, &QTimer::timeout, this, &ModbusDeviceWorker::connect_device);
    connect(inactivity_timer_, &QTimer::timeout, this, &ModbusDeviceWorker::on_request_timeout);
}

ModbusDeviceWorker::~ModbusDeviceWorker() {
    stop();
}

void ModbusDeviceWorker::start() {
    if (running_) {
        return;
    }

    running_ = true;
    emit worker_trace(QString("Starting controller worker for %1 (%2:%3)")
                          .arg(device_config_.id, device_config_.ip)
                          .arg(device_config_.port));
    UiFileLogger::info(
        QString("Starting TCP ASCII worker for %1 (%2:%3), expected_frame='STX humidity,temp ETX', inactivity_timeout_ms=%4")
            .arg(device_config_.id, device_config_.ip)
            .arg(device_config_.port)
            .arg(request_timeout_ms_),
        QStringLiteral("controller"));

    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected, this, &ModbusDeviceWorker::on_connected);
    connect(socket_, &QTcpSocket::disconnected, this, &ModbusDeviceWorker::on_disconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &ModbusDeviceWorker::on_ready_read);
    connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError socket_error) {
        on_error_occurred(static_cast<int>(socket_error));
    });

    connect_device();
}

void ModbusDeviceWorker::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    reconnect_timer_->stop();
    inactivity_timer_->stop();
    cleanup_socket();
    emit_status(ConnectionState::disconnected);
    UiFileLogger::warn(QString("Stopped controller worker for %1").arg(device_config_.id), QStringLiteral("controller"));
}

void ModbusDeviceWorker::connect_device() {
    if (!running_ || socket_ == nullptr) {
        return;
    }

    emit_status(ConnectionState::reconnecting);
    UiFileLogger::debug(
        QString("Connecting TCP ASCII controller %1 to %2:%3").arg(device_config_.id, device_config_.ip).arg(device_config_.port),
        QStringLiteral("controller"));

    if (socket_->state() == QAbstractSocket::ConnectedState ||
        socket_->state() == QAbstractSocket::ConnectingState) {
        return;
    }

    read_buffer_.clear();
    socket_->connectToHost(device_config_.ip, device_config_.port);
}

void ModbusDeviceWorker::on_connected() {
    reconnect_attempts_ = 0;
    current_backoff_ms_ = 1000;
    suppress_disconnect_reconnect_ = false;
    read_buffer_.clear();
    inactivity_timer_->start(request_timeout_ms_);
    emit_status(ConnectionState::connected, QDateTime::currentDateTimeUtc());
    UiFileLogger::info(
        QString("Connected to TCP ASCII controller %1 at %2:%3")
            .arg(device_config_.id, device_config_.ip)
            .arg(device_config_.port),
        QStringLiteral("controller"));
}

void ModbusDeviceWorker::on_disconnected() {
    inactivity_timer_->stop();
    read_buffer_.clear();
    emit_status(ConnectionState::disconnected);
    if (running_ && !suppress_disconnect_reconnect_) {
        schedule_reconnect(QStringLiteral("Controller disconnected."));
    }
    suppress_disconnect_reconnect_ = false;
}

void ModbusDeviceWorker::on_ready_read() {
    if (socket_ == nullptr) {
        return;
    }

    const auto bytes = socket_->readAll();
    if (bytes.isEmpty()) {
        return;
    }

    read_buffer_.append(bytes);
    inactivity_timer_->start(request_timeout_ms_);
    UiFileLogger::trace(
        QString("Received %1 bytes from %2: %3")
            .arg(bytes.size())
            .arg(device_config_.id, QString::fromLatin1(bytes.toHex(' '))),
        QStringLiteral("controller"));
    process_frames();
}

void ModbusDeviceWorker::on_error_occurred(int socket_error) {
    Q_UNUSED(socket_error);
    if (socket_ == nullptr || socket_->error() == QAbstractSocket::UnknownSocketError) {
        return;
    }

    const QString message =
        QString("Controller %1 TCP error on %2:%3: %4")
            .arg(device_config_.id, device_config_.ip)
            .arg(device_config_.port)
            .arg(socket_->errorString());
    UiFileLogger::error(message, QStringLiteral("controller"));
    emit worker_alert(message);
}

void ModbusDeviceWorker::on_request_timeout() {
    if (!running_) {
        return;
    }

    const QString message = QString(
        "Timed out waiting for ASCII payload from %1 at %2:%3 after %4 ms. Expected frame format: STX humidity,temp ETX.")
                                .arg(device_config_.id, device_config_.ip)
                                .arg(device_config_.port)
                                .arg(request_timeout_ms_);
    UiFileLogger::warn(message, QStringLiteral("controller"));
    emit worker_alert(message);
    suppress_disconnect_reconnect_ = true;
    cleanup_socket();
    schedule_reconnect(message);
}

DeviceStatus ModbusDeviceWorker::build_status(ConnectionState state, const QDateTime& last_seen) const {
    DeviceStatus status;
    status.device_id = device_config_.id;
    status.ip = device_config_.ip;
    status.port = device_config_.port;
    status.status = state;
    status.last_seen = last_seen;
    return status;
}

void ModbusDeviceWorker::process_frames() {
    while (true) {
        const int stx_index = read_buffer_.indexOf(kStx);
        if (stx_index < 0) {
            if (read_buffer_.size() > 4096) {
                UiFileLogger::warn(
                    QString("Discarding oversized ASCII buffer for %1 with no STX marker.").arg(device_config_.id),
                    QStringLiteral("controller"));
                read_buffer_.clear();
            }
            return;
        }

        if (stx_index > 0) {
            read_buffer_.remove(0, stx_index);
        }

        const int etx_index = read_buffer_.indexOf(kEtx, 1);
        if (etx_index < 0) {
            return;
        }

        const QByteArray frame = read_buffer_.mid(1, etx_index - 1);
        read_buffer_.remove(0, etx_index + 1);
        if (!handle_frame(frame)) {
            const QString message = QString("Failed to parse ASCII frame from %1: %2")
                                        .arg(device_config_.id, trimmed_ascii(frame));
            UiFileLogger::warn(message, QStringLiteral("controller"));
            emit worker_alert(message);
        }
    }
}

bool ModbusDeviceWorker::handle_frame(const QByteArray& payload) {
    const QString frame_text = trimmed_ascii(payload);
    const auto parts = frame_text.split(',', Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        UiFileLogger::warn(
            QString("Unexpected ASCII payload from %1. Expected 'humidity,temp', received '%2'.")
                .arg(device_config_.id, frame_text),
            QStringLiteral("controller"));

        // Create error record
        SensorRecord record;
        record.device_id = device_config_.id;
        record.timestamp = QDateTime::currentDateTimeUtc();
        record.readings.clear(); // empty
        record.device_health_status = "error";
        record.error_code = "ERR_PARSE_FAIL";
        record.error_message = QString("Unexpected payload format: expected 2 parts, got %1").arg(parts.size());
        record.order_id = "";
        record.batch_id = "";

        emit sensor_data_received(record);
        emit_status(ConnectionState::connected, record.timestamp);
        return false;
    }

    const QString raw_humidity = parts.at(0).trimmed();
    const QString raw_temperature = parts.at(1).trimmed();
    const QString cleaned_humidity = sanitize_numeric_payload(raw_humidity);
    const QString cleaned_temperature = sanitize_numeric_payload(raw_temperature);

    bool humidity_ok = false;
    bool temperature_ok = false;
    const double humidity_value = cleaned_humidity.toDouble(&humidity_ok);
    const double temperature_value = cleaned_temperature.toDouble(&temperature_ok);
    if (!humidity_ok || !temperature_ok) {
        UiFileLogger::warn(
            QString("Non-numeric ASCII payload from %1. humidity='%2' temp='%3'.")
                .arg(device_config_.id, raw_humidity, raw_temperature),
            QStringLiteral("controller"));

        // Create error record
        SensorRecord record;
        record.device_id = device_config_.id;
        record.timestamp = QDateTime::currentDateTimeUtc();
        record.readings.clear(); // empty
        record.device_health_status = "error";
        record.error_code = "ERR_NON_NUMERIC";
        record.error_message = "Non-numeric values in payload";
        record.order_id = "";
        record.batch_id = "";

        emit sensor_data_received(record);
        emit_status(ConnectionState::connected, record.timestamp);
        return false;
    }

    const bool values_reversed = should_swap_temperature_and_humidity(raw_humidity, raw_temperature);
    const double humidity = values_reversed ? temperature_value : humidity_value;
    const double temperature = values_reversed ? humidity_value : temperature_value;

    SensorRecord record;
    record.device_id = device_config_.id;
    record.data_mode = QStringLiteral("continuous");
    record.timestamp = QDateTime::currentDateTimeUtc();
    record.readings.clear();
    record.readings.append({humidity, "%"});
    record.readings.append({temperature, "°C"});
    record.device_health_status = "ok";
    record.error_code = "";
    record.error_message = "";
    // For now, assume discrete, no order_id, batch_id
    record.order_id = "";
    record.batch_id = "";
    // event_id will be set later

    emit sensor_data_received(record);
    emit_status(ConnectionState::connected, record.timestamp);
    return true;
}

void ModbusDeviceWorker::schedule_reconnect(const QString& reason) {
    if (!running_) {
        return;
    }

    ++reconnect_attempts_;
    const int delay_ms = current_backoff_ms_;
    current_backoff_ms_ = qMin(current_backoff_ms_ * 2, 30000);
    emit_status(ConnectionState::reconnecting);
    UiFileLogger::warn(
        QString("Scheduling reconnect for %1 in %2 ms. Reason: %3").arg(device_config_.id).arg(delay_ms).arg(reason),
        QStringLiteral("controller"));
    reconnect_timer_->start(delay_ms);
}

void ModbusDeviceWorker::emit_status(ConnectionState state, const QDateTime& last_seen) {
    emit device_status_changed(build_status(state, last_seen));
}

void ModbusDeviceWorker::cleanup_socket() {
    inactivity_timer_->stop();
    read_buffer_.clear();
    if (socket_ != nullptr) {
        socket_->abort();
    }
}

}  // namespace data_extraction::dashboard
