#include "natsstatuswidget.hpp"

#include <QFormLayout>
#include <QLabel>

namespace data_extraction::dashboard {
namespace {

QString badge_style(ConnectionState state) {
    return QString("padding: 6px 10px; border-radius: 10px; font-weight: 600; color: #111; background-color: %1;")
        .arg(connection_state_to_color(state).name());
}

}  // namespace

NatsStatusWidget::NatsStatusWidget(QWidget* parent) : QFrame(parent) {
    setObjectName("natsStatusCard");

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    state_badge_ = new QLabel("Disconnected", this);
    url_value_ = new QLabel("n/a", this);
    retries_value_ = new QLabel("0", this);
    latency_value_ = new QLabel("n/a", this);
    runtime_value_ = new QLabel("Starting", this);
    url_value_->setWordWrap(true);
    runtime_value_->setWordWrap(true);
    url_value_->setMinimumWidth(0);
    runtime_value_->setMinimumWidth(0);

    state_badge_->setStyleSheet(badge_style(ConnectionState::disconnected));
    layout->addRow("NATS State", state_badge_);
    layout->addRow("Server URL", url_value_);
    layout->addRow("Reconnect Attempts", retries_value_);
    layout->addRow("Latency", latency_value_);
    layout->addRow("Runtime", runtime_value_);
}

void NatsStatusWidget::update_status(const NatsConnectionStatus& status) {
    state_badge_->setText(connection_state_to_string(status.status));
    state_badge_->setStyleSheet(badge_style(status.status));
    url_value_->setText(status.url.isEmpty() ? "n/a" : status.url);
    retries_value_->setText(QString::number(status.retries));
    latency_value_->setText(status.latency_ms >= 0 ? QString("%1 ms").arg(status.latency_ms) : "n/a");
}

void NatsStatusWidget::update_runtime_state(ConnectionState state, const QString& message) {
    runtime_value_->setText(connection_state_to_string(state) + " - " + message);
}

}  // namespace data_extraction::dashboard
