// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AsyncDsc.h"
#include <QObject>
#include <QString>
#include <QVariantList>
namespace dsc_rx {
// UI thread only; receiver outlives controller. Input source remains external.
class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
    Q_PROPERTY(qulonglong droppedResults READ droppedResults NOTIFY changed)
public:
    explicit Controller(AsyncDsc* receiver, QObject* parent=nullptr);
    bool enabled() const { return enabled_; }
    void setEnabled(bool value);
    QString status() const;
    QVariantList messages() const { return messages_; }
    qulonglong droppedBlocks() const;
    qulonglong droppedResults() const;
    Q_INVOKABLE void poll();
    // Call after the owner reconfigures the receiver.
    void configurationChanged();
signals:
    void changed();
private:
    AsyncDsc* rx_; bool enabled_=false; QVariantList messages_;
    uint64_t generation_=0;
};
}
