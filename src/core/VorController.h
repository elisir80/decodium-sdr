// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <cstdint>
namespace dsdr::core {
class VorAsyncReceiver;
// UI thread only. Host owns receiver and serializes producer/lifecycle changes.
class VorController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int input READ input WRITE setInput NOTIFY changed)
    Q_PROPERTY(qint64 frequencyHz READ frequencyHz NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(double radial READ radial NOTIFY changed)
    Q_PROPERTY(QString identifier READ identifier NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
    Q_PROPERTY(qulonglong droppedResults READ droppedResults NOTIFY changed)
public:
    using PollHook = void (*)(void*) noexcept;
    explicit VorController(VorAsyncReceiver*,QObject* parent=nullptr,PollHook hook=nullptr,void* hookContext=nullptr);
    bool enabled() const { return m_enabled; }
    int input() const { return m_input; }
    bool active() const { return m_active; }
    bool valid() const { return m_valid; }
    double radial() const { return m_radial; }
    QString identifier() const { return m_identifier; }
    QString status() const;
    qulonglong droppedBlocks() const;
    qulonglong droppedResults() const;
    void setEnabled(bool);
    void setInput(int);
    void detach() noexcept;
    qint64 frequencyHz() const { return m_frequencyHz; }
    Q_INVOKABLE bool setFrequencyHz(qint64 hz);
    // Host calls only after the requested source is attached/detached.
    void setActive(bool);
public slots:
    void poll();
signals:
    void changed();
    void configurationRequested(bool enabled,int input);
private:
    VorAsyncReceiver* m_receiver;
    PollHook m_pollHook;
    void* m_hookContext;
    bool m_enabled=false,m_active=false,m_valid=false;
    int m_input=1; double m_radial=0;
    qint64 m_frequencyHz=0;
    std::uint64_t m_generation=0,m_drops=0,m_results=0;
    QString m_identifier;
};
}
