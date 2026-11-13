// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include "core/AprsPosition.h"
namespace dsdr::core {
class AprsReceiver;
// UI-thread only; receiver must outlive the controller.
class AprsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int selectedChannel READ selectedChannel WRITE selectChannel NOTIFY changed)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QString destination READ destination NOTIFY changed)
    Q_PROPERTY(QString information READ information NOTIFY changed)
    Q_PROPERTY(bool hasPosition READ hasPosition NOTIFY changed)
    Q_PROPERTY(double latitude READ latitude NOTIFY changed)
    Q_PROPERTY(double longitude READ longitude NOTIFY changed)
    Q_PROPERTY(QString positionObject READ positionObject NOTIFY changed)
    Q_PROPERTY(QString lastFrame READ lastFrame NOTIFY changed)
    Q_PROPERTY(qulonglong frameCount READ frameCount NOTIFY changed)
    Q_PROPERTY(qulonglong droppedFrames READ droppedFrames NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
public:
    using PollHook = void (*)(void*) noexcept;
    explicit AprsController(AprsReceiver *receiver, QObject *parent = nullptr, PollHook hook=nullptr, void* hookContext=nullptr);
    // UI-thread only. Stop polling the receiver before its asynchronous teardown.
    void detach();
    bool enabled() const;
    void setEnabled(bool value);
    int selectedChannel() const;
    Q_INVOKABLE void selectChannel(int id);
    QVariantList channels() const { return m_channels; }
    // UI integration supplies entries {id, label}; include only NFM channels.
    void setChannels(const QVariantList &channels);
    QString status() const;
    QString source() const { return m_source; }
    QString destination() const { return m_destination; }
    QString information() const { return m_information; }
    bool hasPosition() const { return m_position.has_value(); }
    double latitude() const { return m_position ? m_position->latitude : 0; }
    double longitude() const { return m_position ? m_position->longitude : 0; }
    QString positionObject() const { return m_position ? m_position->objectName : QString(); }
    QString lastFrame() const { return m_lastFrame; }
    qulonglong frameCount() const { return m_count; }
    qulonglong droppedFrames() const;
    qulonglong droppedBlocks() const;
public slots:
    void poll();
signals:
    void changed();
    void channelsChanged();
private:
    AprsReceiver *m_receiver;
    PollHook m_pollHook; void* m_hookContext;
    QVariantList m_channels;
    QString m_source, m_destination, m_information, m_lastFrame, m_previousStatus;
    std::optional<AprsPosition> m_position;
    qulonglong m_count = 0, m_oldDroppedFrames = 0, m_oldDroppedBlocks = 0;
    std::uint64_t m_generation = 0;
};
}
