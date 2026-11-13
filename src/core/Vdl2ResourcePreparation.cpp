// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2ResourcePreparation.h"
#include <exception>

namespace dsdr::core {
Vdl2ResourcePreparation::Vdl2ResourcePreparation(unsigned rate, qint64 center,
                                                 double offset, double floor, QObject *parent)
    : QThread(parent), m_rate(rate), m_center(center), m_offset(offset), m_floor(floor) {}
void Vdl2ResourcePreparation::run()
{
    try { m_resources = std::make_unique<Vdl2Resources>(m_rate, m_center, m_offset, m_floor); }
    catch (const std::exception &e) { m_error = QString::fromUtf8(e.what()); }
    catch (...) { m_error = QStringLiteral("VDL2 resource preparation failed"); }
}
std::unique_ptr<Vdl2Resources> Vdl2ResourcePreparation::takeResources()
{
    Q_ASSERT(isFinished());
    return std::move(m_resources);
}
}
