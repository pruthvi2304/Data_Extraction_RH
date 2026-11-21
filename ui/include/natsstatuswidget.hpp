#pragma once

#include "dashboard_models.hpp"

#include <QFrame>

class QLabel;

namespace data_extraction::dashboard {

class NatsStatusWidget final : public QFrame {
    Q_OBJECT

public:
    explicit NatsStatusWidget(QWidget* parent = nullptr);

public slots:
    void update_status(const data_extraction::dashboard::NatsConnectionStatus& status);
    void update_runtime_state(data_extraction::dashboard::ConnectionState state, const QString& message);

private:
    QLabel* state_badge_ {nullptr};
    QLabel* url_value_ {nullptr};
    QLabel* retries_value_ {nullptr};
    QLabel* latency_value_ {nullptr};
    QLabel* runtime_value_ {nullptr};
};

}  // namespace data_extraction::dashboard
