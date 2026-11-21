#include "mainwindow.hpp"

#include "ui_file_logger.hpp"

#include <QStatusBar>

namespace data_extraction::dashboard {

MainWindow::MainWindow(UiAppConfig app_config, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Data Extraction Dashboard");
    resize(1600, 980);

    dashboard_widget_ = new DashboardWidget(&app_config, this);
    runtime_ = new ApplicationRuntime(std::move(app_config), this);

    setCentralWidget(dashboard_widget_);
    dashboard_widget_->attach_runtime(runtime_);

    connect(dashboard_widget_, &DashboardWidget::status_message_changed, statusBar(), [this](const QString& message) {
        statusBar()->showMessage(message);
    });
    connect(runtime_, &ApplicationRuntime::alert_raised, statusBar(), [this](const QString& message) {
        statusBar()->showMessage(message);
    });
    connect(runtime_, &ApplicationRuntime::runtime_state_changed, statusBar(), [this](ConnectionState, const QString& message) {
        statusBar()->showMessage(message);
    });

    UiFileLogger::info(QStringLiteral("Main window initialized."), QStringLiteral("ui"));
    runtime_->start();
}

MainWindow::~MainWindow() {
    if (runtime_ != nullptr) {
        runtime_->stop();
    }
}

}  // namespace data_extraction::dashboard
