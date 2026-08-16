// SPDX-License-Identifier: GPL-3.0-or-later
// Limiti fisici condivisi delle chiavette RTL2832 nel percorso HF.
#pragma once

#include <QString>
#include <QtGlobal>

namespace dsdr::hal::rtlsdr {

// Il Q ADC del RTL2832 non passa dal tuner. Sulle chiavette convenzionali è
// utile in HF, non oltre il confine con il percorso tuner. Il Blog V4 è un
// caso diverso: il suo upconverter HF è nel percorso tuner e l'antenna non
// arriva al Q ADC in modo utilizzabile.
inline constexpr qint64 kDirectSamplingMinimumFrequencyHz = 500'000;
inline constexpr qint64 kDirectSamplingMaximumFrequencyHz = 24'000'000;

enum class DirectSamplingBlockReason {
    None,
    BlogV4UsesUpconverter,
    OutsideHfRange,
};

inline bool isRtlSdrBlogV4Identity(const QString &identity)
{
    return identity.contains(QStringLiteral("Blog V4"), Qt::CaseInsensitive);
}

inline bool isDirectSamplingFrequency(qint64 frequencyHz)
{
    return frequencyHz >= kDirectSamplingMinimumFrequencyHz
        && frequencyHz <= kDirectSamplingMaximumFrequencyHz;
}

inline DirectSamplingBlockReason directSamplingBlockReason(const QString &deviceIdentity,
                                                            qint64 frequencyHz)
{
    if (isRtlSdrBlogV4Identity(deviceIdentity))
        return DirectSamplingBlockReason::BlogV4UsesUpconverter;
    if (!isDirectSamplingFrequency(frequencyHz))
        return DirectSamplingBlockReason::OutsideHfRange;
    return DirectSamplingBlockReason::None;
}

} // namespace dsdr::hal::rtlsdr
