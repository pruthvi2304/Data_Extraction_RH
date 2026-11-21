#pragma once

#include "application_runtime.hpp"
#include "dashboardwidget.hpp"
#include "ui_config.hpp"

#include <QMainWindow>

namespace data_extraction::dashboard {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(UiAppConfig app_config, QWidget* parent = nullptr);

    ~MainWindow() override;

private:
    DashboardWidget* dashboard_widget_ {nullptr};
    ApplicationRuntime* runtime_ {nullptr};
};

}  // namespace data_extraction::dashboard
