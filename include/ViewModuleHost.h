#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class QLocalServer;

class ViewModuleHost : public QObject {
    Q_OBJECT
public:
    explicit ViewModuleHost(QObject* parent = nullptr);
    ~ViewModuleHost();

    bool spawn(const QString& moduleName, const QString& pluginPath,
               const QString& authToken);
    void stop();
    bool isRunning() const;
    QString socketName() const;

signals:
    void processExited(int exitCode);
    void ready();

private:
    QProcess* m_process = nullptr;
    QLocalServer* m_tokenServer = nullptr;
    QString m_moduleName;
    QString m_socketName;
    QByteArray m_stdoutBuffer;
    bool m_readyEmitted = false;
};
