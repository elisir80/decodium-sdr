// SPDX-License-Identifier: GPL-3.0-or-later
// Modulo C ABI minimale usato dalla prova d'integrazione del loader.

#include "core/IqModuleApi.h"

#include <atomic>
#include <cstdint>

namespace {
std::atomic<std::uint64_t> g_calls{0};
std::atomic<std::uint64_t> g_frames{0};

void processIq(void *, std::uint32_t, const float *, std::size_t frames,
               double, std::int64_t, double)
{
    g_calls.fetch_add(1, std::memory_order_relaxed);
    g_frames.fetch_add(frames, std::memory_order_relaxed);
}

dsdr_iq_module_v1 g_module{
    DSDR_IQ_MODULE_ABI_VERSION,
    "test-iq-module",
    nullptr,
    nullptr,
    processIq,
};
} // namespace

extern "C" dsdr_iq_module_v1 *dsdr_create_iq_module_v1(void)
{
    return &g_module;
}

extern "C" std::uint64_t dsdr_test_iq_module_calls(void)
{
    return g_calls.load(std::memory_order_relaxed);
}

extern "C" std::uint64_t dsdr_test_iq_module_frames(void)
{
    return g_frames.load(std::memory_order_relaxed);
}
