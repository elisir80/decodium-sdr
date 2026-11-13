// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2Lifecycle.h"
#include "DspEngine.h"
#include <stdexcept>
namespace dsdr::core {
namespace {
std::unique_ptr<Vdl2Resources> resourcesFor(DspEngine* engine,unsigned rate,std::int64_t center,double offset,double floor) {
    if(!engine) throw std::invalid_argument("VDL2 requires a DSP engine");
    return std::make_unique<Vdl2Resources>(rate,center,offset,floor);
}
Vdl2Resources *requireResources(DspEngine *engine,const std::unique_ptr<Vdl2Resources>& resources) {
    if (!engine || !resources) throw std::invalid_argument("VDL2 requires an engine and prepared resources");
    return resources.get();
}
std::unique_ptr<DecoderShutdown> own(std::unique_ptr<Vdl2Resources> p) {
    DecoderShutdown::Resources list;
    list.push_back(DecoderShutdown::own(std::move(p)));
    // VDL2 resources only reference the DSP tap; no backend owns or calls
    // them. The queued tap-detach is the required producer/user fence.
    return std::make_unique<DecoderShutdown>(std::move(list), DecoderShutdown::BackendFence::NotRequired);
}
}
Vdl2Lifecycle::Vdl2Lifecycle(DspEngine* engine,unsigned rate,std::int64_t center,double offset,double floor,QObject* parent)
    :Vdl2Lifecycle(engine,resourcesFor(engine,rate,center,offset,floor),parent) {}
Vdl2Lifecycle::Vdl2Lifecycle(DspEngine* engine,std::unique_ptr<Vdl2Resources> resources,QObject* parent)
    :QObject(parent),m_engine(engine),m_resources(requireResources(engine,resources)),
     m_shutdown(own(std::move(resources))),m_controller(&m_resources->session(),this,&m_resources->ingress()),m_state(tr("Disattivato")) {
    m_timer.setInterval(20);
    connect(&m_timer,&QTimer::timeout,this,[this]{ if(ready()) { m_timer.stop(); m_resources=nullptr; setState(tr("Arrestato")); } });
}
Vdl2Lifecycle::~Vdl2Lifecycle() = default;
void Vdl2Lifecycle::setState(const QString& value) { if(m_state!=value) {m_state=value; emit changed();} }
void Vdl2Lifecycle::activate() {
    if(m_requested || m_activated || !m_engine) return;
    m_resources->session().start(); m_activated=true; setState(tr("Avvio"));
    QPointer<Vdl2Lifecycle> self(this);
    auto* tap=&m_resources->ingress();
    QMetaObject::invokeMethod(m_engine,[self,tap] {
        if(!self) return;
        bool attached=false;
        if(!self->m_stopping.load(std::memory_order_acquire))
            if(auto* engine=self->m_engine.data()) attached=engine->tryAttachRawIqTap(tap);
        QMetaObject::invokeMethod(self,[self,attached]{ if(self && !self->m_requested) self->setState(attached?self->tr("Attivo"):self->tr("Punto IQ occupato")); },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void Vdl2Lifecycle::requestShutdown() {
    if(m_requested) return;
    m_requested=true; m_stopping.store(true,std::memory_order_release); setState(tr("Arresto in corso"));
    m_resources->ingress().invalidateResults(); m_controller.detach(); m_shutdown->request();
    if(!m_activated) { m_shutdown->acknowledgeUsersQuiescent(); m_usersFenced=true; }
    else if(m_engine) {
        QPointer<Vdl2Lifecycle> self(this);
        auto* tap=&m_resources->ingress();
        QMetaObject::invokeMethod(m_engine,[self,tap] {
            if(!self) return;
            if(auto* engine=self->m_engine.data()) engine->detachRawIqTap(tap);
            QMetaObject::invokeMethod(self,[self] { if(self && !self->m_usersFenced) {
                self->m_shutdown->acknowledgeUsersQuiescent(); self->m_usersFenced=true;
            } },Qt::QueuedConnection);
        },Qt::QueuedConnection);
    }
    m_timer.start();
}
void Vdl2Lifecycle::acknowledgeBackendStopped() noexcept { if(m_shutdown) m_shutdown->acknowledgeBackendStopped(); }
}
