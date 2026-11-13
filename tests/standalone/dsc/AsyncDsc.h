// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FskAudioRx.h"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dsc_rx {
// External owner serializes start/configure/stop; submit and poll are thread-safe.
class AsyncDsc {
public:
    enum class Submit { Accepted, Full, TooLarge, Invalid, Stopped };
    struct Config { int sampleRate = 24000; int centreHz = 1700; std::string source; };
    struct Stats { uint64_t accepted, droppedBlocks, droppedSamples, droppedResults, generation; };
    AsyncDsc(size_t blocks, size_t maxSamples, size_t results);
    ~AsyncDsc();
    AsyncDsc(const AsyncDsc&) = delete;
    AsyncDsc& operator=(const AsyncDsc&) = delete;
    // Stops and discards queued data/results before changing source/configuration.
    // Unsupported settings leave the receiver stopped and unconfigured.
    bool configure(const Config& config);
    bool start();
    void stop(bool drain = false);
    Submit submit(const float* samples, size_t count);
    void gap(); // call before submitting first block after discontinuity
    bool poll(Distress& out);
    Stats stats() const;
    bool active() const;
private:
    struct Block { std::vector<float> samples; bool reset = false; };
    void run();
    const size_t blockLimit_, sampleLimit_, resultLimit_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Block> queue_;
    std::deque<Distress> results_;
    std::thread thread_;
    bool configured_ = false, running_ = false, drain_ = false, resetNext_ = true;
    uint64_t accepted_ = 0, droppedBlocks_ = 0, droppedSamples_ = 0, droppedResults_ = 0, generation_ = 0;
};
}
