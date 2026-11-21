#include "timestamp_utils.hpp"

#include <QTimeZone>

namespace data_extraction::dashboard {
namespace {
const QTimeZone& ist_timezone() {
    static const QTimeZone timezone(QByteArrayLiteral("Asia/Kolkata"));
    return timezone;
}
}  // namespace

QDateTime to_ist_time(const QDateTime& timestamp) {
    if (!timestamp.isValid()) {
        return {};
    }
    return timestamp.toTimeZone(ist_timezone());
}

QString format_ist_timestamp(const QDateTime& timestamp, const QString& format) {
    const QDateTime ist_time = to_ist_time(timestamp);
    return ist_time.isValid() ? ist_time.toString(format) : QString();
}

QString format_ist_iso_timestamp(const QDateTime& timestamp) {
    const QDateTime ist_time = to_ist_time(timestamp);
    return ist_time.isValid() ? ist_time.toString(Qt::ISODateWithMs) : QString();
}

}  // namespace data_extraction::dashboard
