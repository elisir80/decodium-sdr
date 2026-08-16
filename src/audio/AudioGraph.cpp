// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/AudioGraph.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(dsdrAudio)

namespace dsdr::audio {

bool mayRoute(AudioTag tag, AudioSink sink)
{
    if (tag == AudioTag::Clean)
        return true;

    // `EarOnly` va soltanto all'orecchio. Non è una precauzione generica: la
    // registrazione audio di una banda serve anche a chi la riascolta per
    // decodificarla, e un file processato dalla rete porterebbe un segnale che
    // non c'era — o non porterebbe quello che c'era.
    return sink == AudioSink::Ear;
}

QString sinkName(AudioSink sink)
{
    switch (sink) {
    case AudioSink::Ear:            return QStringLiteral("orecchio");
    case AudioSink::AudioRecorder:  return QStringLiteral("registrazione audio");
    case AudioSink::NetworkStream:  return QStringLiteral("audio di rete");
    case AudioSink::DigitalDecoder: return QStringLiteral("decodificatori");
    case AudioSink::Transmit:       return QStringLiteral("trasmissione");
    }
    return QStringLiteral("destinazione sconosciuta");
}

bool AudioGraph::connect(const AudioNode &source, AudioSink sink, QString *why)
{
    if (!source.ring) {
        if (why)
            *why = QStringLiteral("%1 non ha un ring da cui leggere").arg(source.name);
        return false;
    }

    if (!mayRoute(source.tag, sink)) {
        const QString message =
            QStringLiteral("«%1» è passato dalla riduzione neurale e non può "
                           "andare a: %2. Una rete addestrata sulla voce toglie "
                           "ciò che alla voce non somiglia, e su un segnale "
                           "digitale al limite del rumore toglie il segnale — "
                           "senza che nessuno se ne accorga (SPEC-003 §8.3).")
                .arg(source.name, sinkName(sink));
        if (why)
            *why = message;
        qCWarning(dsdrAudio) << "rotta rifiutata:" << message;
        return false;
    }

    m_routes.append(QStringLiteral("%1 → %2").arg(source.name, sinkName(sink)));
    return true;
}

} // namespace dsdr::audio
