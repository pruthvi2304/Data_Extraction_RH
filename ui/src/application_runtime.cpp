#include "application_runtime.hpp"

#include "modbus_device_worker.hpp"
#include "nats_publisher_service.hpp"
#include "ui_file_logger.hpp"

#include <QThread>

#include <utility>

namespace data_extraction::dashboard {

ApplicationRuntime::ApplicationRuntime(UiAppConfig config, QObject* parent)
    : QObject(parent), config_(std::move(config)) {}

ApplicationRuntime::~ApplicationRuntime() {
    stop();
}

const UiAppConfig& ApplicationRuntime::config() const {
    return config_;
}

void ApplicationRuntime::start() {
    if (started_) {
        return;
    }

    started_ = true;
    UiFileLogger::info(QString("Starting integrated Qt runtime with %1 devices.").arg(config_.devices.size()),
                       QStringLiteral("runtime"));
    emit runtime_state_changed(ConnectionState::reconnecting, QStringLiteral("Starting controller and publisher services."));

    publisher_thread_ = new QThread(this);
    publisher_ = new NatsPublisherService(config_.nats, config_.queue_capacity);
    publisher_->moveToThread(publisher_thread_);
    connect(publisher_thread_, &QThread::finished, publisher_, &QObject::deleteLater);
    connect(this, &ApplicationRuntime::sensor_data_received, publisher_, &NatsPublisherService::enqueue_record, Qt::QueuedConnection);
    connect(publisher_, &NatsPublisherService::status_changed, this, &ApplicationRuntime::nats_status_received);
    connect(publisher_, &NatsPublisherService::metrics_changed, this, &ApplicationRuntime::publish_metrics_received);
    connect(publisher_, &NatsPublisherService::alert_raised, this, &ApplicationRuntime::alert_raised);
    connect(publisher_thread_, &QThread::started, publisher_, &NatsPublisherService::start);
    publisher_thread_->start();

    create_workers();
    emit runtime_state_changed(ConnectionState::connected, QStringLiteral("Integrated runtime started."));
}

void ApplicationRuntime::stop() {
    if (!started_) {
        return;
    }

    started_ = false;
    UiFileLogger::warn(QStringLiteral("Stopping integrated Qt runtime."), QStringLiteral("runtime"));

    for (auto* worker : workers_) {
        if (worker != nullptr) {
            QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
        }
    }
    for (auto* thread : worker_threads_) {
        if (thread != nullptr) {
            thread->quit();
            thread->wait(3000);
            thread->deleteLater();
        }
    }
    worker_threads_.clear();
    workers_.clear();

    if (publisher_ != nullptr) {
        QMetaObject::invokeMethod(publisher_, "stop", Qt::BlockingQueuedConnection);
    }
    if (publisher_thread_ != nullptr) {
        publisher_thread_->quit();
        publisher_thread_->wait(3000);
        publisher_thread_->deleteLater();
        publisher_thread_ = nullptr;
        publisher_ = nullptr;
    }

    emit runtime_state_changed(ConnectionState::disconnected, QStringLiteral("Integrated runtime stopped."));
}

void ApplicationRuntime::create_workers() {
    for (const auto& device : config_.devices) {
        auto* thread = new QThread(this);
        auto* worker = new ModbusDeviceWorker(device, config_.request_timeout_ms);
        worker->moveToThread(thread);
        connect_worker(worker, thread);
        thread->start();
        worker_threads_.push_back(thread);
        workers_.push_back(worker);
    }
}

void ApplicationRuntime::connect_worker(ModbusDeviceWorker* worker, QThread* thread) {
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::started, worker, &ModbusDeviceWorker::start);
    connect(worker, &ModbusDeviceWorker::sensor_data_received, this, &ApplicationRuntime::sensor_data_received, Qt::QueuedConnection);
    connect(worker, &ModbusDeviceWorker::device_status_changed, this, &ApplicationRuntime::device_status_received, Qt::QueuedConnection);
    connect(worker, &ModbusDeviceWorker::worker_alert, this, &ApplicationRuntime::alert_raised, Qt::QueuedConnection);
    connect(worker, &ModbusDeviceWorker::worker_trace, this, [this](const QString& message) {
        emit runtime_state_changed(ConnectionState::reconnecting, message);
    }, Qt::QueuedConnection);
}

}  // namespace data_extraction::dashboard
