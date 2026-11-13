// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariantList>
#include <cstdint>
namespace dsdr::core {
class DscReceiver;
class DscController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int selectedChannel READ selectedChannel WRITE selectChannel NOTIFY changed)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastMmsi READ lastMmsi NOTIFY changed)
    Q_PROPERTY(QString lastFormat READ lastFormat NOTIFY changed)
    Q_PROPERTY(QString lastCategory READ lastCategory NOTIFY changed)
    Q_PROPERTY(QString lastSequence READ lastSequence NOTIFY changed)
    Q_PROPERTY(qulonglong messageCount READ messageCount NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
    Q_PROPERTY(qulonglong droppedMessages READ droppedMessages NOTIFY changed)
public:
    using PollHook = void (*)(void*) noexcept;
    explicit DscController(DscReceiver*,QObject* parent=nullptr,PollHook hook=nullptr,void* hookContext=nullptr);
    bool enabled() const; void setEnabled(bool);
    int selectedChannel() const; Q_INVOKABLE void selectChannel(int);
    QVariantList channels() const { return m_channels; }
    void setChannels(const QVariantList&); void setSourcePresent(bool); void detach() noexcept;
    QString status() const; QString lastMmsi() const { return m_lastMmsi; }
    QString lastFormat() const { return m_lastFormat; }
    QString lastCategory() const { return m_lastCategory; }
    QString lastSequence() const { return m_lastSequence; }
    qulonglong messageCount() const { return m_count; }
    qulonglong droppedBlocks() const; qulonglong droppedMessages() const;
public slots: void poll();
signals: void changed(); void channelsChanged();
private:
    DscReceiver* m_receiver; QVariantList m_channels; QString m_lastMmsi,m_lastFormat,m_lastCategory,m_lastSequence;
    PollHook m_pollHook; void* m_hookContext;
    bool m_sourcePresent=false; std::uint64_t m_generation=0;
    qulonglong m_count=0,m_blocks=0,m_dropped=0;
};
}
