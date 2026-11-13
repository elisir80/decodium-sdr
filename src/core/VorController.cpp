// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/VorController.h"
#include "core/VorAsyncReceiver.h"
#include <QTimer>
#include <stdexcept>
namespace dsdr::core {
VorController::VorController(VorAsyncReceiver* receiver,QObject* parent,PollHook hook,void* hookContext):QObject(parent),m_receiver(receiver),m_pollHook(hook),m_hookContext(hookContext) {
    if(!receiver) throw std::invalid_argument("missing VOR receiver");
    auto* timer=new QTimer(this); connect(timer,&QTimer::timeout,this,&VorController::poll); timer->start(100);
}
void VorController::setEnabled(bool value) {
    if(!m_receiver || m_enabled==value) return;
    m_enabled=value; m_valid=false; m_identifier.clear(); emit configurationRequested(m_enabled,m_input); emit changed();
}
void VorController::setInput(int value) {
    if(!m_receiver || value<0 || value>1 || value==m_input) return;
    m_input=value; m_active=false; m_valid=false; m_identifier.clear(); emit configurationRequested(m_enabled,m_input); emit changed();
}
bool VorController::setFrequencyHz(qint64 hz) {
    if(!m_receiver || hz<=0) return false;
    if(m_frequencyHz!=hz) { m_frequencyHz=hz; m_valid=false; m_identifier.clear(); emit configurationRequested(m_enabled,m_input); emit changed(); }
    return true;
}
void VorController::setActive(bool value) { if(!m_receiver || m_active==value) return; m_active=value; m_valid=false; m_identifier.clear(); emit changed(); }
void VorController::detach() noexcept {
    if(!m_receiver) return;
    m_receiver=nullptr; m_enabled=m_active=m_valid=false; m_identifier.clear(); emit changed();
}
QString VorController::status() const {
    if(!m_receiver) return tr("Ricevitore VOR in arresto");
    if(m_active && !m_enabled) return tr("Arresto richiesto");
    if(!m_enabled) return tr("Disattivato");
    if(!m_active) return tr("Sorgente IQ, frequenza o campionamento non supportati");
    return m_valid ? tr("Stima sperimentale disponibile") : tr("In attesa di un segnale VOR valido");
}
qulonglong VorController::droppedBlocks() const { return m_receiver ? m_receiver->stats().droppedBlocks : m_drops; }
qulonglong VorController::droppedResults() const { return m_receiver ? m_receiver->droppedResults() : m_results; }
void VorController::poll() {
    if(!m_receiver) return;
    bool dirty=false; const auto generation=m_receiver->generation();
    if(generation!=m_generation) { m_generation=generation; m_valid=false; m_identifier.clear(); dirty=true; }
    VorAsyncReceiver::Result r;
    // The receiver keeps only the latest result. Read at most one per GUI
    // tick, even if the worker publishes again while this callback runs.
    if(m_receiver->read(r) && r.generation==m_receiver->generation()) {
        m_valid=m_active && m_enabled && r.value.valid; m_radial=r.value.radialDegrees;
        const auto candidate=QString::fromLatin1(r.value.identifier.data());
        m_identifier=m_valid && candidate.size()>=2 ? candidate : QString(); dirty=true;
        if(m_pollHook) m_pollHook(m_hookContext);
    }
    const auto after=m_receiver->generation();
    if(after!=m_generation) { m_generation=after; m_valid=false; m_identifier.clear(); dirty=true; }
    auto drops=droppedBlocks(), results=droppedResults();
    if(drops!=m_drops || results!=m_results) { m_drops=drops; m_results=results; dirty=true; }
    if(dirty) emit changed();
}
}
