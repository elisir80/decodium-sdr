// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Vdl2Resources.h"
#include <QThread>
#include <QString>
#include <memory>

namespace dsdr::core {
// Create filters and bounded IQ queues off the GUI thread. The owner must
// retain this object until QThread::finished, including during shutdown.
class Vdl2ResourcePreparation final : public QThread {
    Q_OBJECT
public:
    Vdl2ResourcePreparation(unsigned rate, qint64 center, double offset, double floor,
                            QObject *parent = nullptr);
    std::unique_ptr<Vdl2Resources> takeResources();
    QString error() const { return m_error; }
protected:
    void run() override;
private:
    const unsigned m_rate;
    const qint64 m_center;
    const double m_offset, m_floor;
    std::unique_ptr<Vdl2Resources> m_resources;
    QString m_error;
};
}
