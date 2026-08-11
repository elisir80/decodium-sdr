// SPDX-License-Identifier: GPL-3.0-or-later
// Il mirino che legge lo spettro sotto il puntatore.
//
// Uno spettro senza mirino si legge a occhio contro la griglia: si vede che
// c'è qualcosa attorno ai 7.074 e che arriva «più o meno a metà scala». Sono
// due stime, e nessuna delle due si può passare a qualcuno. Il mirino dice la
// frequenza e il livello di quel bin, che sono la stessa cosa che direbbe il
// ricevitore se ci si sintonizzasse sopra — senza sintonizzarcisi sopra.
//
// Non riceve gesti: li riceve il pannello sotto, e questo si limita a
// disegnare dove gli si dice. Un altro item che prende il puntatore sopra lo
// spettro è il modo più rapido di rompere il click-to-tune, ed è già successo
// con i comandi del waterfall.
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Dove sta il puntatore, in pixel dentro il pannello. Negativo = fuori.
    property real cursorX: -1

    /// La lettura, che il pannello sa fare perché conosce lo zoom.
    property real cursorHz: 0
    property real cursorLevelDb: -140

    /// La frequenza del ricevitore scelto, per dire di quanto si è lontani.
    /// È l'informazione che si cerca davvero quando si punta un segnale: non
    /// «dove sta», ma «quanto devo spostarmi».
    property real referenceHz: 0

    /// Quota di altezza occupata dallo spettro, per fermare la linea al bordo
    /// fra spettro e waterfall — o lasciarla scendere, se si preferisce.
    property real spectrumRatio: 0.45

    /// Il righello: due frequenze e la distanza fra loro.
    ///
    /// Serve per una cosa sola ma che si fa continuamente: misurare quanto è
    /// larga una emissione, o quanto distano due stazioni. A occhio, contro una
    /// griglia da dieci kilohertz, quella misura non si fa.
    property real rulerFromHz: 0
    property bool rulerActive: false

    readonly property real cursorOffsetHz: cursorHz - referenceHz

    visible: cursorX >= 0
    // Non intercetta nulla: i gesti sono del pannello sotto.
    enabled: false

    // ── Linea verticale ──────────────────────────────────────────────────
    Rectangle {
        x: Math.round(root.cursorX)
        width: 1
        y: 0
        height: root.height
        color: Theme.textSecondary
        opacity: 0.55
    }

    // ── Tacca del livello sulla traccia ──────────────────────────────────
    //
    // Un puntino dove la traccia passa: senza, il numero letto qui sotto è un
    // numero e basta, e non si vede a quale cresta appartenga.
    Rectangle {
        readonly property real span: Math.max(root.ceilingDb - root.floorDb, 1)
        readonly property real fraction:
            Math.max(0, Math.min(1, (root.cursorLevelDb - root.floorDb) / span))

        x: Math.round(root.cursorX) - 3
        y: Math.round((1 - fraction) * root.height * root.spectrumRatio) - 3
        width: 7
        height: 7
        radius: 3.5
        color: "transparent"
        border.width: 1.5
        border.color: Theme.accent
        visible: root.cursorLevelDb > root.floorDb
    }

    /// Gli estremi della scala, per posizionare la tacca. Li dà chi disegna.
    property real floorDb: -130
    property real ceilingDb: -20

    // ── Righello ─────────────────────────────────────────────────────────
    Item {
        anchors.fill: parent
        visible: root.rulerActive

        // La banda misurata, evidenziata: la distanza fra due righe sottili si
        // valuta male, una fascia si vede.
        Rectangle {
            x: Math.min(root.rulerX, root.cursorX)
            width: Math.abs(root.cursorX - root.rulerX)
            y: 0
            height: parent.height
            color: Theme.accent
            opacity: 0.10
        }

        Rectangle {
            x: Math.round(root.rulerX)
            width: 1
            height: parent.height
            color: Theme.accent
            opacity: 0.7
        }
    }

    /// Dove cade a schermo l'estremo fisso del righello. Lo calcola chi
    /// conosce lo zoom e ce lo passa già in pixel.
    property real rulerX: -1

    // ── Lettura ──────────────────────────────────────────────────────────
    //
    // Sta accanto al puntatore e salta dall'altra parte quando il bordo è
    // vicino: un riquadro che esce dalla finestra è un riquadro che si legge a
    // metà proprio quando si sta misurando il segnale al margine della banda.
    Rectangle {
        id: readout

        readonly property bool onTheRight: root.cursorX + width + 18 < root.width

        x: onTheRight ? root.cursorX + 12 : root.cursorX - width - 12
        y: Theme.spacing
        width: column.implicitWidth + 2 * Theme.spacing
        height: column.implicitHeight + 2 * Theme.spacingTight
        radius: Theme.radiusSmall
        // Opaca: sotto scorre il waterfall, e un riquadro velato su un
        // waterfall in movimento non si legge.
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        Column {
            id: column
            anchors.centerIn: parent
            spacing: 1

            Text {
                text: (root.cursorHz / 1e6).toFixed(6) + qsTr(" MHz")
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textPrimary
            }

            Text {
                text: root.cursorLevelDb.toFixed(1) + qsTr(" dB")
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.accent
            }

            // Di quanto si è lontani dal ricevitore scelto. Con il segno,
            // perché la domanda è «di quanto e da che parte».
            Text {
                visible: root.referenceHz > 0
                text: (root.cursorOffsetHz >= 0 ? "+" : "−")
                      + (Math.abs(root.cursorOffsetHz) / 1000).toFixed(3) + qsTr(" kHz")
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textSecondary
            }

            Text {
                visible: root.rulerActive
                text: qsTr("Δ %1 kHz")
                      .arg((Math.abs(root.cursorHz - root.rulerFromHz) / 1000).toFixed(3))
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                font.bold: true
                color: Theme.spectrumPeak
            }
        }
    }
}
