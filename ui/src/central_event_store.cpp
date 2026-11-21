#include "central_event_store.hpp"
#include "timestamp_utils.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUrl>
#include <QVariant>

#include <utility>

namespace data_extraction::dashboard {
namespace {
void ensure_database_runtime() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;
#ifdef Q_OS_WIN
    const QString plugin_dir = QCoreApplication::applicationDirPath() + QStringLiteral("/sqldrivers");
    QCoreApplication::addLibraryPath(plugin_dir);
    qputenv("QT_PLUGIN_PATH", plugin_dir.toUtf8());
#endif
}

bool is_postgres_connection(const QString& value) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    return trimmed.startsWith(QStringLiteral("postgres://"), Qt::CaseInsensitive) ||
           trimmed.startsWith(QStringLiteral("postgresql://"), Qt::CaseInsensitive) ||
           trimmed.contains(QStringLiteral("dbname="), Qt::CaseInsensitive) ||
           trimmed.contains(QStringLiteral("host="), Qt::CaseInsensitive);
}

QString resolve_local_database_path(const QString& connection_string) {
    const QString trimmed = connection_string.trimmed();
    if (trimmed.isEmpty() || is_postgres_connection(trimmed)) {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/logs/central_events.sqlite");
    }

    const QFileInfo info(trimmed);
    if (info.isAbsolute()) {
        return trimmed;
    }
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(trimmed);
}

bool configure_database_connection(QSqlDatabase& database,
                                   const QString& connection_string,
                                   const QString& connection_name) {
    database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
    database.setDatabaseName(connection_string);
    database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    return database.open();
}

bool should_create_directory(const QString& connection_string) {
    return true;
}

void close_and_remove_database(QSqlDatabase& database, const QString& connection_name) {
    if (database.isValid()) {
        database.close();
    }
    database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_name);
}
}  // namespace

CentralEventStore::CentralEventStore(QString database_path)
    : database_path_(std::move(database_path)) {}

QString create_table_id_column(const QSqlDatabase& database) {
    return database.driverName() == QLatin1String("QPSQL") ? QStringLiteral("BIGSERIAL PRIMARY KEY")
                                                        : QStringLiteral("INTEGER PRIMARY KEY AUTOINCREMENT");
}

bool CentralEventStore::initialize() {
    ensure_database_runtime();
    database_path_ = resolve_local_database_path(database_path_);

    if (should_create_directory(database_path_)) {
        const QFileInfo file_info(database_path_);
        const QDir directory = file_info.absoluteDir();
        if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
            return false;
        }
    }

    const QString connection_name = QStringLiteral("central_event_store_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase database;
    if (!configure_database_connection(database, database_path_, connection_name)) {
        return false;
    }

    bool success = false;
    {
        QSqlQuery query(database);
        success = query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS central_event_store ("
            "id %1,"
            "created_at TEXT NOT NULL,"
            "payload TEXT NOT NULL,"
            "subject TEXT NOT NULL,"
            "device_id TEXT,"
            "instrument_id TEXT,"
            "event_type TEXT,"
            "dedupe_key TEXT UNIQUE NOT NULL,"
            "trace_id TEXT)").arg(create_table_id_column(database)));
    }

    close_and_remove_database(database, connection_name);
    return success;
}

bool CentralEventStore::persist_payload(const QString& payload,
                                        const QString& subject,
                                        const QString& device_id,
                                        const QString& instrument_id,
                                        const QString& event_type,
                                        const QString& dedupe_key) {
    if (payload.isEmpty() || dedupe_key.isEmpty()) {
        return false;
    }

    ensure_database_runtime();
    const QString connection_name = QStringLiteral("central_event_store_persist_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase database;
    const QString resolved = resolve_local_database_path(database_path_);
    if (!configure_database_connection(database, resolved, connection_name)) {
        return false;
    }

    bool success = false;
    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS central_event_store ("
                "id %1,"
                "created_at TEXT NOT NULL,"
                "payload TEXT NOT NULL,"
                "subject TEXT NOT NULL,"
                "device_id TEXT,"
                "instrument_id TEXT,"
                "event_type TEXT,"
                "dedupe_key TEXT UNIQUE NOT NULL,"
                "trace_id TEXT)").arg(create_table_id_column(database)))) {
            close_and_remove_database(database, connection_name);
            return false;
        }

        const QString timestamp = format_ist_iso_timestamp(QDateTime::currentDateTime());
        query.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO central_event_store (created_at, payload, subject, device_id, instrument_id, event_type, dedupe_key, trace_id) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(timestamp);
        query.addBindValue(payload);
        query.addBindValue(subject);
        query.addBindValue(device_id);
        query.addBindValue(instrument_id);
        query.addBindValue(event_type);
        query.addBindValue(dedupe_key);
        query.addBindValue(QString());
        success = query.exec();
    }

    close_and_remove_database(database, connection_name);
    return success;
}

}  // namespace data_extraction::dashboard
