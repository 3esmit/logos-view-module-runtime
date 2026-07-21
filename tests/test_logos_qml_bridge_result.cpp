// Regression guard for the QML-only `result`-type bug: a `result`-returning
// method (e.g. makeResult) comes back as a custom LogosResult QVariant, which
// QJsonValue::fromVariant cannot convert — serializeResultForTesting used to
// degrade it to the literal "null", so QML's JSON.parse produced null and every
// result round-trip failed. It must instead emit the canonical
// {success, value, error} shape (empty error -> null), identical to the lp/std
// transport, so `logos.callModule(...)` of a result method round-trips to QML.
#include "LogosQmlBridge.h"

#include "logos_types.h"  // LogosResult

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QTest>
#include <QVariant>
#include <QVariantMap>

class TestLogosQmlBridgeResult : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<LogosResult>("LogosResult"); }

    // makeResult(true)-shaped success with an empty error must serialize to
    // {"success":true,"value":{...},"error":null} — the exact value the QML-only
    // full-api doctest asserts. Previously this produced "null".
    void successResult_emptyError_serializesCanonically()
    {
        LogosResult lr;
        lr.success = true;
        QVariantMap value;
        value["ok"] = true;
        value["provider"] = QStringLiteral("test_fullapi_cpp");
        lr.value = value;
        lr.error = QVariant();  // empty error -> canonical null

        const QString json =
            LogosQmlBridge::serializeResultForTesting(QVariant::fromValue(lr));
        QVERIFY2(json != QLatin1String("null"),
                 qPrintable(QStringLiteral("serialized to bare null: %1").arg(json)));

        const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
        QVERIFY2(!o.isEmpty(), qPrintable(QStringLiteral("not an object: %1").arg(json)));
        QCOMPARE(o.value(QStringLiteral("success")).toBool(), true);
        QVERIFY2(o.value(QStringLiteral("error")).isNull(),
                 qPrintable(QStringLiteral("error not null: %1").arg(json)));
        const QJsonObject v = o.value(QStringLiteral("value")).toObject();
        QCOMPARE(v.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(v.value(QStringLiteral("provider")).toString(),
                 QStringLiteral("test_fullapi_cpp"));
    }

    // A failure result carries its error string through intact.
    void failureResult_serializesErrorString()
    {
        LogosResult lr;
        lr.success = false;
        lr.value = QVariant();
        lr.error = QStringLiteral("deliberate error for testing");

        const QString json =
            LogosQmlBridge::serializeResultForTesting(QVariant::fromValue(lr));

        const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
        QCOMPARE(o.value(QStringLiteral("success")).toBool(), false);
        QCOMPARE(o.value(QStringLiteral("error")).toString(),
                 QStringLiteral("deliberate error for testing"));
    }
};

QTEST_GUILESS_MAIN(TestLogosQmlBridgeResult)
#include "test_logos_qml_bridge_result.moc"
