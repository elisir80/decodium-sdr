// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Vdl2ResultQueue.h"
#include <QAbstractListModel>
#include <QString>
#include <vector>
namespace dsdr::core {
// GUI-thread-only presentation model. It does not own or read an IQ source.
class Vdl2MessageModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { SourceRole=Qt::UserRole+1,DestinationRole,KindRole,ProtocolRole,SummaryRole };
    explicit Vdl2MessageModel(QObject* parent=nullptr);
    int rowCount(const QModelIndex& parent={}) const override;
    QVariant data(const QModelIndex& index,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    void setGeneration(std::uint64_t generation);
    bool append(const Vdl2ResultQueue::Result& result,std::uint64_t currentGeneration);
private:
    struct Item { QString source,destination,kind,protocol,summary; };
    std::uint64_t m_generation=0;
    std::vector<Item> m_items;
};
}
