// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2Controller.h"
#include "Vdl2Session.h"
#include "Vdl2IqIngress.h"
namespace dsdr::core {
Vdl2Controller::Vdl2Controller(Vdl2Session* session,QObject* parent,Vdl2IqIngress* ingress)
    :QObject(parent),m_session(session),m_ingress(ingress),m_model(this) {
    m_timer.setInterval(100);
    connect(&m_timer,&QTimer::timeout,this,&Vdl2Controller::poll);
    if(m_session) m_timer.start();
}
qulonglong Vdl2Controller::droppedResults() const noexcept { return m_session?m_session->droppedResults():0; }
qulonglong Vdl2Controller::droppedBlocks() const noexcept { return m_session?m_session->stats().droppedBlocks:0; }
qulonglong Vdl2Controller::workerErrors() const noexcept { return m_session?m_session->errors():0; }
QString Vdl2Controller::sourceStatus() const {
    if(!m_session) return tr("Decoder scollegato");
    if(!m_ingress) return tr("Stato sorgente non disponibile");
    switch(m_ingress->status()) {
    case Vdl2IqIngress::Status::Waiting: return tr("In attesa della sorgente IQ");
    case Vdl2IqIngress::Status::Active: return tr("Sorgente IQ attiva");
    case Vdl2IqIngress::Status::NoInput: return tr("Ingresso IQ non disponibile");
    case Vdl2IqIngress::Status::WrongRate: return tr("Frequenza di campionamento cambiata: riconfigurare il decoder");
    case Vdl2IqIngress::Status::WrongCenter: return tr("Frequenza centrale cambiata: riconfigurare il decoder");
    }
    return tr("Stato sorgente non disponibile");
}
void Vdl2Controller::detach() noexcept { m_timer.stop(); m_session=nullptr; m_ingress=nullptr; m_model.setGeneration(0); emit changed(); }
void Vdl2Controller::poll() {
    if(!m_session) return;
    m_model.setGeneration(m_session->generation());
    Vdl2ResultQueue::Result result;
    for(int i=0;i<PollBatch && m_session->read(result);++i)
        m_model.append(result,m_session->generation());
    const auto a=droppedResults(),b=droppedBlocks(),c=workerErrors();
    const auto status=sourceStatus();
    if(a!=m_lastResults || b!=m_lastBlocks || c!=m_lastErrors || status!=m_lastStatus) {
        m_lastResults=a; m_lastBlocks=b; m_lastErrors=c; m_lastStatus=status; emit changed();
    }
}
}
