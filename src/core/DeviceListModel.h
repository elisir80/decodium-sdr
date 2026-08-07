// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — modello dei device scoperti.
#pragma once

#include "hal/DeviceDescriptor.h"

#include <QAbstractListModel>

#include <vector>

namespace dsdr::core {

class DeviceListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        BackendIdRole,
        DeviceIdRole,
        DisplayNameRole,
        ModelRole,
        SerialRole,
        TransportRole,
        AddressRole,
    };
    Q_ENUM(Roles)

    explicit DeviceListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Aggiunge il device se non è già presente (la discovery può ripetersi).
    void addDevice(const hal::DeviceDescriptor &device);
    void clear();

    const hal::DeviceDescriptor *at(int row) const;
    int indexOfKey(const QString &key) const;

signals:
    void countChanged();

private:
    std::vector<hal::DeviceDescriptor> m_devices;
};

} // namespace dsdr::core
