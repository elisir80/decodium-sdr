// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AisEngineTap.h"
namespace dsdr::core {
AisEngineTap::AisEngineTap(AisReceiver &receiver) noexcept
    :m_receiver(receiver),m_frontend(output,this) {}
bool AisEngineTap::configure(unsigned rate,double offset) {
    m_receiver.discontinuity(); m_accept=false; m_filterGeneration=0;
    return m_frontend.configure(rate,offset);
}
void AisEngineTap::reset() noexcept {
    m_receiver.discontinuity(); m_frontend.reset(); m_accept=false; m_filterGeneration=0;
}
void AisEngineTap::process(const dsp::Complex *samples,std::size_t count,std::uint64_t generation) noexcept {
    if(generation!=m_receiver.generation() || !m_frontend.configured() ||
       m_receiver.status()!=AisReceiver::Status::Active) return;
    if(m_filterGeneration!=generation) { m_frontend.reset(); m_filterGeneration=generation; }
    m_generation=generation; m_accept=true; m_frontend.process(samples,count); m_accept=false;
}
void AisEngineTap::output(void *ctx,const dsp::AisIqFrontend::Sample *samples,std::size_t count) noexcept {
    auto &self=*static_cast<AisEngineTap *>(ctx);
    if(!samples && !count) { self.m_receiver.discontinuity(); self.m_accept=false; return; }
    if(!self.m_accept) return;
    if(self.m_receiver.submitForGeneration(samples,count,self.m_generation)!=AsyncIqDecoder::SubmitResult::Accepted)
        self.m_accept=false;
}
}
