// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <cstdint>
namespace dsdr::core {
class SelcalReceiver;
class SelcalController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int selectedChannel READ selectedChannel WRITE selectChannel NOTIFY changed)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastCode READ lastCode NOTIFY changed)
    Q_PROPERTY(qulonglong messageCount READ messageCount NOTIFY changed)
    Q_PROPERTY(qulonglong droppedMessages READ droppedMessages NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
public:
    using PollHook=void (*)(void*) noexcept;
    explicit SelcalController(SelcalReceiver *,QObject *parent=nullptr,PollHook hook=nullptr,void* context=nullptr);
    bool enabled() const;
    void setEnabled(bool);
    int selectedChannel() const;
    Q_INVOKABLE void selectChannel(int);
    QVariantList channels() const { return m_channels; }
    void setChannels(const QVariantList &);
    void setSourcePresent(bool);
    void detach() noexcept;
    QString status() const;
    QString lastCode() const { return m_lastCode; }
    qulonglong messageCount() const { return m_count; }
    qulonglong droppedMessages() const;
    qulonglong droppedBlocks() const;
public slots:
    void poll();
signals:
    void changed();
    void channelsChanged();
private:
    SelcalReceiver *m_receiver;
    PollHook m_pollHook; void* m_hookContext;
    QVariantList m_channels;
    QString m_lastCode;
    bool m_sourcePresent=false;
    std::uint64_t m_generation=0;
    qulonglong m_count=0,m_dropped=0,m_blocks=0;
};
}
