// SPDX-License-Identifier: GPL-3.0-or-later
#include "AsyncDsc.h"
#include <utility>
namespace dsc_rx {
AsyncDsc::AsyncDsc(size_t b,size_t s,size_t r):blockLimit_(b),sampleLimit_(s),resultLimit_(r) {}
AsyncDsc::~AsyncDsc() { stop(false); }
bool AsyncDsc::configure(const Config& c) {
    stop(false);
    std::lock_guard<std::mutex> lock(mu_);
    queue_.clear(); results_.clear(); resetNext_=true; ++generation_;
    configured_ = blockLimit_ && sampleLimit_ && resultLimit_ && c.sampleRate==24000 && c.centreHz==1700 && !c.source.empty();
    return configured_;
}
bool AsyncDsc::start() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!configured_ || running_ || thread_.joinable()) return false;
    running_=true; drain_=false; resetNext_=true;
    try { thread_=std::thread(&AsyncDsc::run,this); } catch (...) { running_=false; return false; }
    return true;
}
void AsyncDsc::stop(bool drain) {
    { std::lock_guard<std::mutex> lock(mu_); running_=false; drain_=drain; if (!drain) queue_.clear(); }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (!drain) { std::lock_guard<std::mutex> lock(mu_); results_.clear(); }
}
AsyncDsc::Submit AsyncDsc::submit(const float* p,size_t n) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!running_) return Submit::Stopped;
    if (!n) return Submit::Accepted;
    if (!p || n>sampleLimit_ || queue_.size()>=blockLimit_) {
        ++droppedBlocks_; droppedSamples_+=n; resetNext_=true;
        return !p ? Submit::Invalid : n>sampleLimit_ ? Submit::TooLarge : Submit::Full;
    }
    queue_.push_back({std::vector<float>(p,p+n),resetNext_}); resetNext_=false; ++accepted_;
    cv_.notify_one(); return Submit::Accepted;
}
void AsyncDsc::gap() { std::lock_guard<std::mutex> lock(mu_); resetNext_=true; }
bool AsyncDsc::poll(Distress& out) { std::lock_guard<std::mutex> lock(mu_); if(results_.empty()) return false; out=results_.front(); results_.pop_front(); return true; }
AsyncDsc::Stats AsyncDsc::stats() const { std::lock_guard<std::mutex> lock(mu_); return {accepted_,droppedBlocks_,droppedSamples_,droppedResults_,generation_}; }
bool AsyncDsc::active() const { std::lock_guard<std::mutex> lock(mu_); return running_; }
void AsyncDsc::run() {
    FskAudioRx rx([this](const Distress& d) { std::lock_guard<std::mutex> lock(mu_); if(results_.size()==resultLimit_) ++droppedResults_; else results_.push_back(d); });
    for (;;) {
        Block b;
        { std::unique_lock<std::mutex> lock(mu_); cv_.wait(lock,[this]{return !running_ || !queue_.empty();});
          if (!running_ && (!drain_ || queue_.empty())) break;
          b=std::move(queue_.front()); queue_.pop_front(); }
        if (b.reset) rx.reset();
        rx.process(b.samples.data(),b.samples.size());
    }
}
}
