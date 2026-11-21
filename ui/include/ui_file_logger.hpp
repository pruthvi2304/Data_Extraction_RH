#pragma once

#include <QString>

namespace data_extraction::dashboard {

class UiFileLogger {
public:
    static void initialize(const QString& directory_path, int max_file_size_mb = 10, int max_files = 5);
    static void install_qt_message_handler();
    static void trace(const QString& message, const QString& category = QStringLiteral("app"));
    static void debug(const QString& message, const QString& category = QStringLiteral("app"));
    static void info(const QString& message);
    static void info(const QString& message, const QString& category);
    static void warn(const QString& message);
    static void warn(const QString& message, const QString& category);
    static void error(const QString& message);
    static void error(const QString& message, const QString& category);
    static void data(const QString& message);
    static void write_line(const QString& level, const QString& category, const QString& message);
};

}  // namespace data_extraction::dashboard
