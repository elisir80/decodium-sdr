// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2Session.h"
namespace dsdr::core {
Vdl2Session::Vdl2Session(unsigned rate,double offset,double floor,std::size_t results,std::size_t slots)
    :m_results(results),m_pipeline(rate,offset,floor,[this](const auto& b){m_results.publish(b);},slots) {}
bool Vdl2Session::read(Vdl2ResultQueue::Result& out) noexcept {
    const auto generation=m_pipeline.generation();
    if(!m_results.read(out,generation)) return false;
    if(generation!=m_pipeline.generation()) { out={}; return false; }
    return true;
}
}
