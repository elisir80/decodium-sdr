// SPDX-License-Identifier: GPL-3.0-or-later
// Modo e larghezza del filtro, a pulsanti.
//
// Una casella a discesa costa tre gesti — apri, cerca, scegli — per una cosa
// che si cambia di continuo e che ha undici valori in tutto: stanno tutti
// sotto gli occhi, e cambiare modo torna a essere un clic solo. È il motivo
// per cui le radio, da PowerSDR in poi, hanno una pulsantiera e non un menu.
//
// I filtri preimpostati sono quelli d'uso per il modo scelto: 500 Hz ha senso
// in CW e non in AM, e proporre sempre gli stessi otto valori vorrebbe dire
// farne cercare due ogni volta.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

ColumnLayout {
    id: root

    /// Riga del canale nel modello.
    required property int channelIndex

    /// Modo corrente, come indice di DemodMode.
    required property int mode

    /// Estremi del filtro, in hertz rispetto alla portante.
    required property int filterLowHz
    required property int filterHighHz

    spacing: Theme.spacingTight

    readonly property int filterWidthHz: Math.max(0, filterHighHz - filterLowHz)

    /// I nomi vengono dalla sessione: sono tradotti e in ordine di enum, così
    /// l'indice del pulsante è già l'indice del modo.
    readonly property var modeNames: Session.modeNames()

    /// Larghezze proposte, in hertz, per famiglia di modo.
    ///
    /// Gli indici seguono DemodMode: 0 USB, 1 LSB, 2 CW, 3 CWR, 4 AM, 5 SAM,
    /// 6 FM, 7 NFM, 8 DIGU, 9 DIGL, 10 IQ.
    readonly property var presetsFor: {
        "cw":    [100, 250, 500, 1000],
        "ssb":   [1800, 2400, 2800, 3600],
        "am":    [4000, 6000, 9000, 12000],
        "fm":    [7000, 12000, 16000, 25000],
        "digi":  [500, 1000, 2400, 3000],
    }

    readonly property var presets: {
        switch (mode) {
        case 2: case 3:            return presetsFor["cw"]
        case 4: case 5:            return presetsFor["am"]
        case 6: case 7:            return presetsFor["fm"]
        case 8: case 9:            return presetsFor["digi"]
        default:                   return presetsFor["ssb"]
        }
    }

    /// Da che parte della portante sta il passabanda.
    ///
    /// In LSB, CW reverse e DIGL il filtro sta sotto: applicare una larghezza
    /// senza tenerne conto sposterebbe il passabanda dall'altra parte e farebbe
    /// sparire il segnale che si stava ascoltando.
    readonly property bool lowerSideband: mode === 1 || mode === 3 || mode === 9

    /// I modi centrati sulla portante: il filtro è simmetrico.
    readonly property bool symmetric: mode >= 4 && mode <= 7

    function applyWidth(width) {
        if (symmetric) {
            Session.setChannelFilter(channelIndex, -width / 2, width / 2)
        } else if (lowerSideband) {
            // Si conserva lo scostamento dalla portante: in CW è il tono di
            // battimento, e cambiarlo mentre si stringe il filtro vorrebbe
            // dire perdere la nota su cui si stava copiando.
            const edge = Math.min(-1, root.filterHighHz)
            Session.setChannelFilter(channelIndex, edge - width, edge)
        } else {
            const edge = Math.max(1, root.filterLowHz)
            Session.setChannelFilter(channelIndex, edge, edge + width)
        }
    }

    // ── Modo ─────────────────────────────────────────────────────────────
    Text {
        text: qsTr("Modo")
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 4
        columnSpacing: Theme.spacingTight
        rowSpacing: Theme.spacingTight

        Repeater {
            model: root.modeNames

            delegate: DsdrButton {
                required property int index
                required property string modelData

                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                text: modelData
                checkable: true
                checked: root.mode === index
                // Con la demodulazione a bordo della radio il modo lo decide
                // lei: mostrarlo sì, spacciarlo per nostro no.
                enabled: Session.capabilities.clientDemod
                onClicked: Session.setChannelMode(root.channelIndex, index)
            }
        }
    }

    // ── Larghezza ────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        visible: Session.capabilities.clientDemod

        Text {
            text: qsTr("Filtro")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Item { Layout.fillWidth: true }

        Text {
            text: root.filterLowHz + " … " + root.filterHighHz + " Hz"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textDisabled
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight
        visible: Session.capabilities.clientDemod

        Repeater {
            model: root.presets

            delegate: DsdrButton {
                required property int modelData

                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                text: modelData >= 1000
                      ? (modelData / 1000).toFixed(modelData % 1000 === 0 ? 1 : 1) + "k"
                      : String(modelData)
                checkable: true
                // Tolleranza di cinquanta hertz: il passo dei cursori è
                // quello, e un preset che non si accende mai perché il valore
                // differisce di un'inezia è peggio che inutile.
                checked: Math.abs(root.filterWidthHz - modelData) <= 50
                onClicked: root.applyWidth(modelData)
            }
        }
    }
}
