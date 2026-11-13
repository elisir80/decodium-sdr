// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/NavtexController.h"
#include "core/NavtexReceiver.h"
#include <QTimer>
#include <stdexcept>
namespace dsdr::core {
NavtexController::NavtexController(NavtexReceiver* r,QObject* parent,PollHook hook,void* context):QObject(parent),m_receiver(r),m_pollHook(hook),m_hookContext(context) {
    if(!r) throw std::invalid_argument("missing NAVTEX receiver");
    auto* timer=new QTimer(this); connect(timer,&QTimer::timeout,this,&NavtexController::poll); timer->start(100);
}
bool NavtexController::enabled() const { return m_receiver && m_receiver->enabled(); }
void NavtexController::setEnabled(bool v) { if(!m_receiver || enabled()==v) return; m_receiver->setEnabled(v); poll(); emit changed(); }
int NavtexController::selectedChannel() const { return m_receiver?m_receiver->selectedChannel():-1; }
void NavtexController::selectChannel(int id) {
    if (!m_receiver) return;
    bool found = false;
    for(const auto& e:m_channels) if(e.toMap().value("id").toInt()==id) found=true;
    const int next=found?id:-1; if(next==selectedChannel()) return;
    m_receiver->selectChannel(next); poll(); emit changed();
}
void NavtexController::setChannels(const QVariantList& c) {
    if (m_channels == c) return;
    m_channels = c; bool found = false;
    for(const auto& e:c) if(e.toMap().value("id").toInt()==selectedChannel()) found=true;
    if(!found && m_receiver) m_receiver->selectChannel(-1);
    emit channelsChanged(); emit changed();
}
void NavtexController::setSourcePresent(bool p) { if(m_sourcePresent==p) return; m_sourcePresent=p; if(m_receiver) m_receiver->discontinuity(); emit changed(); }
void NavtexController::clear() { m_header.clear(); m_text.clear(); m_errors=0; m_truncated=false; }
void NavtexController::detach() noexcept { if(!m_receiver) return; m_receiver=nullptr; clear(); m_sourcePresent=false; emit changed(); }
QString NavtexController::status() const {
    if(!m_receiver) return tr("Ricevitore NAVTEX in arresto");
    if(!enabled()) return tr("Decoder disattivato");
    if(selectedChannel()<0) return tr("Seleziona un canale USB o LSB");
    if(!m_sourcePresent) return tr("Sorgente IQ non disponibile");
    return tr("In ascolto NAVTEX sperimentale");
}
qulonglong NavtexController::droppedBlocks() const { return m_receiver?m_receiver->stats().droppedBlocks:m_blocks; }
qulonglong NavtexController::droppedMessages() const { return m_receiver?m_receiver->droppedMessages():m_dropped; }
void NavtexController::poll() {
    if (!m_receiver) return;
    bool dirty = false;
    auto g=m_receiver->generation(); if(g!=m_generation) { m_generation=g; clear(); dirty=true; }
    NavtexReceiver::Message m; unsigned accepted=0;
    for(int i=0;i<32;++i) {
        auto result=m_receiver->readOne(m); if(result==NavtexReceiver::ReadResult::Empty) break;
        if(result==NavtexReceiver::ReadResult::Stale) continue;
        m_header=QString::fromLatin1(m.header.data()); m_text=QString::fromLatin1(m.text.data());
        m_errors=m.errors; m_truncated=m.truncated; ++accepted; dirty=true;
        if(m_pollHook) m_pollHook(m_hookContext);
    }
    if(m_generation!=m_receiver->generation()) { m_generation=m_receiver->generation(); clear(); dirty=true; }
    else m_count+=accepted;
    if(m_blocks!=droppedBlocks() || m_dropped!=droppedMessages()) { m_blocks=droppedBlocks(); m_dropped=droppedMessages(); dirty=true; }
    if(dirty) emit changed();
}
}
