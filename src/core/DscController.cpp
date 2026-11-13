// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DscController.h"
#include "core/DscReceiver.h"
#include <QTimer>
#include <stdexcept>
namespace dsdr::core {
DscController::DscController(DscReceiver* r,QObject* parent,PollHook hook,void* hookContext):QObject(parent),m_receiver(r),m_pollHook(hook),m_hookContext(hookContext) {
    if(!r) throw std::invalid_argument("missing DSC receiver");
    auto* timer=new QTimer(this); connect(timer,&QTimer::timeout,this,&DscController::poll); timer->start(100);
}
bool DscController::enabled() const { return m_receiver && m_receiver->enabled(); }
void DscController::setEnabled(bool v) { if(!m_receiver || enabled()==v) return; m_receiver->setEnabled(v); poll(); emit changed(); }
int DscController::selectedChannel() const { return m_receiver ? m_receiver->selectedChannel() : -1; }
void DscController::selectChannel(int id) {
    if (!m_receiver) return;
    bool found = false;
    for(const auto& e:m_channels) if(e.toMap().value("id").toInt()==id) found=true;
    int next=found?id:-1; if(next==selectedChannel()) return;
    m_receiver->selectChannel(next); poll(); emit changed();
}
void DscController::setChannels(const QVariantList& c) {
    if (m_channels == c) return;
    m_channels = c; bool found = false;
    for(const auto& e:c) if(e.toMap().value("id").toInt()==selectedChannel()) found=true;
    if(!found && m_receiver) m_receiver->selectChannel(-1);
    emit channelsChanged(); emit changed();
}
void DscController::setSourcePresent(bool p) { if(m_sourcePresent==p) return; m_sourcePresent=p; if(m_receiver) m_receiver->discontinuity(); emit changed(); }
void DscController::detach() noexcept { if(!m_receiver) return; m_receiver=nullptr; m_lastMmsi.clear(); m_lastFormat.clear(); m_lastCategory.clear(); m_lastSequence.clear(); m_sourcePresent=false; emit changed(); }
QString DscController::status() const {
    if(!m_receiver) return tr("Ricevitore DSC in arresto");
    if(!enabled()) return tr("Decoder disattivato");
    if(selectedChannel()<0) return tr("Seleziona un canale USB o LSB");
    if(!m_sourcePresent) return tr("Sorgente IQ non disponibile");
    return tr("In ascolto DSC MF/HF sperimentale");
}
qulonglong DscController::droppedBlocks() const { return m_receiver ? m_receiver->stats().droppedBlocks : m_blocks; }
qulonglong DscController::droppedMessages() const { return m_receiver ? m_receiver->droppedMessages() : m_dropped; }
void DscController::poll() {
    if (!m_receiver) return;
    bool dirty = false;
    auto g=m_receiver->generation(); if(g!=m_generation) { m_generation=g; m_lastMmsi.clear(); m_lastFormat.clear(); m_lastCategory.clear(); m_lastSequence.clear(); dirty=true; }
    DscReceiver::Message m; unsigned accepted=0;
    for(int i=0;i<32;++i) {
        auto r=m_receiver->readOne(m); if(r==DscReceiver::ReadResult::Empty) break;
        if(r==DscReceiver::ReadResult::Stale) continue;
        QString digits; for(int j=0;j<5;++j) digits+=QStringLiteral("%1").arg(m.call.fullBody[m.call.sourceOffset+j],2,10,QLatin1Char('0'));
        digits.chop(1); m_lastMmsi=digits;
        const auto format=m.call.fullBody[0];
        const int category=format==112 ? 112 : m.call.fullBody[format==116 ? 1 : 6];
        switch(category) {
        case 112: m_lastCategory=tr("Soccorso"); break;
        case 110: m_lastCategory=tr("Urgenza"); break;
        case 108: m_lastCategory=tr("Sicurezza"); break;
        case 106: m_lastCategory=tr("Servizio nave"); break;
        case 100: m_lastCategory=tr("Routine"); break;
        default: m_lastCategory=tr("Sconosciuta"); break;
        }
        switch(m.call.eos) {
        case 117: m_lastSequence=tr("Riscontro richiesto"); break;
        case 122: m_lastSequence=tr("Riscontro"); break;
        case 127: m_lastSequence=tr("Fine chiamata"); break;
        default: m_lastSequence=tr("Sconosciuta"); break;
        }
        if(m.call.bodyLength==27 && m.call.fullBody[12]==110)
            m_lastFormat=tr("Riscontro rilancio soccorso");
        else if(m.call.bodyLength==27 && m.call.fullBody[12]==112)
            m_lastFormat=tr("Rilancio soccorso indirizzato");
        else switch(m.call.fullBody[0]) {
        case 112: m_lastFormat=tr("Soccorso"); break;
        case 116:
            if(m.call.bodyLength==22 && m.call.fullBody[7]==110) m_lastFormat=tr("Riscontro soccorso");
            else if(m.call.bodyLength==22 && m.call.fullBody[7]==112) m_lastFormat=tr("Rilancio soccorso");
            else m_lastFormat=tr("Tutte le navi");
            break;
        case 120: m_lastFormat=tr("Individuale"); break;
        case 123: m_lastFormat=tr("Individuale automatico"); break;
        case 114: m_lastFormat=tr("Gruppo"); break;
        case 102: m_lastFormat=tr("Area geografica"); break;
        default: m_lastFormat=tr("Sconosciuto"); break;
        }
        ++accepted; dirty=true;
        if(m_pollHook) m_pollHook(m_hookContext);
    }
    if(m_generation!=m_receiver->generation()) { m_generation=m_receiver->generation(); m_lastMmsi.clear(); m_lastFormat.clear(); m_lastCategory.clear(); m_lastSequence.clear(); dirty=true; }
    else m_count+=accepted;
    if(m_blocks!=droppedBlocks() || m_dropped!=droppedMessages()) { m_blocks=droppedBlocks(); m_dropped=droppedMessages(); dirty=true; }
    if(dirty) emit changed();
}
}
