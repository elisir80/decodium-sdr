// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DscReceiver.h"
#include "dect2/marine/marine_dsc.h"
#include "dect2/marine/marine_dsp.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
using namespace dect2::marine;
using dsdr::core::DscReceiver;
using C=dsdr::dsp::Complex;
bool otherFormat(const std::vector<int>& body) {
    constexpr double pi=3.14159265358979323846;
    auto bits=dscFrameBits(body,127,200); std::vector<C> audio(1874); double phase=.73;
    for(size_t i=0;i<bits.size()*480;++i) {
        phase+=2*pi*(1700+(bits[i/480]?-85:85))/48000;
        audio.emplace_back(float(.65*std::sin(phase)),0);
    }
    audio.insert(audio.end(),1440,C{});
    std::vector<C> refAudio; for(size_t i=0;i<audio.size();i+=2) refAudio.push_back(audio[i]);
    FskSlicer slicer; slicer.setCentreHz(1700); DscDecoder ref(false,false),inv(true,false); int calls=0;
    auto cb=[&](const dect2::DscCall& c){if(c.eccOk && c.format==body[0] && c.fromMmsi=="232123456") ++calls;};
    ref.setCallback(cb); inv.setCallback(cb);
    slicer.process(refAudio.data(),refAudio.size(),[&](float s){ref.pushBit(-s);inv.pushBit(-s);});
    DscReceiver rx(512,1024,4); rx.start(); rx.setEnabled(true); const auto gen=rx.generation();
    for(size_t i=0;i<audio.size();i+=1024) {
        const auto n=std::min(size_t(1024),audio.size()-i);
        if(rx.submitForGeneration(audio.data()+i,n,gen)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) { rx.stop(); return false; }
    }
    DscReceiver::Message msg; bool got=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<deadline) {
        if(rx.readOne(msg)==DscReceiver::ReadResult::Message) { got=true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    rx.stop();
    const bool match=got && msg.call.bodyLength==int(body.size()) &&
        std::equal(body.begin(),body.end(),msg.call.fullBody.begin()) && calls==1;
    std::printf("format=%d OnAir=%d product=%d match=%d\n",body[0],calls,got,match);
    return match;
}
int main() {
    constexpr double pi=3.14159265358979323846;
    const std::array<int,15> body={112,23,21,23,45,60,103,15,5,10,1,18,12,34,109};
    const auto bits=dscFrameBits(std::vector<int>(body.begin(),body.end()),127,200);
    std::vector<C> audio(1874);
    double phase=.73;
    for(size_t i=0;i<bits.size()*480;++i) {
        phase+=2*pi*(1700+(bits[i/480]?-85:85))/48000;
        audio.emplace_back(float(.65*std::sin(phase)),0);
    }
    audio.insert(audio.end(),1440,C{});
    FskSlicer slicer; slicer.setCentreHz(1700);
    DscDecoder ref(false,false),inv(true,false); int calls=0;
    auto cb=[&](const dect2::DscCall& c) { if(c.eccOk && c.fromMmsi=="232123456") ++calls; };
    ref.setCallback(cb); inv.setCallback(cb);
    std::vector<C> referenceAudio;
    for(size_t i=0;i<audio.size();i+=2) referenceAudio.push_back(audio[i]);
    slicer.process(referenceAudio.data(),referenceAudio.size(),[&](float s){ref.pushBit(-s);inv.pushBit(-s);});
    DscReceiver rx(512,1024,4); rx.start(); rx.setEnabled(true);
    const auto gen=rx.generation();
    for(size_t i=0;i<audio.size();i+=1024) {
        auto n=std::min(size_t(1024),audio.size()-i);
        if(rx.submitForGeneration(audio.data()+i,n,gen)!=dsdr::core::AsyncIqDecoder::SubmitResult::Accepted) return 1;
    }
    DscReceiver::Message msg; bool got=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<deadline) {
        if(rx.readOne(msg)==DscReceiver::ReadResult::Message) { got=true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const bool match=got && msg.call.body==body && calls==1;
    rx.discontinuity();
    const bool stale=rx.submitForGeneration(audio.data(),100,gen)==dsdr::core::AsyncIqDecoder::SubmitResult::Stopped;
    const auto next=rx.generation();
    bool accepted=true;
    for(size_t i=0;i<audio.size();i+=1024) {
        auto n=std::min(size_t(1024),audio.size()-i);
        accepted=accepted && rx.submitForGeneration(audio.data()+i,n,next)==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted;
    }
    bool recovered=false;
    const auto deadline2=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<deadline2) {
        if(rx.readOne(msg)==DscReceiver::ReadResult::Message) { recovered=msg.call.body==body; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    rx.setEnabled(false);
    const bool disabled=rx.submitForGeneration(audio.data(),100,gen)==dsdr::core::AsyncIqDecoder::SubmitResult::Stopped;
    rx.stop();
    std::printf("OnAir=%d product=%d match=%d stale=%d recovery=%d disabled=%d\n",calls,got,match,stale,recovered,disabled);
    const bool all=otherFormat(dscBuildAllShips("232123456",108,100,126,82910,82910));
    const bool individual=otherFormat(dscBuildIndividual("123456789",100,"232123456",100,126,82910,82910));
    auto addressed=dscBuildIndividual("123456789",100,"232123456",100,126,82910,82910);
    addressed[0]=114; const bool group=otherFormat(addressed);
    addressed[0]=123; const bool automatic=otherFormat(addressed);
    addressed[0]=102; addressed[5]=91; const bool area=otherFormat(addressed);
    auto ackBody=dscBuildDistressAck("232123456","123456789",103,35.0,14.0,12,34,109);
    const bool ack=otherFormat(ackBody);
    ackBody[7]=112;
    const bool relay=otherFormat(ackBody);
    return match && accepted && stale && recovered && disabled && all && individual && group && automatic && area && ack && relay ? 0 : 1;
}
