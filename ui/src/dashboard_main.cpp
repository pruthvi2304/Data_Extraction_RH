#include "mainwindow.hpp"
#include "ui_config_loader.hpp"
#include "ui_file_logger.hpp"

#include <QApplication>
#include <QFile>

#include <exception>
#include <utility>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Data Extraction Dashboard");
    QApplication::setOrganizationName("OpenAI");

    try {
        const QString config_path = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("config/config.json");
        auto loaded_config = data_extraction::dashboard::UiConfigLoader::default_config();
        if (QFile::exists(config_path)) {
            loaded_config = data_extraction::dashboard::UiConfigLoader::load_from_file(config_path);
        }

        data_extraction::dashboard::UiFileLogger::initialize(
            loaded_config.logging.directory,
            loaded_config.logging.max_file_size_mb,
            loaded_config.logging.max_files);
        data_extraction::dashboard::UiFileLogger::install_qt_message_handler();
        data_extraction::dashboard::UiFileLogger::info(
            QString("Launching integrated Qt application with config %1").arg(config_path),
            QStringLiteral("startup"));

        qRegisterMetaType<data_extraction::dashboard::ConnectionState>("data_extraction::dashboard::ConnectionState");
        qRegisterMetaType<data_extraction::dashboard::SensorRecord>("data_extraction::dashboard::SensorRecord");
        qRegisterMetaType<data_extraction::dashboard::DeviceStatus>("data_extraction::dashboard::DeviceStatus");
        qRegisterMetaType<data_extraction::dashboard::NatsConnectionStatus>("data_extraction::dashboard::NatsConnectionStatus");
        qRegisterMetaType<data_extraction::dashboard::PublishMetrics>("data_extraction::dashboard::PublishMetrics");

        QFile style_file(":/styles/styles.qss");
        if (style_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            app.setStyleSheet(QString::fromUtf8(style_file.readAll()));
        }

        data_extraction::dashboard::MainWindow window(std::move(loaded_config));
        window.show();
        return app.exec();
    } catch (const std::exception& ex) {
        data_extraction::dashboard::UiFileLogger::error(QString("Application fatal error: %1").arg(ex.what()), QStringLiteral("startup"));
        return 1;
    }
}
