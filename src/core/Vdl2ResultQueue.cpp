// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2ResultQueue.h"
#include <algorithm>
#include <stdexcept>
namespace dsdr::core {
Vdl2ResultQueue::Vdl2ResultQueue(std::size_t capacity):m_queue(capacity && capacity<=4096?capacity:1) {
    if(!capacity || capacity>4096) throw std::invalid_argument("invalid VDL2 result queue capacity");
}
void Vdl2ResultQueue::publish(const vdl2::OfflineBurst& burst) noexcept {
    for(const auto& frame:burst.burst.frames) {
        if(frame.size()>Result{}.bytes.size()) { ++m_dropped; continue; }
        Result out; out.generation=burst.generation; out.size=frame.size();
        std::copy(frame.begin(),frame.end(),out.bytes.begin());
        if(!m_queue.write(&out,1)) ++m_dropped;
    }
}
bool Vdl2ResultQueue::read(Result& result,std::uint64_t generation) noexcept {
    while(m_queue.read(&result,1)) if(result.generation==generation) return true;
    result={}; return false;
}
}
