// Capability-handshake reproduction: the one code path every prior harness
// bypassed by pre-seeding the target token. Here the target (echo_module) token
// is NOT pre-seeded, so the LogosAPIClient runs its real auto-`requestModule`
// handshake against a published capability_module provider on the FIRST call —
// while 14 event-subscription replicas are live — exactly like a QML-only plugin
// in basecamp. Exercises both the sync path (callModule → requestModule spins a
// nested waitForFinished loop, then the real call spins another) and the async
// path (requestModule chaining + first-burst coalescing).
//
// If the sequential-call stall is rooted in the handshake / nested-loop
// reentrancy / token-rotation, this is where it should finally surface.
#include "LogosQmlBridge.h"

#include "logos_api.h"
#include "token_manager.h"
#include "logos_provider_interface.h"
#include "module_proxy.h"
#include "remote_transport.h"
#include "logos_instance.h"
#include "logos_mode.h"

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
    EventCallback emitFn;
    bool fireEvents = false;
    QVariant callMethod(const QString& method, const QVariantList& args) override {
        if (method == QLatin1String("echo") && !args.isEmpty()) {
            // Optionally fire an event back mid-chain (LOGOS_FIRE_EVENTS=1) to
            // model a provider that emits during the call sequence — the delivery
            // races the nested handshake/call loops.
            if (fireEvents && emitFn)
                emitFn(QStringLiteral("ev0"), QVariantList() << args.first());
            return args.first();
        }
        return QVariant();
    }
    bool informModuleToken(const QString&, const QString&) override { return true; }
    QJsonArray getMethods() override { return QJsonArray{}; }
    void setEventListener(EventCallback cb) override { emitFn = std::move(cb); }
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("echo_module"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }
};

// Minimal capability_module: requestModule(origin, target) mints a token and
// informs the target proxy (mirrors the real capability flow's saveToken on the
// target), so the target subsequently authorizes the caller.
class CapabilityProvider : public LogosProviderObject {
public:
    ModuleProxy* echoProxy = nullptr;
    bool rotate = false;      // LOGOS_ROTATE_TOKENS=1 → mint a fresh token per call
    int calls = 0;
    QVariant callMethod(const QString& method, const QVariantList& args) override {
        if (method == QLatin1String("requestModule") && args.size() >= 2) {
            const QString origin = args[0].toString();
            const QString mint = rotate
                ? QStringLiteral("echo-token-%1").arg(++calls)
                : QStringLiteral("echo-token");
            if (!rotate) ++calls;
            if (echoProxy) echoProxy->saveToken(origin, mint);
            return mint;
        }
        return QVariant();
    }
    bool informModuleToken(const QString&, const QString&) override { return true; }
    QJsonArray getMethods() override { return QJsonArray{}; }
    void setEventListener(EventCallback) override {}
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("capability_module"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }
};

struct Fixture {
    RemoteTransportHost echoHost;
    EchoProvider echo;
    ModuleProxy echoProxy;

    RemoteTransportHost capHost;
    CapabilityProvider cap;
    ModuleProxy capProxy;

    LogosAPI api;
    LogosQmlBridge bridge;

    Fixture()
        : echoHost(LogosInstance::id("echo_module"))
        , echoProxy(&echo)
        , capHost(LogosInstance::id("capability_module"))
        , capProxy(&cap)
        , api(QStringLiteral("caller"))
        , bridge(&api)
    {
        cap.echoProxy = &echoProxy;
        cap.rotate = !qEnvironmentVariableIsEmpty("LOGOS_ROTATE_TOKENS");
        echo.fireEvents = !qEnvironmentVariableIsEmpty("LOGOS_FIRE_EVENTS");

        echoHost.publishObject("echo_module", &echoProxy);

        // capability_module authorizes the caller with the pre-shared cap token.
        capProxy.saveToken(QStringLiteral("caller"), QStringLiteral("cap-token"));
        capHost.publishObject("capability_module", &capProxy);

        LogosModeConfig::setMode(LogosMode::Remote);

        // Pre-seed ONLY the capability token (the app-boot auth token). The
        // echo_module token is deliberately absent → forces the real handshake.
        api.getTokenManager()->saveToken(QStringLiteral("capability_module"),
                                         QStringLiteral("cap-token"));
    }

