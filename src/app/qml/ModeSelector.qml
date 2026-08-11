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

    /// Larghezze e regole di applicazione stanno in [FilterPresets]: le usa
    /// anche la targa sopra lo spettro, e due copie della stessa tabella
    /// divergono al primo ritocco.
    readonly property var presets: FilterPresets.widthsFor(mode)

    function applyWidth(width) {
        FilterPresets.applyWidth(channelIndex, mode, width,
                                 root.filterLowHz, root.filterHighHz)
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
                fontSize: Theme.fontSmall
                text: modelData
                checkable: true
                checked: root.mode === index
                // Il modo si sceglie sempre. Cambia solo chi lo esegue: con un
                // backend raw-IQ lo esegue il nostro demodulatore, con una
                // radio tradizionale glielo si comanda via CAT. In entrambi i
                // casi è una scelta dell'operatore, e disabilitarla lasciava
                // una fila di pulsanti grigi davanti a una radio che il modo
                // lo cambia benissimo.
                onClicked: Session.setChannelMode(root.channelIndex, index)
            }
        }
    }

    // ── Larghezza ────────────────────────────────────────────────────────
    //
    // Il filtro è nostro anche quando a demodulare è la radio: la sua passata
    // è il limite esterno, la nostra sta dentro. Stringere a 500 Hz su un
    // FT-991A restringe davvero quello che si sente.
    RowLayout {
        Layout.fillWidth: true

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

        Repeater {
            model: root.presets

            delegate: DsdrButton {
                required property int modelData

                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                fontSize: Theme.fontSmall
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
