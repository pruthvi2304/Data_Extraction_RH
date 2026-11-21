#include "dashboardwidget.hpp"

#include "application_runtime.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QHeaderView>
#include <QTableView>
#include <QVBoxLayout>
#include <QAbstractItemView>
#include <QPixmap>
#include <QDialog>
#include <QToolButton>
#include <QFrame>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>

namespace data_extraction::dashboard {

DashboardWidget::DashboardWidget(const UiAppConfig* app_config, QWidget* parent) : QWidget(parent) {
    device_model_ = new DeviceTableModel(this);
    sensor_model_ = new SensorTableModel(this);
    metrics_widget_ = new MetricsWidget(this);
    nats_status_widget_ = new NatsStatusWidget(this);

    if (app_config != nullptr) {
        QList<DeviceStatus> initial_devices;
        for (const auto& device : app_config->devices) {
            if (!device.id.isEmpty() && !device_card_order_.contains(device.id)) {
                device_card_order_.push_back(device.id);
            }
            DeviceStatus initial;
            initial.device_id = device.id;
            initial.ip = device.ip;
            initial.port = device.port;
            initial.status = ConnectionState::disconnected;
            initial_devices.push_back(initial);
        }
        device_model_->set_initial_devices(initial_devices);
    }

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(16, 16, 16, 16);
    root_layout->setSpacing(12);

    alert_label_ = new QLabel("Waiting for integrated runtime...", this);
    alert_label_->setObjectName("alertBanner");
    root_layout->addWidget(alert_label_);

    auto* main_area = new QWidget(this);
    auto* main_layout = new QHBoxLayout(main_area);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(12);

    auto* sidebar = new QScrollArea(main_area);
    sidebar->setWidgetResizable(true);
    sidebar->setFrameShape(QFrame::NoFrame);
    sidebar->setMinimumWidth(230);
    sidebar->setMaximumWidth(340);
    sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* sidebar_content = new QWidget(sidebar);
    sidebar_content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* sidebar_layout = new QVBoxLayout(sidebar_content);
    sidebar_layout->setContentsMargins(0, 0, 0, 0);
    sidebar_layout->setSpacing(10);

    auto* logo_label = new QLabel(sidebar_content);
    logo_label->setAlignment(Qt::AlignCenter);
    logo_label->setPixmap(QPixmap(":/images/ShasmayLogo.jpg").scaled(240, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    sidebar_layout->addWidget(logo_label, 0, Qt::AlignHCenter);

    auto* matrix = new QGridLayout();
    matrix->setContentsMargins(0, 0, 0, 0);
    matrix->setVerticalSpacing(10);
    matrix->addWidget(build_health_card(), 0, 0);
    matrix->addWidget(create_card("Publishing Metrics", metrics_widget_), 1, 0);
    matrix->addWidget(create_card("NATS & Transport", nats_status_widget_), 2, 0);
    sidebar_layout->addLayout(matrix);
    sidebar_layout->addStretch(1);
    sidebar->setWidget(sidebar_content);

    auto* devices_area = new QScrollArea(main_area);
    devices_area->setWidgetResizable(true);
    devices_area->setFrameShape(QFrame::NoFrame);
    devices_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* devices_content = new QWidget(devices_area);
    auto* devices_layout = new QVBoxLayout(devices_content);
    devices_layout->setContentsMargins(0, 0, 0, 0);
    devices_layout->addWidget(build_device_card(), 1);
    devices_area->setWidget(devices_content);

    main_layout->addWidget(sidebar, 0);
    main_layout->addWidget(devices_area, 1);
    root_layout->addWidget(main_area, 1);

    connect(sensor_model_, &SensorTableModel::data_summary_changed, this, &DashboardWidget::refresh_health_summary);
    refresh_health_summary();
}

void DashboardWidget::attach_runtime(ApplicationRuntime* runtime) {
    connect(runtime, &ApplicationRuntime::sensor_data_received, sensor_model_, &SensorTableModel::append_record);
    connect(runtime, &ApplicationRuntime::device_status_received, device_model_, &DeviceTableModel::upsert_device);
    connect(runtime, &ApplicationRuntime::device_status_received, this, &DashboardWidget::update_device_card);
    connect(runtime, &ApplicationRuntime::sensor_data_received, this, &DashboardWidget::update_device_reading);
    connect(runtime, &ApplicationRuntime::nats_status_received, nats_status_widget_, &NatsStatusWidget::update_status);
    connect(runtime, &ApplicationRuntime::publish_metrics_received, metrics_widget_, &MetricsWidget::update_metrics);
    connect(runtime, &ApplicationRuntime::runtime_state_changed, nats_status_widget_, &NatsStatusWidget::update_runtime_state);
    connect(runtime, &ApplicationRuntime::alert_raised, this, &DashboardWidget::show_alert);
    connect(runtime, &ApplicationRuntime::runtime_state_changed, this, [this](ConnectionState, const QString& message) {
        emit status_message_changed(message);
    });
    connect(runtime, &ApplicationRuntime::device_status_received, this, [this](const DeviceStatus&) {
        refresh_health_summary();
    });
}

void DashboardWidget::show_alert(const QString& message) {
    alert_label_->setText(message);
}

void DashboardWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { relayout_device_cards(); });
}

void DashboardWidget::relayout_device_cards() {
    if (device_cards_layout_ == nullptr || device_cards_container_ == nullptr) return;
    constexpr int card_min_width = 280;
    constexpr int spacing = 12;
    const int usable_width = qMax(1, device_cards_container_->width() - 16);
    const int columns = qBound(1, (usable_width + spacing) / (card_min_width + spacing), 4);
    for (int column = 0; column < 4; ++column) device_cards_layout_->setColumnStretch(column, column < columns ? 1 : 0);
    for (int index = 0; index < device_card_order_.size(); ++index) {
        if (auto* card = device_cards_.value(device_card_order_.at(index))) {
            device_cards_layout_->removeWidget(card);
            device_cards_layout_->addWidget(card, index / columns, index % columns);
        }
    }
}

void DashboardWidget::refresh_health_summary() {
    health_summary_label_->setText(
        QString("Devices: %1 active / %2 reconnecting / %3 offline | Sensor records: %4")
            .arg(device_model_->connected_count())
            .arg(device_model_->reconnecting_count())
            .arg(device_model_->disconnected_count())
            .arg(sensor_model_->total_visible_records()));
}

QWidget* DashboardWidget::create_card(const QString& title, QWidget* content) {
    auto* card = new QWidget(this);
    card->setObjectName("dashboardCard");

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* title_label = new QLabel(title, card);
    title_label->setObjectName("cardTitle");
    layout->addWidget(title_label);
    layout->addWidget(content);

    return card;
}

QWidget* DashboardWidget::build_health_card() {
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    health_summary_label_ = new QLabel(container);
    health_summary_label_->setWordWrap(true);
    health_summary_label_->setMinimumWidth(0);
    layout->addWidget(health_summary_label_);

    return create_card("System Health", container);
}

QWidget* DashboardWidget::build_device_card() {
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(container);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    device_cards_container_ = new QWidget(scroll);
    device_cards_layout_ = new QGridLayout(device_cards_container_);
    device_cards_layout_->setContentsMargins(8, 8, 8, 8);
    device_cards_layout_->setSpacing(12);
    scroll->setWidget(device_cards_container_);
    layout->addWidget(scroll);
    container->setMinimumHeight(230);
    return create_card("Connected Devices", container);
}

void DashboardWidget::update_device_card(const DeviceStatus& status) {
    // Add cards when a device first connects; keep them visible through reconnects.
    if (!device_cards_.contains(status.device_id)) {
        if (status.status != ConnectionState::connected) return;
        auto* card = new QToolButton(device_cards_container_);
        card->setObjectName("controllerCard");
        card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        card->setCursor(Qt::PointingHandCursor);
        card->setMinimumSize(260, 150);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* card_layout = new QVBoxLayout(card);
        auto* title = new QLabel(status.device_id, card);
        title->setObjectName("controllerCardTitle");
        title->setAlignment(Qt::AlignCenter);
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* state = new QLabel(card);
        state->setAlignment(Qt::AlignCenter);
        state->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* readings = new QLabel("Waiting for readings…", card);
        readings->setAlignment(Qt::AlignCenter);
        readings->setWordWrap(true);
        readings->setAttribute(Qt::WA_TransparentForMouseEvents);
        card_layout->addWidget(title);
        card_layout->addWidget(state);
        card_layout->addWidget(readings, 1);
        connect(card, &QToolButton::clicked, this, [this, id = status.device_id] { open_device_details(id); });
        device_cards_.insert(status.device_id, card);
        if (!device_card_order_.contains(status.device_id)) {
            device_card_order_.push_back(status.device_id);
        }
        device_status_labels_.insert(status.device_id, state);
        device_reading_labels_.insert(status.device_id, readings);
        relayout_device_cards();
    }
    if (auto* label = device_status_labels_.value(status.device_id)) {
        label->setText(connection_state_to_string(status.status));
        label->setStyleSheet(status.status == ConnectionState::connected ? "color:#81c784;font-weight:600" : "color:#ffb74d;font-weight:600");
    }
}

void DashboardWidget::update_device_reading(const SensorRecord& record) {
    auto& history = recent_records_[record.device_id];
    history.push_back(record);
    while (history.size() > 100) history.removeFirst();
    if (!device_cards_.contains(record.device_id)) {
        DeviceStatus status;
        status.device_id = record.device_id;
        status.status = ConnectionState::connected;
        update_device_card(status);
    }
    QStringList values;
    for (int i = 0; i < record.readings.size(); ++i) {
        const auto& reading = record.readings.at(i);
        const QString name = !reading.parameter_id.isEmpty() ? reading.parameter_id : (i == 0 ? "Humidity" : "Temperature");
        values << QString("%1: %2%3").arg(name).arg(reading.value, 0, 'f', 1).arg(reading.unit);
    }
    if (auto* label = device_reading_labels_.value(record.device_id)) label->setText(values.join("\n"));
    if (auto* model = detail_models_.value(record.device_id)) model->append_record(record);
    if (auto* chart = detail_charts_.value(record.device_id)) chart->append_record(record);
}

void DashboardWidget::open_device_details(const QString& device_id) {
    auto* dialog = detail_dialogs_.value(device_id, nullptr);
    if (dialog != nullptr) {
        dialog->show(); dialog->raise(); dialog->activateWindow(); return;
    }
    dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Controller — " + device_id);
    dialog->resize(1000, 700);
    auto* layout = new QVBoxLayout(dialog);
    auto* chart = new SensorChartWidget(dialog);
    auto* table_model = new SensorTableModel(dialog);
    table_model->set_filter_device_id(device_id);
    auto* table = new QTableView(dialog);
    table->setModel(table_model);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setMinimumHeight(220);
    chart->set_device_filter(device_id);
    layout->addWidget(chart, 3);
    layout->addWidget(table, 2);
    for (const auto& record : recent_records_.value(device_id)) {
        chart->append_record(record);
        table_model->append_record(record);
    }
    connect(dialog, &QObject::destroyed, this, [this, device_id] {
        detail_dialogs_.remove(device_id);
        detail_models_.remove(device_id);
        detail_charts_.remove(device_id);
    });
    // Runtime signals are forwarded through the dashboard's existing receive path.
    detail_dialogs_.insert(device_id, dialog);
    detail_models_.insert(device_id, table_model);
    detail_charts_.insert(device_id, chart);
    dialog->show();
}

}  // namespace data_extraction::dashboard
