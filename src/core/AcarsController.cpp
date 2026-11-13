// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AcarsController.h"
#include "core/AcarsReceiver.h"
#include <QTimer>
#include <stdexcept>
namespace dsdr::core {
int AcarsHistoryModel::rowCount(const QModelIndex &parent) const { return parent.isValid()?0:int(m_entries.size()); }
QVariant AcarsHistoryModel::data(const QModelIndex &index,int role) const {
    if(!index.isValid() || index.row()<0 || index.row()>=int(m_entries.size()) || index.column()!=0) return {};
    const auto &e=m_entries[std::size_t(index.row())];
    switch(role) { case ModeRole:return e.mode; case AddressRole:return e.address; case AckRole:return e.ack;
    case LabelRole:return e.label; case BlockIdRole:return e.blockId; case TextRole:return e.text; case EndingRole:return e.ending; default:return {}; }
}
QHash<int,QByteArray> AcarsHistoryModel::roleNames() const {
    return {{ModeRole,"mode"},{AddressRole,"address"},{AckRole,"ack"},{LabelRole,"label"},
            {BlockIdRole,"blockId"},{TextRole,"messageText"},{EndingRole,"ending"}};
}
void AcarsHistoryModel::append(const Entry &e) {
    if(int(m_entries.size())==Capacity) { beginRemoveRows({},0,0); m_entries.pop_front(); endRemoveRows(); }
    const int row=int(m_entries.size()); beginInsertRows({},row,row); m_entries.push_back(e); endInsertRows();
}
void AcarsHistoryModel::clear() { if(m_entries.empty()) return; beginResetModel(); m_entries.clear(); endResetModel(); }
namespace {
QString character(unsigned byte) {
    const unsigned c=byte&0x7f;
    if(c>=0x20 && c<=0x7e) return QString(QChar(ushort(c)));
    return QStringLiteral("<%1>").arg(c,2,16,QLatin1Char('0')).toUpper();
}
QString field(const AcarsReceiver::Message &m,std::size_t begin,std::size_t end) {
    QString s; for(auto i=begin;i<end && i<m.size;++i) s+=character(m.bytes[i]); return s;
}
}
AcarsController::AcarsController(AcarsReceiver *receiver,QObject *parent) : QObject(parent),m_receiver(receiver) {
    if(!receiver) throw std::invalid_argument("ACARS receiver required");
    auto *timer=new QTimer(this); connect(timer,&QTimer::timeout,this,&AcarsController::poll); timer->start(100);
}
bool AcarsController::enabled() const { return m_receiver && m_receiver->status()!=AcarsReceiver::Status::Disabled; }
void AcarsController::setEnabled(bool value) { if(!m_receiver || enabled()==value) return; m_receiver->setEnabled(value); poll(); emit changed(); }
void AcarsController::detach() noexcept { if(!m_receiver) return; m_receiver=nullptr; m_messages.clear(); emit changed(); }
bool AcarsController::setFrequencyHz(qint64 hz) {
    if(!m_receiver || hz<=0) return false;
    if(m_frequencyHz!=hz) { m_frequencyHz=hz; emit frequencyChanged(); emit changed(); }
    return true;
}
QString AcarsController::status() const {
    if(!m_receiver) return tr("Ricevitore ACARS in arresto");
    switch(m_receiver->status()) {
    case AcarsReceiver::Status::Disabled:return tr("Decoder disattivato");
    case AcarsReceiver::Status::Stopped:return tr("Ricevitore ACARS non avviato");
    case AcarsReceiver::Status::NoSource:return tr("Sorgente IQ non disponibile");
    case AcarsReceiver::Status::WrongRate:return tr("Frequenza di campionamento non supportata");
    case AcarsReceiver::Status::WrongOffset:return tr("Canale ACARS fuori dalla banda disponibile");
    case AcarsReceiver::Status::Active:return tr("Ricezione ACARS attiva");
    } return {};
}
qulonglong AcarsController::droppedMessages() const { return m_receiver ? m_receiver->droppedMessages() : m_oldMessages; }
qulonglong AcarsController::droppedBlocks() const { return m_receiver ? m_receiver->stats().droppedBlocks : m_oldBlocks; }
qulonglong AcarsController::workerErrors() const { return m_receiver ? m_receiver->workerErrors() : m_oldErrors; }
void AcarsController::clearMessages() { m_messages.clear(); }
void AcarsController::poll() {
    if(!m_receiver) return;
    bool dirty=false;
    auto generation=m_receiver->generation();
    if(generation!=m_generation) { m_generation=generation; m_messages.clear(); dirty=true; }
    AcarsReceiver::Message msg; unsigned accepted=0;
    for(int i=0;i<PollBatch;++i) {
        const auto result=m_receiver->readOne(msg);
        if(result==AcarsReceiver::ReadResult::Empty) break;
        if(result==AcarsReceiver::ReadResult::Stale) continue;
        if(msg.generation!=m_receiver->generation() || msg.size<13 || msg.size>msg.bytes.size()) continue;
        AcarsHistoryModel::Entry e;
        e.mode=field(msg,0,1); e.address=field(msg,1,8); e.ack=field(msg,8,9); e.label=field(msg,9,11); e.blockId=field(msg,11,12);
        if(msg.size>13 && (msg.bytes[12]&0x7f)==2) e.text=field(msg,13,msg.size-1);
        e.ending=(msg.bytes[msg.size-1]&0x7f)==3 ? QStringLiteral("ETX") : QStringLiteral("ETB");
        m_messages.append(e); ++accepted; dirty=true;
    }
    generation=m_receiver->generation();
    if(generation!=m_generation) { m_generation=generation; m_messages.clear(); dirty=true; }
    else m_count+=accepted;
    const auto current=status();
    if(current!=m_oldStatus || droppedMessages()!=m_oldMessages || droppedBlocks()!=m_oldBlocks || workerErrors()!=m_oldErrors) {
        m_oldStatus=current; m_oldMessages=droppedMessages(); m_oldBlocks=droppedBlocks(); m_oldErrors=workerErrors(); dirty=true;
    }
    if(dirty) emit changed();
}
}
