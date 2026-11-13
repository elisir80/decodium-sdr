// Standalone comparison. OnAir is linked from the reference checkout at build time.
#ifdef DSC_PRODUCT
#include "dsp/dsc/DscRx.h"
#else
#include "DscRx.h"
#endif
#include "dect2/marine/marine_dsc.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using namespace dect2::marine;
using dect2::DscCall;
using Bits = std::vector<uint8_t>;
static void require(bool x, const char* what) { if (!x) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); } }

// Independent fixture: explicit construction of the 10-bit code and interleave.
static void append(Bits& dst, int value) {
    int zeros = 7;
    for (int k = 0; k < 7; ++k) { int b = (value >> k) & 1; dst.push_back(b); zeros -= b; }
    for (int k = 2; k >= 0; --k) dst.push_back((zeros >> k) & 1);
}
static Bits fixture(const std::array<int,15>& body) {
    std::vector<int> stream(52, 126);
    for (int j = 0; j < 12; ++j) stream[j] = j % 2 ? 111 - j / 2 : 125;
    for (int j = 13; j < 16; j += 2) stream[j] = 111 - j / 2;
    std::vector<int> msg = {body[0], body[0]};
    msg.insert(msg.end(), body.begin() + 1, body.end());
    int parity = 127;
    for (int v : body) parity ^= v;
    msg.push_back(127); msg.push_back(parity);
    for (int j = 0; j < 18; ++j) {
        stream[12 + 2*j] = msg[j];
        stream[17 + 2*j] = msg[j];
    }
    // EOS repeat in the final DX position (the final RX slot is the ECC).
    stream[48] = 127; stream[50] = 127;
    Bits bits;
    for (int i = 0; i < 200; ++i) bits.push_back((i + 1) % 2);
    for (int s : stream) append(bits, s);
    return bits;
}
struct Result { std::vector<dsc_rx::Distress> a; std::vector<DscCall> b; };
static Result run(const Bits& bits, bool inverted = false) {
    Result r; dsc_rx::Decoder d; DscDecoder reference(inverted, false);
    reference.setCallback([&](const DscCall& c) { r.b.push_back(c); });
    for (auto b : bits) {
        if (auto call = d.push(b)) r.a.push_back(*call);
        reference.pushBit(b ? 1.f : -1.f);
    }
    return r;
}
int main() {
    std::cout << "Based on OnAir by Kristijonas Adomaitis - https://on-air.dev\n";
    const std::array<int,15> body = {112,23,21,23,45,60,103,15,5,10,1,18,12,34,109};
    const std::vector<int> refBody(body.begin(), body.end());
    auto bits = fixture(body);
    require(bits == dscFrameBits(refBody, 127, 200), "independent fixture versus reference framing");
    auto r = run(bits);
    require(r.a.size() == 1 && r.b.size() == 1 && r.b[0].eccOk, "both decode clean call");
    require(r.a[0].body == body && r.b[0].fromMmsi == "232123456" && r.b[0].nature == 103 && r.b[0].eos == 127, "call fields");
    auto reverse = run(dscFrameBits(refBody, 127, 200));
    require(reverse.a.size() == 1 && reverse.a[0].body == body, "reference generator to local decoder");
    std::mt19937 bodies(420);
    for (int n = 0; n < 40; ++n) {
        auto varied = body;
        for (int i = 1; i <= 4; ++i) varied[i] = bodies() % 100;
        varied[5] = (bodies() % 10) * 10;
        varied[6] = 100 + bodies() % 12;
        auto result = run(fixture(varied));
        require(result.a.size() == 1 && result.a[0].body == varied && result.b.size() == 1 && result.b[0].eccOk, "varied distress cases");
    }
    auto inv = bits; for (auto& b : inv) b ^= 1;
    auto ri = run(inv, true);
    require(ri.a.size() == 1 && ri.a[0].inverted && ri.b.size() == 1 && ri.b[0].eccOk, "inverted polarity");
    auto damaged = bits; damaged[200 + 10 * 20] ^= 1;
    auto rd = run(damaged);
    require(rd.a.size() == 1 && rd.a[0].repaired > 0 && rd.b.size() == 1 && rd.b[0].eccOk, "one bad copy recovered");
    auto conflict = bits;
    Bits alternate; append(alternate, 24);
    const size_t conflictRx = 200 + 10*(17 + 2*2);
    for (int i = 0; i < 10; ++i) conflict[conflictRx+i] = alternate[i];
    require(run(conflict).a.empty(), "valid conflicting copies rejected locally");
    auto invalid = bits;
    // Replace both ECC copies with another valid symbol, retaining symbol parity.
    const size_t eccdx = 200 + 10*(12 + 2*17), eccrx = 200 + 10*(17 + 2*17);
    int wrong = 1; for (int v : body) wrong ^= v; wrong ^= 127;
    Bits bad; append(bad, wrong);
    for (int i = 0; i < 10; ++i) invalid[eccdx+i] = invalid[eccrx+i] = bad[i];
    auto rb = run(invalid);
    require(rb.a.empty(), "invalid ECC rejected locally");
    for (const auto& c : rb.b) require(!c.eccOk, "invalid ECC rejected by reference");
    auto truncated = bits; truncated.resize(truncated.size()-1);
    require(run(truncated).a.empty(), "truncated call rejected");
    dsc_rx::Decoder reset;
    for (size_t i=0; i<bits.size()/2; ++i) reset.push(bits[i]);
    reset.reset();
    bool found = false; for (size_t i=bits.size()/2; i<bits.size(); ++i) found |= bool(reset.push(bits[i]));
    require(!found, "reset discards partial frame");
    std::mt19937 rng(781); Bits noise(300000); for (auto& b : noise) b = rng() & 1;
    auto rn = run(noise);
    require(rn.a.empty() && rn.b.empty(), "random noise yields no calls");
    std::cout << "PASS: clean, reverse, inversion, one-copy damage, ECC rejection, truncation, reset, noise\n";
    return 0;
}
