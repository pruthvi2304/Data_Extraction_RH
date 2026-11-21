#include "json_parser.hpp"

#include <QtTest/QtTest>

using data_extraction::dashboard::ConnectionState;
using data_extraction::dashboard::JsonParser;

class JsonParserQtTest final : public QObject {
    Q_OBJECT

private slots:
    void parses_sensor_message() {
        JsonParser parser;
        const QByteArray payload = R"({"type":"sensor_data","device_id":"ctrl_1","temperature":25.5,"humidity":60.2,"timestamp":"2026-04-24T12:00:00Z"})";

        const auto result = parser.parse_message(payload);

        QCOMPARE(result.type, JsonParser::Result::Type::sensor_data);
        QVERIFY(result.sensor_record.has_value());
        QCOMPARE(result.sensor_record->device_id, QString("ctrl_1"));
        QCOMPARE(result.sensor_record->temperature, 25.5);
        QCOMPARE(result.sensor_record->humidity, 60.2);
    }

    void parses_device_message() {
        JsonParser parser;
        const QByteArray payload = R"({"type":"device_update","device_id":"ctrl_1","status":"reconnecting","ip":"192.168.1.10","port":502,"last_seen":"2026-04-24T12:00:00Z"})";

        const auto result = parser.parse_message(payload);

        QCOMPARE(result.type, JsonParser::Result::Type::device_update);
        QVERIFY(result.device_status.has_value());
        QCOMPARE(static_cast<int>(result.device_status->status), static_cast<int>(ConnectionState::reconnecting));
    }

    void rejects_invalid_message() {
        JsonParser parser;
        const auto result = parser.parse_message(R"({"type":"sensor_data","device_id":"ctrl_1"})");

        QCOMPARE(result.type, JsonParser::Result::Type::invalid);
        QVERIFY(!result.error_message.isEmpty());
    }
};

QTEST_MAIN(JsonParserQtTest)
#include "test_json_parser_qt.moc"
