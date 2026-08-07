// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DeviceListModel.h"

#include <algorithm>

namespace dsdr::core {

DeviceListModel::DeviceListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DeviceListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_devices.size());
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return QVariant();

    const hal::DeviceDescriptor &device = m_devices[static_cast<std::size_t>(index.row())];

    switch (role) {
    case KeyRole:         return device.key();
    case BackendIdRole:   return device.backendId;
    case DeviceIdRole:    return device.deviceId;
    case DisplayNameRole: return device.displayName;
    case ModelRole:       return device.model;
    case SerialRole:      return device.serial;
    case TransportRole:   return device.transport;
    case AddressRole:     return device.address;
    default:              return QVariant();
    }
}

QHash<int, QByteArray> DeviceListModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {BackendIdRole, "backendId"},
        {DeviceIdRole, "deviceId"},
        {DisplayNameRole, "displayName"},
        {ModelRole, "model"},
        {SerialRole, "serial"},
        {TransportRole, "transport"},
        {AddressRole, "address"},
    };
}

void DeviceListModel::addDevice(const hal::DeviceDescriptor &device)
{
    if (!device.isValid() || indexOfKey(device.key()) >= 0)
        return;

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_devices.push_back(device);
    endInsertRows();
    emit countChanged();
}

void DeviceListModel::clear()
{
    if (m_devices.empty())
        return;
    beginResetModel();
    m_devices.clear();
    endResetModel();
    emit countChanged();
}

const hal::DeviceDescriptor *DeviceListModel::at(int row) const
{
    if (row < 0 || row >= rowCount())
        return nullptr;
    return &m_devices[static_cast<std::size_t>(row)];
}

int DeviceListModel::indexOfKey(const QString &key) const
{
    const auto it = std::find_if(m_devices.begin(), m_devices.end(),
                                 [&key](const hal::DeviceDescriptor &d) { return d.key() == key; });
    return it == m_devices.end() ? -1 : static_cast<int>(std::distance(m_devices.begin(), it));
}

} // namespace dsdr::core
