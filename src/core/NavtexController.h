// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariantList>
#include <cstdint>
namespace dsdr::core {
class NavtexReceiver;
class NavtexController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int selectedChannel READ selectedChannel WRITE selectChannel NOTIFY changed)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString header READ header NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(bool truncated READ truncated NOTIFY changed)
    Q_PROPERTY(unsigned errors READ errors NOTIFY changed)
    Q_PROPERTY(qulonglong messageCount READ messageCount NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
    Q_PROPERTY(qulonglong droppedMessages READ droppedMessages NOTIFY changed)
public:
    using PollHook=void (*)(void*) noexcept;
    explicit NavtexController(NavtexReceiver*,QObject* parent=nullptr,PollHook hook=nullptr,void* context=nullptr);
    bool enabled() const; void setEnabled(bool);
    int selectedChannel() const; Q_INVOKABLE void selectChannel(int);
    QVariantList channels() const { return m_channels; }
    void setChannels(const QVariantList&); void setSourcePresent(bool); void detach() noexcept;
    QString status() const; QString header() const { return m_header; } QString text() const { return m_text; }
    bool truncated() const { return m_truncated; } unsigned errors() const { return m_errors; }
    qulonglong messageCount() const { return m_count; }
    qulonglong droppedBlocks() const; qulonglong droppedMessages() const;
public slots: void poll();
signals: void changed(); void channelsChanged();
private:
    void clear();
    NavtexReceiver* m_receiver; QVariantList m_channels; QString m_header,m_text;
    PollHook m_pollHook; void* m_hookContext;
    bool m_sourcePresent=false,m_truncated=false; unsigned m_errors=0;
    std::uint64_t m_generation=0; qulonglong m_count=0,m_blocks=0,m_dropped=0;
};
}
