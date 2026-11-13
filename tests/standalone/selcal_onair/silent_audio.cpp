// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone test sink: deliberately cannot open or play an audio device.
#include "dect2/audio/audioout.h"
namespace dect2 {
struct AudioOut::Impl {};
AudioOut::AudioOut() : p_(new Impl) {}
AudioOut::~AudioOut() = default;
bool AudioOut::start(int, int) { return false; }
int AudioOut::write(const float*, int) { return 0; }
void AudioOut::flush() {}
int AudioOut::bufferedFrames() const { return 0; }
bool AudioOut::playing() const { return false; }
void AudioOut::setVolume(float) {}
void AudioOut::setMuted(bool) {}
void AudioOut::setStartThreshold(int) {}
}
