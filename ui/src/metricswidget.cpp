#include "metricswidget.hpp"

#include <QFormLayout>
#include <QLabel>

namespace data_extraction::dashboard {

MetricsWidget::MetricsWidget(QWidget* parent) : QFrame(parent) {
    setObjectName("metricsCard");

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    total_sent_value_ = new QLabel("0", this);
    failed_value_ = new QLabel("0", this);
    retries_value_ = new QLabel("0", this);
    last_sent_value_ = new QLabel("Never", this);

    layout->addRow("Total Messages", total_sent_value_);
    layout->addRow("Failed Messages", failed_value_);
    layout->addRow("Retry Count", retries_value_);
    layout->addRow("Last Publish", last_sent_value_);
}

void MetricsWidget::update_metrics(const PublishMetrics& metrics) {
    total_sent_value_->setText(QString::number(metrics.total_sent));
    failed_value_->setText(QString::number(metrics.failed));
    retries_value_->setText(QString::number(metrics.retries));
    last_sent_value_->setText(metrics.last_sent.isValid() ? metrics.last_sent.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : "Never");
}

}  // namespace data_extraction::dashboard
