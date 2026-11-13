// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2MessageModel.h"
#include "dsp/vdl2/Vdl2Frame.h"
namespace dsdr::core {
Vdl2MessageModel::Vdl2MessageModel(QObject* parent):QAbstractListModel(parent) {}
int Vdl2MessageModel::rowCount(const QModelIndex& parent) const { return parent.isValid()?0:int(m_items.size()); }
QVariant Vdl2MessageModel::data(const QModelIndex& index,int role) const {
    if(!index.isValid() || index.row()<0 || std::size_t(index.row())>=m_items.size()) return {};
    const auto& m=m_items[std::size_t(index.row())];
    switch(role) { case SourceRole:return m.source; case DestinationRole:return m.destination;
    case KindRole:return m.kind; case ProtocolRole:return m.protocol; case SummaryRole:return m.summary; default:return {}; }
}
QHash<int,QByteArray> Vdl2MessageModel::roleNames() const {
    return {{SourceRole,"source"},{DestinationRole,"destination"},{KindRole,"kind"},
            {ProtocolRole,"protocol"},{SummaryRole,"summary"}};
}
void Vdl2MessageModel::setGeneration(std::uint64_t generation) {
    if(generation==m_generation) return;
    beginResetModel(); m_items.clear(); m_generation=generation; endResetModel();
}
bool Vdl2MessageModel::append(const Vdl2ResultQueue::Result& result,std::uint64_t currentGeneration) {
    setGeneration(currentGeneration);
    if(result.generation!=currentGeneration || result.size>result.bytes.size()) return false;
    const auto frame=vdl2::parse(result.bytes.data(),result.size);
    if(!frame) return false;
    Item item{QStringLiteral("%1").arg(frame->source,6,16,QLatin1Char('0')).toUpper(),
              QStringLiteral("%1").arg(frame->destination,6,16,QLatin1Char('0')).toUpper(),
              QString::fromStdString(frame->kind),QString::fromStdString(frame->protocol),QString::fromStdString(frame->summary)};
    if(m_items.size()==500) { beginRemoveRows({},499,499); m_items.pop_back(); endRemoveRows(); }
    beginInsertRows({},0,0); m_items.insert(m_items.begin(),std::move(item)); endInsertRows();
    return true;
}
}
