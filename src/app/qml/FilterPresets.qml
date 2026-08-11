// SPDX-License-Identifier: GPL-3.0-or-later
// Le larghezze di filtro d'uso, e come si applicano.
//
// Stavano dentro `ModeSelector`, che è la pulsantiera della colonna laterale.
// Poi le stesse scelte sono servite anche alla targa sopra lo spettro, e due
// copie della stessa tabella sono due tabelle che divergono al primo ritocco:
// si aggiunge una larghezza in CW da una parte e dall'altra no, e la stessa
// radio offre due cose diverse a seconda di dove la si guarda.
//
// La parte che è davvero facile sbagliare non è la tabella: è da che parte
// della portante mettere il passabanda. In LSB, CW reverse e DIGL sta sotto,
// e applicare una larghezza senza tenerne conto lo sposta dall'altra parte —
// il segnale che si stava ascoltando sparisce, e sembra un guasto.
pragma Singleton

import QtQuick

QtObject {
    /// Larghezze proposte, in hertz, per famiglia di modo.
    ///
    /// Sono quelle d'uso per il modo scelto: 500 Hz ha senso in CW e non in
    /// AM, e proporre sempre gli stessi otto valori vorrebbe dire farne
    /// cercare due ogni volta.
    readonly property var byFamily: {
        "cw":    [100, 250, 500, 1000],
        "ssb":   [1800, 2400, 2800, 3600],
        "am":    [4000, 6000, 9000, 12000],
        "fm":    [7000, 12000, 16000, 25000],
        "digi":  [500, 1000, 2400, 3000],
    }

    /// Gli indici seguono DemodMode: 0 USB, 1 LSB, 2 CW, 3 CWR, 4 AM, 5 SAM,
    /// 6 FM, 7 NFM, 8 DIGU, 9 DIGL, 10 IQ.
    function widthsFor(mode) {
        switch (mode) {
        case 2: case 3: return byFamily["cw"]
        case 4: case 5: return byFamily["am"]
        case 6: case 7: return byFamily["fm"]
        case 8: case 9: return byFamily["digi"]
        default:        return byFamily["ssb"]
        }
    }

    /// Da che parte della portante sta il passabanda.
    function isLowerSideband(mode) {
        return mode === 1 || mode === 3 || mode === 9
    }

    /// I modi centrati sulla portante: il filtro è simmetrico.
    function isSymmetric(mode) {
        return mode >= 4 && mode <= 7
    }

    /// Applica una larghezza al canale, conservando lo scostamento dalla
    /// portante: in CW è il tono di battimento, e cambiarlo mentre si stringe
    /// il filtro vorrebbe dire perdere la nota su cui si stava copiando.
    function applyWidth(channelIndex, mode, width, filterLowHz, filterHighHz) {
        if (isSymmetric(mode)) {
            Session.setChannelFilter(channelIndex, -width / 2, width / 2)
        } else if (isLowerSideband(mode)) {
            const edge = Math.min(-1, filterHighHz)
            Session.setChannelFilter(channelIndex, edge - width, edge)
        } else {
            const edge = Math.max(1, filterLowHz)
            Session.setChannelFilter(channelIndex, edge, edge + width)
        }
    }

    /// La larghezza come la si scrive su una radio.
    function label(widthHz) {
        return widthHz >= 1000 ? (widthHz / 1000).toFixed(1) + " kHz"
                               : widthHz + " Hz"
    }
}
