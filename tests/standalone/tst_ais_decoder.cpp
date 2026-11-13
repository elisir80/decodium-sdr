// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/AisDecoder.h"
#include "ais_synthetic.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using Decoder=dsdr::dsp::AisDecoder;
using Message=Decoder::Message;
void capture(void *p,const Message &m) { static_cast<std::vector<Message> *>(p)->push_back(m); }
std::vector<Message> run(const std::vector<std::complex<float>> &iq,int rate,double offset,bool chunks) {
    std::vector<Message> result; Decoder d(capture,&result); assert(d.configure(rate,offset));
    for(std::size_t at=0;at<iq.size();) {
        const auto n=chunks?std::min<std::size_t>(1+(at*7%89),iq.size()-at):iq.size()-at;
        d.process(iq.data()+at,n);at+=n;
    }
    return result;
}
int main() {
    const std::string vector="123456789";
    const std::vector<std::uint8_t> crcVector(vector.begin(),vector.end());
    assert(ais_test::fcs(crcVector)==0x906e);
    assert(Decoder::fcs(crcVector.data(),crcVector.size())==0x906e);
    auto p=ais_test::position(); int cases=0;
    for(int rate:{48000,96000,192000}) for(int delay:{0,1,3,7}) for(bool reverse:{false,true}) {
        const double offset=reverse?-1200:1200;
        auto good=ais_test::transmit(p,rate,offset,0.025,delay,false,reverse);
        auto bad=ais_test::transmit(p,rate,offset,0.025,delay,true,reverse);
        const double configured=reverse?-offset:offset;
        auto a=run(good,rate,configured,false), b=run(good,rate,configured,true);
        if(a.size()!=1 || b.size()!=1 || a[0].payload!=p || b[0].payload!=p) {
            std::fprintf(stderr,"failure rate=%d delay=%d reversed=%d whole=%zu chunks=%zu\n",rate,delay,reverse,a.size(),b.size()); return 1;
        }
        assert(a[0].type==1 && a[0].mmsi==123456789 && a[0].hasPosition);
        assert(std::abs(a[0].longitude-14.5)<1e-6 && std::abs(a[0].latitude-35.9)<1e-6);
        assert(run(bad,rate,configured,true).empty()); ++cases;
    }
    // GPSD sample.aivdm: one public type-1 payload, re-modulated here.
    // Copyright 2010 the GPSD project; BSD-2-Clause. See ais-gpsd-BSD-2-Clause.txt.
    // Source: https://gitlab.com/gpsd/gpsd/-/raw/master/test/sample.aivdm
    const std::string armored="15RTgt0PAso;90TKcjM8h6g208CQ";
    std::vector<std::uint8_t> known(21,0);
    for (std::size_t i=0;i<armored.size();++i) {
        int v=armored[i]-48; if(v>40) v-=8;
        assert(v>=0 && v<64);
        ais_test::setBits(known,unsigned(i*6),6,unsigned(v));
    }
    auto reconstructed=ais_test::transmit(known,96000,0,0.01,3,false);
    auto actual=run(reconstructed,96000,0,true);
    assert(actual.size()==1 && actual[0].type==1 && actual[0].mmsi==371798000 && actual[0].hasPosition);
    assert(std::abs(actual[0].longitude-(-123.395383333))<0.000002);
    assert(std::abs(actual[0].latitude-48.381633333)<0.000002);
    // Long sequences of ones exercise stuffing across payload and FCS.
    auto stuffed=p; std::fill(stuffed.begin()+10,stuffed.end(),0xff);
    auto stuffedResult=run(ais_test::transmit(stuffed,96000,0,0,1,false),96000,0,true);
    assert(stuffedResult.size()==1 && stuffedResult[0].payload==stuffed);
    // Unavailable position sentinel must not be displayed as coordinates.
    auto unavailable=std::vector<std::uint8_t>(21,0);
    ais_test::setBits(unavailable,0,6,1); ais_test::setBits(unavailable,8,30,123456789);
    ais_test::setBits(unavailable,61,28,181*600000); ais_test::setBits(unavailable,89,27,91*600000);
    auto noPosition=run(ais_test::transmit(unavailable,48000,0,0,0,false),48000,0,true);
    assert(noPosition.size()==1 && !noPosition[0].hasPosition);
    // Class B position fields begin at bits 57 and 85, not the Class A
    // offsets. Compare positive and negative coordinates, missing positions,
    // and a CRC-valid but truncated type-19 report.
    for (unsigned type : {18u,19u}) {
        std::vector<std::uint8_t> report(type==18 ? 21 : 39,0);
        ais_test::setBits(report,0,6,type); ais_test::setBits(report,8,30,123456789);
        ais_test::setBits(report,57,28,std::uint32_t(-7500000) & 0x0fffffff);
        ais_test::setBits(report,85,27,31000000);
        const auto good=run(ais_test::transmit(report,96000,0,0,3,false),96000,0,true);
        assert(good.size()==1 && good[0].type==type && good[0].hasPosition);
        assert(std::abs(good[0].longitude-(-12.5))<1e-6);
        assert(std::abs(good[0].latitude-(31000000.0/600000.0))<1e-6);
        ais_test::setBits(report,57,28,181*600000);
        ais_test::setBits(report,85,27,91*600000);
        const auto absent=run(ais_test::transmit(report,96000,0,0,3,false),96000,0,true);
        assert(absent.size()==1 && !absent[0].hasPosition);
        assert(run(ais_test::transmit(report,96000,0,0,3,true),96000,0,true).empty());
    }
    auto truncated=std::vector<std::uint8_t>(21,0);
    ais_test::setBits(truncated,0,6,19); ais_test::setBits(truncated,8,30,123456789);
    auto incomplete=run(ais_test::transmit(truncated,96000,0,0,3,false),96000,0,true);
    assert(incomplete.size()==1 && !incomplete[0].hasPosition);
    // Type 27 has lower-resolution coordinates and a 96-bit payload.
    for (unsigned bytes : {12u,21u}) {
        std::vector<std::uint8_t> longRange(bytes,0);
        ais_test::setBits(longRange,0,6,27); ais_test::setBits(longRange,8,30,123456789);
        ais_test::setBits(longRange,44,18,std::uint32_t(-7500)&0x3ffff);
        ais_test::setBits(longRange,62,17,30000);
        auto good=run(ais_test::transmit(longRange,96000,0,0,3,false),96000,0,true);
        assert(good.size()==1 && good[0].type==27 && good[0].hasPosition);
        assert(std::abs(good[0].longitude-(-12.5))<1e-6 && std::abs(good[0].latitude-50.0)<1e-6);
        assert(run(ais_test::transmit(longRange,96000,0,0,3,true),96000,0,true).empty());
        ais_test::setBits(longRange,44,18,181*600); ais_test::setBits(longRange,62,17,91*600);
        auto absent=run(ais_test::transmit(longRange,96000,0,0,3,false),96000,0,true);
        assert(absent.size()==1 && !absent[0].hasPosition);
    }
    std::vector<std::uint8_t> shortLongRange(10,0);
    ais_test::setBits(shortLongRange,0,6,27); ais_test::setBits(shortLongRange,8,30,123456789);
    auto shortResult=run(ais_test::transmit(shortLongRange,96000,0,0,3,false),96000,0,true);
    assert(shortResult.size()==1 && !shortResult[0].hasPosition);
    for(unsigned type : {4u,9u,11u}) {
        const unsigned lonBit=type==9 ? 61 : 79, latBit=type==9 ? 89 : 107;
        std::vector<std::uint8_t> report(21,0);
        ais_test::setBits(report,0,6,type); ais_test::setBits(report,8,30,123456789);
        ais_test::setBits(report,lonBit,28,std::uint32_t(-7500000)&0x0fffffff);
        ais_test::setBits(report,latBit,27,30000000);
        auto good=run(ais_test::transmit(report,96000,0,0,3,false),96000,0,true);
        assert(good.size()==1 && good[0].type==type && good[0].hasPosition);
        assert(std::abs(good[0].longitude-(-12.5))<1e-6 && std::abs(good[0].latitude-50.0)<1e-6);
        assert(run(ais_test::transmit(report,96000,0,0,3,true),96000,0,true).empty());
        ais_test::setBits(report,lonBit,28,181*600000); ais_test::setBits(report,latBit,27,91*600000);
        auto absent=run(ais_test::transmit(report,96000,0,0,3,false),96000,0,true);
        assert(absent.size()==1 && !absent[0].hasPosition);
        report.resize(20);
        auto shortReport=run(ais_test::transmit(report,96000,0,0,3,false),96000,0,true);
        assert(shortReport.size()==1 && !shortReport[0].hasPosition);
    }
    // Message 21: fixed 272-bit aid-to-navigation report, with optional name extension.
    for(unsigned length : {34u, 45u}) {
        std::vector<std::uint8_t> report(length,0);
        ais_test::setBits(report,0,6,21); ais_test::setBits(report,8,30,993001234);
        ais_test::setBits(report,164,28,std::uint32_t(-7500000)&0x0fffffff);
        ais_test::setBits(report,192,27,30000000);
        const auto good=run(ais_test::transmit(report,96000,0,0.01,3,false),96000,0,true);
        assert(good.size()==1 && good[0].type==21 && good[0].hasPosition);
        assert(std::abs(good[0].longitude+12.5)<1e-6 && std::abs(good[0].latitude-50.)<1e-6);
        assert(run(ais_test::transmit(report,96000,0,0.01,3,true),96000,0,true).empty());
        ais_test::setBits(report,164,28,181*600000); ais_test::setBits(report,192,27,91*600000);
        const auto absent=run(ais_test::transmit(report,96000,0,0,3,false),96000,0,true);
        assert(absent.size()==1 && !absent[0].hasPosition);
        report.resize(33);
        const auto shortReport=run(ais_test::transmit(report,96000,0,0,3,false),96000,0,true);
        assert(shortReport.size()==1 && !shortReport[0].hasPosition);
    }
    // Type 17 identifies the surveyed DGNSS reference station, not a vessel.
    for(unsigned length : {10u, 30u}) {
        std::vector<std::uint8_t> station(length,0);
        ais_test::setBits(station,0,6,17); ais_test::setBits(station,8,30,123456789);
        ais_test::setBits(station,40,18,std::uint32_t(-7500)&0x3ffff);
        ais_test::setBits(station,58,17,30000);
        auto good=run(ais_test::transmit(station,96000,0,0,3,false),96000,0,true);
        assert(good.size()==1 && good[0].type==17 && good[0].hasPosition);
        assert(std::abs(good[0].longitude+12.5)<1e-6 && std::abs(good[0].latitude-50.)<1e-6);
        assert(run(ais_test::transmit(station,96000,0,0,3,true),96000,0,true).empty());
        ais_test::setBits(station,40,18,181*600); ais_test::setBits(station,58,17,91*600);
        auto absent=run(ais_test::transmit(station,96000,0,0,3,false),96000,0,true);
        assert(absent.size()==1 && !absent[0].hasPosition);
        station.resize(9);
        auto truncated=run(ais_test::transmit(station,96000,0,0,3,false),96000,0,true);
        assert(truncated.size()==1 && !truncated[0].hasPosition);
    }
    // Gross frequency mismatch suppresses a frame even though its FCS is valid.
    assert(run(ais_test::transmit(p,48000,3500,0,0,false),48000,0,true).empty());
    std::vector<Message> got; Decoder d(capture,&got); assert(!d.configure(32000)); assert(!d.configure(48000,20000)); assert(d.configure(48000));
    auto samples=ais_test::transmit(p,48000,0,0,3,false);
    d.process(samples.data(),samples.size()/2); d.reset(); d.process(samples.data()+samples.size()/2,samples.size()-samples.size()/2); assert(got.empty());
    d.reset(); d.process(samples.data(),samples.size()); assert(got.size()==1);
    d.process(samples.data(),samples.size()); assert(got.size()==2);
    std::puts(("AIS independent GMSK A/B, FCS, chunking, reset: "+std::to_string(cases)+" cases passed").c_str());
}
