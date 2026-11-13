// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AdsbController.h"
#include "core/AdsbReceiver.h"
#include <QCoreApplication>
#include <cassert>
#include <vector>
using namespace dsdr;
using Frame = dsp::AdsbDecoder::Frame;
static void feed(core::AdsbReceiver &r, const Frame &f) {
    std::vector<dsp::Complex> s(240);
    for (int i : {0,2,7,9}) s[i] = dsp::Complex(0.8f,0);
    for (int b=0;b<112;++b) s[16+2*b+(((f[b/8]>>(7-b%8))&1)?0:1)] = dsp::Complex(0.8f,0);
    r.process(s.data(),s.size());
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    Frame even{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x82,0xd6,0x90,0xc8,0xac,0x28,0x63,0xa7};
    Frame odd {0x8d,0x40,0x62,0x1d,0x58,0xc3,0x86,0x43,0x5c,0xc4,0x12,0x69,0x2a,0xd6};
    core::AdsbReceiver r; core::AdsbController c(&r);
    r.setSource(true,false,2000000,1090000000); c.setEnabled(true);
    feed(r,even); c.poll(); assert(!c.hasPosition());
    feed(r,odd); c.poll(); assert(c.hasPosition()); assert(c.latitude()>52.26 && c.latitude()<52.27);
    r.discontinuity(); c.poll(); assert(!c.hasPosition());
    feed(r,odd); c.poll(); assert(!c.hasPosition());
    feed(r,even); c.poll(); assert(c.hasPosition());
    c.setEnabled(false); assert(!c.hasPosition());
}
