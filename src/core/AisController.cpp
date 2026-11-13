// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AisController.h"
#include "core/AisReceiver.h"
#include <QTimer>
#include <QDateTime>
namespace dsdr::core {
AisController::AisController(AisReceiver *receiver,QObject *parent,PollHook hook,void* hookContext):QObject(parent),m_receiver(receiver),m_pollHook(hook),m_hookContext(hookContext) {
    auto *timer=new QTimer(this); connect(timer,&QTimer::timeout,this,&AisController::poll); timer->start(100);
}
bool AisController::enabled() const { return m_receiver && m_receiver->enabled(); }
void AisController::setEnabled(bool value) { if (!m_receiver) return; m_receiver->setEnabled(value); poll(); emit changed(); }
void AisController::setChannel(int channel) {
    if (!m_receiver || (channel != 0 && channel != 1) || channel == m_channel) return;
    m_receiver->discontinuity(); m_channel=channel; clearMessage(); emit channelChanged(); emit changed();
}
void AisController::detach() noexcept { m_receiver=nullptr; clearMessage(); emit changed(); }
QString AisController::status() const {
    if (!m_receiver) return tr("Ricevitore AIS in arresto");
    switch(m_receiver->status()) {
    case AisReceiver::Status::Disabled: return tr("Decoder disattivato");
    case AisReceiver::Status::Stopped: return tr("Ricevitore AIS non avviato");
    case AisReceiver::Status::NoSource: return tr("Sorgente IQ non disponibile");
    case AisReceiver::Status::WrongRate: return tr("Frequenza di campionamento non supportata");
    case AisReceiver::Status::WrongOffset: return tr("Canale AIS fuori dalla banda disponibile");
    case AisReceiver::Status::Active: return tr("Ricezione AIS attiva");
    }
    return {};
}
qulonglong AisController::droppedMessages() const { return m_receiver ? m_receiver->droppedMessages() : m_oldMessages; }
qulonglong AisController::droppedBlocks() const { return m_receiver ? m_receiver->stats().droppedBlocks : m_oldBlocks; }
qulonglong AisController::workerErrors() const { return m_receiver ? m_receiver->workerErrors() : m_oldErrors; }
void AisController::clearMessage() { m_type=-1; m_mmsi.clear(); m_processedUtc.clear(); m_hasPosition=false; m_latitude=m_longitude=0; }
void AisController::poll() {
    if (!m_receiver) return;
    bool dirty=false;
    const auto generation=m_receiver->generation();
    if(generation!=m_generation) { m_generation=generation; clearMessage(); dirty=true; }
    AisReceiver::Message m; unsigned accepted=0;
    for (int i=0;i<PollBatch;++i) {
        const auto result=m_receiver->readOne(m);
        if(result==AisReceiver::ReadResult::Empty) break;
        if(result==AisReceiver::ReadResult::Stale) continue;
        if(m.generation!=m_receiver->generation()) continue;
        m_type=int(m.type); m_mmsi=QStringLiteral("%1").arg(m.mmsi,9,10,QLatin1Char('0'));
        m_hasPosition=m.hasPosition; m_latitude=m.latitude; m_longitude=m.longitude;
        m_processedUtc=QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        ++accepted; dirty=true;
        if(m_pollHook) m_pollHook(m_hookContext);
    }
    const auto after=m_receiver->generation();
    if(after!=m_generation) { m_generation=after; clearMessage(); dirty=true; }
    else m_count+=accepted;
    const auto current=status();
    if(current!=m_previousStatus || droppedMessages()!=m_oldMessages || droppedBlocks()!=m_oldBlocks || workerErrors()!=m_oldErrors) {
        m_previousStatus=current; m_oldMessages=droppedMessages(); m_oldBlocks=droppedBlocks(); m_oldErrors=workerErrors(); dirty=true;
    }
    if(dirty) emit changed();
}
}
