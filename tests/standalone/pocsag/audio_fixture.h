// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/PocsagDecoder.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using dsdr::dsp::PocsagDecoder;
static std::uint32_t encode(std::uint32_t data) {
    std::uint32_t dividend = data << 10;
    for (int i = 30; i >= 10; --i) if ((dividend >> i) & 1) dividend ^= 0x769u << (i - 10);
    std::uint32_t word = ((data << 10) | dividend) << 1;
    unsigned parity = 0; for (auto x = word; x; x >>= 1) parity ^= x & 1;
    return word | parity;
}
static void append(std::vector<std::uint8_t>& b, std::uint32_t w) { for (int i=31;i>=0;--i) b.push_back((w>>i)&1); }
static std::vector<std::uint32_t> words(const std::string &s) {
    std::vector<int> b; for (unsigned char c:s) for(int j=0;j<7;++j) b.push_back((c>>j)&1);
    while (b.size()%20) b.push_back(0);
    std::vector<std::uint32_t> out;
    for (std::size_t i=0;i<b.size();i+=20) { std::uint32_t d=1; for(int j=0;j<20;++j) d=(d<<1)|b[i+j]; out.push_back(encode(d)); }
    return out;
}
static void receive(void *c, const PocsagDecoder::Message &m) { static_cast<std::vector<PocsagDecoder::Message>*>(c)->push_back(m); }
static std::vector<std::uint8_t> stream(unsigned errors, bool invert=false) {
    std::vector<std::uint8_t> b(576); for (std::size_t i=0;i<b.size();++i) b[i]=i%2;
    append(b,0x7cd215d8); auto msg=words("HELLO PAGER");
    // RIC 1234563 belongs to frame 3. Idle fills preceding frames.
    for(int i=0;i<6;++i) append(b,0x7a89c197);
    append(b,encode(((1234563u >> 3) << 2) | 3));
    for(std::size_t i=0;i<msg.size();++i) { auto w=msg[i]; if(i==0) { if(errors>=1)w^=1u<<5; if(errors>=2)w^=1u<<9; if(errors>=3)w^=1u<<15; } append(b,w); }
    for (std::size_t i=7+msg.size();i<16;++i) append(b,0x7a89c197);
    if(invert) for(auto &v:b) v^=1;
    return b;
}
