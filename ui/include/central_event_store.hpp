#pragma once

#include <QString>

namespace data_extraction::dashboard {

class CentralEventStore {
public:
    explicit CentralEventStore(QString database_path = QString());

    bool initialize();
    bool persist_payload(const QString& payload,
                         const QString& subject,
                         const QString& device_id,
                         const QString& instrument_id,
                         const QString& event_type,
                         const QString& dedupe_key);

private:
    QString database_path_;
};

}  // namespace data_extraction::dashboard
