// SPDX-License-Identifier: GPL-3.0-or-later
#include "DscController.h"
#include <QVariantMap>
#include <QTimer>
namespace dsc_rx {
Controller::Controller(AsyncDsc* r, QObject* parent):QObject(parent),rx_(r) {
    if (rx_) generation_=rx_->stats().generation;
    auto* timer = new QTimer(this); timer->setInterval(200);
    connect(timer, &QTimer::timeout, this, &Controller::poll); timer->start();
}
void Controller::setEnabled(bool v) {
    if (enabled_ == v) return;
    enabled_=v;
    if (rx_) { if(v) rx_->start(); else rx_->stop(false); }
    emit changed();
}
QString Controller::status() const {
    if (!enabled_) return tr("DSC disabilitato — supporto distress MF/HF parziale.");
    if (!rx_ || !rx_->active()) return tr("In attesa di sorgente audio configurata — distress MF/HF parziale.");
    return tr("In ascolto su audio 24 kHz / 1700 Hz — distress MF/HF parziale.");
}
qulonglong Controller::droppedBlocks() const {return rx_ ? rx_->stats().droppedBlocks : 0;}
qulonglong Controller::droppedResults() const {return rx_ ? rx_->stats().droppedResults : 0;}
void Controller::configurationChanged() {
    if (!rx_) return;
    auto generation=rx_->stats().generation;
    if (generation_ != generation) { messages_.clear(); generation_=generation; }
    if (enabled_ && !rx_->active()) rx_->start();
    emit changed();
}
void Controller::poll() {
    if (!rx_) return;
    auto generation=rx_->stats().generation;
    if (generation_ != generation) { messages_.clear(); generation_=generation; }
    Distress d;
    while (rx_->poll(d)) {
        QString digits;
        for (int i=1;i<=5;++i) digits+=QStringLiteral("%1").arg(d.body[i],2,10,QLatin1Char('0'));
        digits.chop(1);
        QString position;
        for (int i=7;i<=11;++i) position+=QStringLiteral("%1").arg(d.body[i],2,10,QLatin1Char('0'));
        QVariantMap m{{"mmsi",digits},{"nature",d.body[6]},{"positionRaw",position},
                      {"utcRaw",QStringLiteral("%1%2").arg(d.body[12],2,10,QLatin1Char('0')).arg(d.body[13],2,10,QLatin1Char('0'))},
                      {"subsequent",d.body[14]},{"repaired",d.repaired},{"eos",d.eos}};
        messages_.append(m); if (messages_.size()>100) messages_.removeFirst();
    }
    emit changed();
}
}
