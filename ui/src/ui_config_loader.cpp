#include "ui_config_loader.hpp"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>

namespace data_extraction::dashboard {
namespace {
QString resolve_nats_path(const QString& value, const QString& config_file) {
    if (value.isEmpty() || QDir::isAbsolutePath(value)) return value;
    QDir config_dir(QFileInfo(config_file).absolutePath());
    if (config_dir.dirName().compare(QStringLiteral("config"), Qt::CaseInsensitive) == 0) config_dir.cdUp();
    return QDir::cleanPath(config_dir.absoluteFilePath(value));
}
}

UiAppConfig UiConfigLoader::default_config() {
    return UiAppConfig {};
}

UiAppConfig UiConfigLoader::load_from_file(const QString& file_path) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("Unable to open config file: " + file_path).toStdString());
    }

    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        throw std::runtime_error("Invalid dashboard config JSON.");
    }

    const auto root = document.object();
    const auto devices = root.value("devices").toArray();

    UiAppConfig config;
    config.request_timeout_ms = root.value("request_timeout_ms").toInt(config.request_timeout_ms);
    config.queue_capacity = root.value("queue_capacity").toInt(config.queue_capacity);

    for (const auto& value : devices) {
        const auto object = value.toObject();
        UiDeviceConfig device;
        device.id = object.value("id").toString();
        device.ip = object.value("ip").toString();
        device.port = static_cast<quint16>(object.value("port").toInt(502));

        if (!device.id.isEmpty()) {
            config.devices.push_back(device);
        }
    }

    if (root.contains("nats") && root.value("nats").isObject()) {
        const auto object = root.value("nats").toObject();
        config.nats.url = object.value("url").toString(config.nats.url);
        config.nats.tls_enabled = object.value("tls_enabled").toBool(config.nats.tls_enabled);
        config.nats.tls_min_protocol = object.value("tls_min_protocol").toString(config.nats.tls_min_protocol);
        config.nats.client_cert = resolve_nats_path(object.value("client_cert").toString(config.nats.client_cert), file_path);
        config.nats.client_key = resolve_nats_path(object.value("client_key").toString(config.nats.client_key), file_path);
        config.nats.ca_cert = resolve_nats_path(object.value("ca_cert").toString(config.nats.ca_cert), file_path);
        config.nats.nkey_public = object.value("nkey_public").toString(config.nats.nkey_public);
        config.nats.nkey_seed_file = resolve_nats_path(object.value("nkey_seed_file").toString(config.nats.nkey_seed_file), file_path);
        config.nats.subject = object.value("subject").toString(config.nats.subject);
        config.nats.jetstream_enabled = object.value("jetstream_enabled").toBool(config.nats.jetstream_enabled);
        config.nats.jetstream_auto_provision = object.value("jetstream_auto_provision").toBool(config.nats.jetstream_auto_provision);
        config.nats.jetstream_stream = object.value("jetstream_stream").toString(config.nats.jetstream_stream);
        config.nats.connect_timeout_ms = object.value("connect_timeout_ms").toInt(config.nats.connect_timeout_ms);
        config.nats.publish_retry_limit = object.value("publish_retry_limit").toInt(config.nats.publish_retry_limit);
        config.nats.retry_backoff_ms = object.value("retry_backoff_ms").toInt(config.nats.retry_backoff_ms);
        config.nats.retention_minutes = object.value("retention_minutes").toInt(config.nats.retention_minutes);
        config.nats.local_buffer_file = resolve_nats_path(object.value("local_buffer_file").toString(config.nats.local_buffer_file), file_path);
        config.nats.central_db_path = resolve_nats_path(object.value("central_db_path").toString(config.nats.central_db_path), file_path);
        config.nats.enable_central_persistence = object.value("enable_central_persistence").toBool(config.nats.enable_central_persistence);
        config.nats.plant = object.value("plant").toString(config.nats.plant);
        config.nats.work_center = object.value("work_center").toString(config.nats.work_center);
    }

    if (root.contains("logging") && root.value("logging").isObject()) {
        const auto object = root.value("logging").toObject();
        config.logging.directory = object.value("directory").toString(config.logging.directory);
        config.logging.max_file_size_mb = object.value("max_file_size_mb").toInt(config.logging.max_file_size_mb);
        config.logging.max_files = object.value("max_files").toInt(config.logging.max_files);
        config.logging.flush_every_ms = object.value("flush_every_ms").toInt(config.logging.flush_every_ms);
    }

    return config;
}

}  // namespace data_extraction::dashboard
