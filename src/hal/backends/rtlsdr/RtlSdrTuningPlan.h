// SPDX-License-Identifier: GPL-3.0-or-later
// Piano di sintonia RTL-SDR: RF normale, Q ADC e uscita IF fissa.
#pragma once

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace dsdr::hal::rtlsdr {

enum class IfSideband {
    Usb,
    Lsb,
};

struct TuningRequest
{
    qint64 dialFrequencyHz = 14'074'000;
    double sampleRate = 2'048'000.0;
    bool ifEnabled = false;
    qint64 ifFrequencyHz = 8'830'000;
    qint64 usbShiftHz = 1'500;
    qint64 lsbShiftHz = -1'500;
    IfSideband sideband = IfSideband::Usb;
    bool spectrumInverted = false;

    // Posizione del VFO che ascolta la radio nell'attuale panoramica logica.
    // Con IF fissa l'IF non segue il centro del panadapter: segue il VFO RX.
    qint64 logicalSelectedOffsetHz = 0;
};

struct TuningPlan
{
    qint64 dialFrequencyHz = 0;          ///< Frequenza che resta visibile all'operatore.
    qint64 selectedInputFrequencyHz = 0; ///< Frequenza fisica scelta dentro l'IF/RF.
    qint64 hardwareCenterFrequencyHz = 0;
    qint64 channelOffsetHz = 0;          ///< selectedInput - hardwareCenter
    qint64 logicalSelectedOffsetHz = 0;  ///< selected radio VFO - centro logico

    // Il worker trasla la portante al suo offset logico prima di consegnare
    // l'IQ al DSP. Perciò il core continua a vedere il VFO radio come centro
    // dello spettro anche se la chiavetta è collegata a una IF fissa.
    double basebandTranslationHz = 0.0;
    qint64 logicalCenterFrequencyHz = 0;

    bool ifEnabled = false;
    bool spectrumInverted = false;
};

inline TuningPlan makeTuningPlan(const TuningRequest &request,
                                 qint64 minimumHardwareFrequencyHz,
                                 qint64 maximumHardwareFrequencyHz)
{
    TuningPlan plan;
    plan.ifEnabled = request.ifEnabled;
    plan.spectrumInverted = request.ifEnabled && request.spectrumInverted;
    plan.dialFrequencyHz = std::clamp(request.dialFrequencyHz,
                                      minimumHardwareFrequencyHz,
                                      maximumHardwareFrequencyHz);

    const qint64 shift = request.sideband == IfSideband::Lsb
        ? request.lsbShiftHz : request.usbShiftHz;
    const qint64 requestedInput = request.ifEnabled
        ? request.ifFrequencyHz + shift : plan.dialFrequencyHz;
    plan.selectedInputFrequencyHz = std::clamp(requestedInput,
                                                minimumHardwareFrequencyHz,
                                                maximumHardwareFrequencyHz);

    // Per RF diretto si allontana il segnale scelto dal punto zero: evita lo
    // spur DC e lascia una banda utile simmetrica attorno al VFO. Un'uscita
    // IF e' differente: il ricevitore fornisce gia' una finestra stretta e
    // stabile, centrata su IF +/- offset USB/LSB. In quel caso il tuner deve
    // restare esattamente su `selectedInput`, come SDR++ in modalita'
    // panadapter. Traslare ulteriormente il LO di un quarto del sample rate
    // ha mostrato un errore residuo rispetto al ricevitore reale.
    qint64 hardwareCenter = plan.selectedInputFrequencyHz;
    if (!request.ifEnabled) {
        const qint64 preferredOffset = std::max<qint64>(12'000,
            qRound64(std::abs(request.sampleRate) / 4.0));
        hardwareCenter += preferredOffset;
        if (hardwareCenter > maximumHardwareFrequencyHz)
            hardwareCenter = plan.selectedInputFrequencyHz - preferredOffset;
    }
    plan.hardwareCenterFrequencyHz = std::clamp(hardwareCenter,
                                                 minimumHardwareFrequencyHz,
                                                 maximumHardwareFrequencyHz);
    plan.channelOffsetHz = plan.selectedInputFrequencyHz - plan.hardwareCenterFrequencyHz;
    plan.logicalSelectedOffsetHz = request.ifEnabled ? request.logicalSelectedOffsetHz : 0;

    // L'uscita IF della radio viene già centrata dal tuner su IF +/- shift
    // USB/LSB, proprio come SDR++. Un secondo mixer qui sposterebbe la banda
    // laterale una seconda volta: con USB +1500 Hz si doveva infatti
    // sintonizzare Decodium 1,5 kHz più in basso della radio. L'inversione
    // IQ raddrizza soltanto il verso dello spettro; gli spostamenti dei VFO
    // virtuali restano al DDC del ChannelProcessor.
    if (request.ifEnabled) {
        plan.basebandTranslationHz = 0.0;
    } else {
        plan.basebandTranslationHz = plan.spectrumInverted
            ? static_cast<double>(plan.logicalSelectedOffsetHz + plan.channelOffsetHz)
            : static_cast<double>(plan.logicalSelectedOffsetHz - plan.channelOffsetHz);
    }
    plan.logicalCenterFrequencyHz = request.ifEnabled
        ? plan.dialFrequencyHz : plan.selectedInputFrequencyHz;
    return plan;
}

} // namespace dsdr::hal::rtlsdr
