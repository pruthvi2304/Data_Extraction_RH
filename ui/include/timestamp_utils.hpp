#pragma once

#include <QDateTime>
#include <QString>

namespace data_extraction::dashboard {

QDateTime to_ist_time(const QDateTime& timestamp);
QString format_ist_timestamp(const QDateTime& timestamp, const QString& format);
// Produces ISO 8601 with milliseconds and the explicit IST UTC offset (+05:30).
QString format_ist_iso_timestamp(const QDateTime& timestamp);

}  // namespace data_extraction::dashboard
