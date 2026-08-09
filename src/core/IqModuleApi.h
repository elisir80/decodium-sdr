// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — ABI C minimale per moduli IQ esterni.
//
// Il contratto usa solo tipi C primitivi: un modulo compilato fuori dal
// repository non deve includere Qt, libstdc++ o gli header interni del DSP.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DSDR_IQ_MODULE_ABI_VERSION 1u

typedef struct dsdr_iq_module_v1 {
    uint32_t abi_version;
    const char *name;
    void *user;

    /// Distrugge lo stato creato dal modulo. Può essere NULL.
    void (*destroy)(void *user);

    /// Riceve il baseband complesso filtrato di un canale RX.
    ///
    /// `iq_interleaved` contiene `frames` coppie I/Q float, al rate indicato.
    /// Il callback gira sul thread DSP: non deve bloccare, allocare o
    /// modificare il buffer, che resta valido solo durante la chiamata.
    void (*process_iq)(void *user,
                       uint32_t channel_id,
                       const float *iq_interleaved,
                       size_t frames,
                       double sample_rate,
                       int64_t center_frequency_hz,
                       double channel_offset_hz);
} dsdr_iq_module_v1;

typedef dsdr_iq_module_v1 *(*dsdr_create_iq_module_v1_fn)(void);

#ifdef __cplusplus
}
#endif
