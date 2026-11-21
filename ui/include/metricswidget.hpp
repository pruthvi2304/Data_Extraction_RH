#pragma once

#include "dashboard_models.hpp"

#include <QFrame>

class QLabel;

namespace data_extraction::dashboard {

class MetricsWidget final : public QFrame {
    Q_OBJECT

public:
    explicit MetricsWidget(QWidget* parent = nullptr);

public slots:
    void update_metrics(const data_extraction::dashboard::PublishMetrics& metrics);

private:
    QLabel* total_sent_value_ {nullptr};
    QLabel* failed_value_ {nullptr};
    QLabel* retries_value_ {nullptr};
    QLabel* last_sent_value_ {nullptr};
};

}  // namespace data_extraction::dashboard
