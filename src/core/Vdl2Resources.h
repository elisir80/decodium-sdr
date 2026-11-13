// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Vdl2IqIngress.h"
namespace dsdr::core {
// Owns the ingress and its session together. The ingress is destroyed first.
// Destruction joins workers, so release this bundle on the DecoderShutdown
// thread only after UI users, DSP producer and backend have been fenced.
class Vdl2Resources final {
public:
    Vdl2Resources(unsigned rate,std::int64_t center,double offset,double floor,
                  std::size_t results=64,std::size_t queueSlots=64)
        :m_session(rate,offset,floor,results,queueSlots),m_ingress(m_session,rate,center) {}
    Vdl2Session& session() noexcept { return m_session; }
    Vdl2IqIngress& ingress() noexcept { return m_ingress; }
private:
    Vdl2Session m_session;
    Vdl2IqIngress m_ingress;
};
}
