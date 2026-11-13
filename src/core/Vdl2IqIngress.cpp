// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2IqIngress.h"
#include <algorithm>
#include <cmath>
namespace dsdr::core {
Vdl2IqIngress::Vdl2IqIngress(Vdl2Session& session,double rate,std::int64_t center) noexcept
    :m_session(session),m_rate(rate),m_center(center) {}
void Vdl2IqIngress::discontinuity() noexcept {
    m_session.discontinuity(); m_valid=false; m_status.store(Status::Waiting,std::memory_order_release);
}
void Vdl2IqIngress::process(const float* input,std::size_t frames,double rate,std::int64_t center) noexcept {
    const auto status=!input ? Status::NoInput : (!std::isfinite(rate) || rate!=m_rate) ? Status::WrongRate :
                      center!=m_center ? Status::WrongCenter : Status::Active;
    if(status!=Status::Active) {
        if(m_valid) discontinuity();
        m_status.store(status,std::memory_order_release); return;
    }
    m_valid=true;
    const auto generation=m_session.sourceGeneration();
    for(std::size_t at=0;at<frames;at+=m_iq.size()) {
        const auto n=std::min(m_iq.size(),frames-at);
        for(std::size_t i=0;i<n;++i) m_iq[i]={input[2*(at+i)],input[2*(at+i)+1]};
        m_session.processForGeneration(m_iq.data(),n,generation);
    }
    if(generation==m_session.sourceGeneration()) m_status.store(Status::Active,std::memory_order_release);
}
}
