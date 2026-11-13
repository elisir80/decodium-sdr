// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SelcalController.h"
#include "core/SelcalReceiver.h"
#include <QTimer>
#include <stdexcept>
namespace dsdr::core {
SelcalController::SelcalController(SelcalReceiver *receiver,QObject *parent,PollHook hook,void* context):QObject(parent),m_receiver(receiver),m_pollHook(hook),m_hookContext(context) {
    if(!receiver) throw std::invalid_argument("missing SELCAL receiver");
    auto *timer=new QTimer(this); connect(timer,&QTimer::timeout,this,&SelcalController::poll); timer->start(100);
}
bool SelcalController::enabled() const { return m_receiver && m_receiver->enabled(); }
void SelcalController::setEnabled(bool value) { if(!m_receiver || enabled()==value) return; m_receiver->setEnabled(value); poll(); emit changed(); }
int SelcalController::selectedChannel() const { return m_receiver ? m_receiver->selectedChannel() : -1; }
void SelcalController::selectChannel(int id) {
    if(!m_receiver) return;
    bool found=false; for(const auto &entry:m_channels) if(entry.toMap().value("id").toInt()==id) found=true;
    const int next=found?id:-1; if(next==selectedChannel()) return;
    m_receiver->selectChannel(next); poll(); emit changed();
}
void SelcalController::setChannels(const QVariantList &channels) {
    if(m_channels==channels) return;
    m_channels=channels;
    bool found=false; for(const auto &entry:m_channels) if(entry.toMap().value("id").toInt()==selectedChannel()) found=true;
    if(!found && m_receiver) m_receiver->selectChannel(-1);
    emit channelsChanged(); emit changed();
}
void SelcalController::setSourcePresent(bool present) {
    if(m_sourcePresent==present) return;
    m_sourcePresent=present; if(m_receiver) m_receiver->discontinuity(); emit changed();
}
void SelcalController::detach() noexcept { if(!m_receiver) return; m_receiver=nullptr; m_lastCode.clear(); m_sourcePresent=false; emit changed(); }
QString SelcalController::status() const {
    if(!m_receiver) return tr("Ricevitore SELCAL in arresto");
    if(!enabled()) return tr("Decoder disattivato");
    if(selectedChannel()<0) return tr("Seleziona un canale AM o SSB");
    if(!m_sourcePresent) return tr("Sorgente IQ non disponibile");
    return tr("Ricezione SELCAL attiva");
}
qulonglong SelcalController::droppedMessages() const { return m_receiver ? m_receiver->droppedMessages() : m_dropped; }
qulonglong SelcalController::droppedBlocks() const { return m_receiver ? m_receiver->stats().droppedBlocks : m_blocks; }
void SelcalController::poll() {
    if(!m_receiver) return;
    bool dirty=false; const auto generation=m_receiver->generation();
    if(generation!=m_generation) { m_generation=generation; m_lastCode.clear(); dirty=true; }
    SelcalReceiver::Message message; unsigned accepted=0;
    for(int i=0;i<32;++i) {
        const auto result=m_receiver->readOne(message);
        if(result==SelcalReceiver::ReadResult::Empty) break;
        if(result==SelcalReceiver::ReadResult::Stale) continue;
        m_lastCode=QString::fromLatin1(message.code.data(),message.code.size()); ++accepted; dirty=true;
        if(m_pollHook) m_pollHook(m_hookContext);
    }
    if(m_generation!=m_receiver->generation()) { m_generation=m_receiver->generation(); m_lastCode.clear(); dirty=true; }
    else m_count+=accepted;
    if(m_dropped!=droppedMessages() || m_blocks!=droppedBlocks()) { m_dropped=droppedMessages(); m_blocks=droppedBlocks(); dirty=true; }
    if(dirty) emit changed();
}
}
