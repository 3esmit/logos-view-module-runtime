// End-to-end LogosQmlBridge test: a real provider published over a local socket,
// a real LogosAPI consumer, and a QJSEngine driving an ASYNC CHAIN through the
// bridge exactly like a QML-only ui_qml plugin does (each reply callback issues
// the next call). Every lower layer (transport, LogosAPIConsumer — including the
// chained case — and LogosAPIClient's cached-token passthrough) is already
// proven in logos-protocol's gtests; this pins the remaining layer: the bridge's
// QJSValue callback path under a chained JS driver.
#include "LogosQmlBridge.h"

#include "logos_api.h"                 // LogosAPI, getTokenManager
#include "token_manager.h"            // TokenManager::saveToken (skip capability)
#include "logos_provider_interface.h" // LogosProviderObject
#include "module_proxy.h"             // ModuleProxy
#include "remote_transport.h"         // RemoteTransportHost
#include "logos_instance.h"           // LogosInstance
#include "logos_mode.h"               // LogosModeConfig / LogosMode

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QString>
#include <QTest>
#include <QVariant>
#include <QVariantList>

namespace {

class EchoProvider : public LogosProviderObject {
public:
    QVariant callMethod(const QString& method, const QVariantList& args) override {
        if (method == QLatin1String("echo") && !args.isEmpty())
            return args.first();
        return QVariant();
    }
    bool informModuleToken(const QString&, const QString&) override { return true; }
    QJsonArray getMethods() override { return QJsonArray{}; }
    void setEventListener(EventCallback) override {}
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("echo_module"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }
};

} // namespace

class TestLogosQmlBridgeE2E : public QObject {
    Q_OBJECT
private slots:
    // Fire an async chain of N calls through logos.callModuleAsync where each
    // reply callback issues the next call — the exact pattern of a QML-only
    // plugin's runAllMethods. Every call must complete.
    void chainedCallModuleAsync_allComplete()
    {
        const QString registryUrl = LogosInstance::id("echo_module");

        RemoteTransportHost host(registryUrl);
        EchoProvider provider;
        ModuleProxy proxy(&provider);
        QVERIFY(proxy.saveToken(QStringLiteral("caller"), QStringLiteral("tok")));
        QVERIFY(host.publishObject("echo_module", &proxy));

        LogosModeConfig::setMode(LogosMode::Remote);

        LogosAPI api(QStringLiteral("caller"));
        // Pre-seed the target token so the client skips the capability_module
        // requestModule handshake (no capability provider needed in this test).
        api.getTokenManager()->saveToken(QStringLiteral("echo_module"),
                                         QStringLiteral("tok"));

        LogosQmlBridge bridge(&api);

        // Reproduce the QML plugin's up-front event subscriptions: onModuleEvent
        // acquires a fresh replica per subscription. Do it before the method
        // chain, exactly like Component.onCompleted subscribing to N events.
        for (int i = 0; i < 14; ++i)
            bridge.onModuleEvent("echo_module", QStringLiteral("ev%1").arg(i));
        for (int k = 0; k < 40; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);

        QJSEngine engine;
        engine.setObjectOwnership(&bridge, QJSEngine::CppOwnership);
        engine.globalObject().setProperty(
            QStringLiteral("logos"), engine.newQObject(&bridge));

        constexpr int N = 12;
        // Top-level var/function become global properties in QJSEngine's global
        // scope (there is no `globalThis`); the callbacks close over __results.
        QJSValue driver = engine.evaluate(QStringLiteral(R"JS(
            var __results = [];
            function fireNext(i) {
              if (i >= %1) return;
              logos.callModuleAsync("echo_module", "echo", [i], function (payload) {
                __results.push(JSON.parse(payload));
                fireNext(i + 1);
              });
            }
            fireNext(0);
        )JS").arg(N));
        QVERIFY2(!driver.isError(), qPrintable(driver.toString()));

        QJSValue results = engine.globalObject().property(QStringLiteral("__results"));
        QElapsedTimer t; t.start();
        while (results.property(QStringLiteral("length")).toInt() < N && t.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }

        QCOMPARE(results.property(QStringLiteral("length")).toInt(), N);
        for (int i = 0; i < N; ++i)
            QCOMPARE(results.property(i).toInt(), i);
    }
};

QTEST_GUILESS_MAIN(TestLogosQmlBridgeE2E)
#include "test_logos_qml_bridge_e2e.moc"
