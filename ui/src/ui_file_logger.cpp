#include "ui_file_logger.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QtGlobal>

#include <algorithm>
#include <cstdlib>

namespace data_extraction::dashboard {
namespace {

QMutex& logger_mutex() {
    static QMutex mutex;
    return mutex;
}

QString& logger_directory() {
    static QString path = QStringLiteral("logs");
    return path;
}

int& logger_max_file_size_mb() {
    static int value = 10;
    return value;
}

int& logger_max_files() {
    static int value = 5;
    return value;
}

QString app_log_path() {
    return QDir(logger_directory()).filePath("app.log");
}

QString error_log_path() {
    return QDir(logger_directory()).filePath("error.log");
}

QString data_log_path() {
    return QDir(logger_directory()).filePath("data.log");
}

void rotate_if_needed(const QString& file_path) {
    const QFileInfo info(file_path);
    if (!info.exists()) {
        return;
    }

    const auto max_size = static_cast<qint64>(logger_max_file_size_mb()) * 1024 * 1024;
    if (max_size <= 0 || info.size() < max_size) {
        return;
    }

    for (int index = std::max(1, logger_max_files() - 1); index >= 1; --index) {
        const auto source = QString("%1.%2").arg(file_path).arg(index);
        const auto target = QString("%1.%2").arg(file_path).arg(index + 1);
        if (QFile::exists(target)) {
            QFile::remove(target);
        }
        if (QFile::exists(source)) {
            QFile::rename(source, target);
        }
    }

    const auto rotated = QString("%1.1").arg(file_path);
    if (QFile::exists(rotated)) {
        QFile::remove(rotated);
    }
    QFile::rename(file_path, rotated);
}

void append_line(const QString& file_path, const QString& line) {
    rotate_if_needed(file_path);
    QFile file(file_path);
    const QFileInfo file_info(file);
    QDir().mkpath(file_info.absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << line << '\n';
}

void qt_message_handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    QString level = QStringLiteral("INFO");
    switch (type) {
        case QtDebugMsg:
            level = QStringLiteral("DEBUG");
            break;
        case QtInfoMsg:
            level = QStringLiteral("INFO");
            break;
        case QtWarningMsg:
            level = QStringLiteral("WARN");
            break;
        case QtCriticalMsg:
            level = QStringLiteral("ERROR");
            break;
        case QtFatalMsg:
            level = QStringLiteral("FATAL");
            break;
    }

    const QString category = context.category != nullptr ? QString::fromUtf8(context.category) : QStringLiteral("qt");
    UiFileLogger::write_line(level, category, message);
    if (type == QtFatalMsg) {
        abort();
    }
}

}  // namespace

void UiFileLogger::initialize(const QString& directory_path, int max_file_size_mb, int max_files) {
    QMutexLocker lock(&logger_mutex());
    logger_directory() = directory_path;
    logger_max_file_size_mb() = max_file_size_mb;
    logger_max_files() = std::max(2, max_files);
    QDir().mkpath(logger_directory());
}

void UiFileLogger::install_qt_message_handler() {
    qInstallMessageHandler(qt_message_handler);
}

void UiFileLogger::trace(const QString& message, const QString& category) {
    write_line(QStringLiteral("TRACE"), category, message);
}

void UiFileLogger::debug(const QString& message, const QString& category) {
    write_line(QStringLiteral("DEBUG"), category, message);
}

void UiFileLogger::info(const QString& message) {
    info(message, QStringLiteral("app"));
}

void UiFileLogger::info(const QString& message, const QString& category) {
    write_line(QStringLiteral("INFO"), category, message);
}

void UiFileLogger::warn(const QString& message) {
    warn(message, QStringLiteral("app"));
}

void UiFileLogger::warn(const QString& message, const QString& category) {
    write_line(QStringLiteral("WARN"), category, message);
}

void UiFileLogger::error(const QString& message) {
    error(message, QStringLiteral("app"));
}

void UiFileLogger::error(const QString& message, const QString& category) {
    write_line(QStringLiteral("ERROR"), category, message);
}

void UiFileLogger::data(const QString& message) {
    write_line(QStringLiteral("DATA"), QStringLiteral("sensor"), message);
}

void UiFileLogger::write_line(const QString& level, const QString& category, const QString& message) {
    QMutexLocker lock(&logger_mutex());
    const QString line =
        QString("%1 [%2] [%3] %4").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate), level, category, message);
    append_line(app_log_path(), line);

    if (level == QStringLiteral("ERROR") || level == QStringLiteral("FATAL")) {
        append_line(error_log_path(), line);
    }

    if (level == QStringLiteral("DATA") || category == QStringLiteral("sensor")) {
        append_line(data_log_path(), line);
    }
}

}  // namespace data_extraction::dashboard