    void subscribeEvents(int n)
    {
        for (int i = 0; i < n; ++i)
            bridge.onModuleEvent("echo_module", QStringLiteral("ev%1").arg(i));
        for (int k = 0; k < 60; ++k)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
};

} // namespace

class TestLogosQmlBridgeHandshake : public QObject {
    Q_OBJECT
private slots:
    // ASYNC chain over the real handshake: first call triggers requestModule,
    // rest ride the cached token. All must complete.
    void asyncChainWithHandshake_allComplete()
    {
        Fixture fx;
        fx.subscribeEvents(14);

        QJSEngine engine;
        engine.setObjectOwnership(&fx.bridge, QJSEngine::CppOwnership);
        engine.globalObject().setProperty(QStringLiteral("logos"),
                                          engine.newQObject(&fx.bridge));

        constexpr int N = 12;
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
        while (results.property(QStringLiteral("length")).toInt() < N && t.elapsed() < 20000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        QVERIFY2(results.property(QStringLiteral("length")).toInt() == N,
                 qPrintable(QStringLiteral("async handshake chain stalled at %1/%2")
                                .arg(results.property(QStringLiteral("length")).toInt()).arg(N)));
        for (int i = 0; i < N; ++i)
            QCOMPARE(results.property(i).toInt(), i);
    }

    // FAN-OUT async over the real handshake: fire ALL N calls before any
    // completes, so they queue behind ONE requestModule handshake (the
    // first-burst coalescing path). Under token rotation this is the exact
    // scenario the m_pendingHandshakes / token-cache comment describes — if
    // coalescing regressed, later calls carry a superseded token and drop.
    void fanoutAsyncWithHandshake_allComplete()
    {
        Fixture fx;
        fx.subscribeEvents(14);

        QJSEngine engine;
        engine.setObjectOwnership(&fx.bridge, QJSEngine::CppOwnership);
        engine.globalObject().setProperty(QStringLiteral("logos"),
                                          engine.newQObject(&fx.bridge));

        constexpr int N = 12;
        QJSValue driver = engine.evaluate(QStringLiteral(R"JS(
            var __results = [];
            for (var i = 0; i < %1; ++i) {
              logos.callModuleAsync("echo_module", "echo", [i], function (payload) {
                __results.push(JSON.parse(payload));
              });
            }
        )JS").arg(N));
        QVERIFY2(!driver.isError(), qPrintable(driver.toString()));

        QJSValue results = engine.globalObject().property(QStringLiteral("__results"));
        QElapsedTimer t; t.start();
        while (results.property(QStringLiteral("length")).toInt() < N && t.elapsed() < 20000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        QVERIFY2(results.property(QStringLiteral("length")).toInt() == N,
                 qPrintable(QStringLiteral("fan-out handshake dropped calls: %1/%2 (coalescing/rotation)")
                                .arg(results.property(QStringLiteral("length")).toInt()).arg(N)));
    }

    // SYNC chain over the real handshake: each callModule spins a nested loop,
    // and the first also spins a nested loop for requestModule — the reentrancy
    // the token-cache comment in logos_api_client.cpp warns about.
    void syncChainWithHandshake_allComplete()
    {
        Fixture fx;
        fx.subscribeEvents(14);

        QJSEngine engine;
        engine.setObjectOwnership(&fx.bridge, QJSEngine::CppOwnership);
        engine.globalObject().setProperty(QStringLiteral("logos"),
                                          engine.newQObject(&fx.bridge));

        constexpr int N = 12;
        QJSValue driver = engine.evaluate(QStringLiteral(R"JS(
            var __results = [];
            function runSync() {
              for (var i = 0; i < %1; ++i) {
                var r = logos.callModule("echo_module", "echo", [i]);
                __results.push(JSON.parse(r));
              }
            }
            runSync();
        )JS").arg(N));
        QVERIFY2(!driver.isError(), qPrintable(driver.toString()));

        QJSValue results = engine.globalObject().property(QStringLiteral("__results"));
        QVERIFY2(results.property(QStringLiteral("length")).toInt() == N,
                 qPrintable(QStringLiteral("sync handshake chain stalled at %1/%2")
                                .arg(results.property(QStringLiteral("length")).toInt()).arg(N)));
        for (int i = 0; i < N; ++i)
            QCOMPARE(results.property(i).toInt(), i);
    }
};

QTEST_GUILESS_MAIN(TestLogosQmlBridgeHandshake)
#include "test_logos_qml_bridge_handshake.moc"
