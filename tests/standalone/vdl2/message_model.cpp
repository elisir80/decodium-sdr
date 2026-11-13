// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Vdl2MessageModel.h"
#include "core/Vdl2Controller.h"
#include "core/Vdl2Session.h"
#include "dsp/vdl2/OnAirVdl2Abi.h"
#include <QCoreApplication>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    dsdr::core::Vdl2MessageModel model;
    dsdr::core::Vdl2ResultQueue::Result result;
    OnairVdl2Addr dst{0x10a000,4,0},src{0x4ca2b1,1,0};
    onair_vdl2_put_addr(&dst,1,result.bytes.data());
    onair_vdl2_put_addr(&src,1,result.bytes.data()+4);
    result.bytes[8]=1;
    const auto crc=onair_vdl2_fcs(result.bytes.data(),9);
    result.bytes[9]=std::uint8_t(crc); result.bytes[10]=std::uint8_t(crc>>8);
    result.size=11; result.generation=1;
    if(!model.append(result,1) || model.rowCount()!=1 ||
       model.data(model.index(0,0),dsdr::core::Vdl2MessageModel::SourceRole).toString()!="4CA2B1") return 1;
    if(model.append(result,2) || model.rowCount()!=0) return 2;
    result.generation=2; result.bytes[5]^=1;
    if(model.append(result,2) || model.rowCount()!=0) return 3;
    result.bytes[5]^=1;
    for(int i=0;i<501;++i) if(!model.append(result,2)) return 4;
    if(model.rowCount()!=500) return 5;
    model.setGeneration(3);
    if(model.rowCount()!=0) return 6;
    dsdr::core::Vdl2Session session(105000,0,0.001);
    dsdr::core::Vdl2Controller controller(&session);
    controller.poll(); session.start(); controller.poll(); session.stop();
    if(controller.messages()->rowCount()!=0 || controller.workerErrors()!=0) return 7;
    controller.detach(); controller.poll();
    if(controller.messages()->rowCount()!=0) return 8;
    return 0;
}
