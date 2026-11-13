// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <cstdint>
namespace dsdr::core {
class AisReceiver;
// UI-thread only; receiver must outlive controller, including its timer.
class AisController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY channelChanged)
    Q_PROPERTY(int messageType READ messageType NOTIFY changed)
    Q_PROPERTY(QString mmsi READ mmsi NOTIFY changed)
    Q_PROPERTY(bool hasPosition READ hasPosition NOTIFY changed)
    Q_PROPERTY(double latitude READ latitude NOTIFY changed)
    Q_PROPERTY(double longitude READ longitude NOTIFY changed)
    Q_PROPERTY(QString processedUtc READ processedUtc NOTIFY changed)
    Q_PROPERTY(qulonglong frameCount READ frameCount NOTIFY changed)
    Q_PROPERTY(qulonglong droppedMessages READ droppedMessages NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
    Q_PROPERTY(qulonglong workerErrors READ workerErrors NOTIFY changed)
public:
    static constexpr int PollBatch = 64;
    using PollHook = void (*)(void*) noexcept;
    explicit AisController(AisReceiver *receiver, QObject *parent = nullptr, PollHook hook=nullptr, void* hookContext=nullptr);
    bool enabled() const;
    void setEnabled(bool enabled);
    QString status() const;
    int channel() const { return m_channel; }
    void setChannel(int channel);
    void detach() noexcept;
    int messageType() const { return m_type; }
    QString mmsi() const { return m_mmsi; }
    bool hasPosition() const { return m_hasPosition; }
    double latitude() const { return m_latitude; }
    double longitude() const { return m_longitude; }
    QString processedUtc() const { return m_processedUtc; }
    qulonglong frameCount() const { return m_count; }
    qulonglong droppedMessages() const;
    qulonglong droppedBlocks() const;
    qulonglong workerErrors() const;
public slots:
    void poll();
signals:
    void changed();
    void channelChanged();
private:
    void clearMessage();
    AisReceiver *m_receiver;
    PollHook m_pollHook; void* m_hookContext;
    int m_type = -1, m_channel = 0;
    QString m_mmsi, m_previousStatus, m_processedUtc;
    bool m_hasPosition = false;
    double m_latitude = 0, m_longitude = 0;
    std::uint64_t m_generation = 0;
    qulonglong m_count = 0, m_oldMessages = 0, m_oldBlocks = 0, m_oldErrors = 0;
};
}
