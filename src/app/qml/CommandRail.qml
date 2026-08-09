// SPDX-License-Identifier: GPL-3.0-or-later
// Colonna dei comandi rapidi, a sinistra dello spettro.
//
// Cinquantasei punti di larghezza per i gesti che si ripetono. Non ruba spazio
// allo spettro come farebbe un pannello, e non costringe a cercare dentro un
// menu quello che si fa ogni due minuti.
//
// Qui stanno solo comandi che fanno qualcosa: le voci che il progetto non ha
// ancora — le memorie, per esempio — non compaiono. Un pulsante che non
// risponde è peggio di un pulsante che manca.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    /// Il panadattatore, per i comandi che riguardano la vista.
    required property PanadapterView panadapter

    /// Lo spettro, per il ritorno a piena banda.
    required property Item scope

    implicitWidth: 56
    color: Theme.surface

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    /// Un comando: icona sopra, etichetta sotto, tutta la larghezza sensibile.
    component RailButton: Rectangle {
        id: button

        property string glyph
        property string label
        property bool active: false
        property bool danger: false

        signal clicked()

        Layout.fillWidth: true
        implicitHeight: 42
        radius: Theme.radiusSmall
        color: !button.enabled ? "transparent"
             : hover.hovered ? Theme.surfaceRaised
             : button.active ? Theme.surfaceRaised : "transparent"
        border.width: button.active ? 1 : 0
        border.color: Theme.borderStrong

        readonly property color tint:
            !button.enabled ? Theme.textDisabled
            : button.danger ? Theme.danger
            : button.active ? Theme.accent
            : Theme.textSecondary

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 1

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: button.glyph
                font.pixelSize: Theme.fontLarge
                color: button.tint
            }

            // L'etichetta è facoltativa: dove il glifo è già una parola —
            // «12k» — ripetere l'unità sotto a ogni pulsante è rumore che
            // allunga la colonna senza aggiungere niente.
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: button.label.length > 0
                text: button.label
                font.pixelSize: 8
                font.letterSpacing: 0.6
                color: button.tint
            }
        }

        HoverHandler {
            id: hover
            enabled: button.enabled
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: button.enabled
            onTapped: button.clicked()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingTight
        spacing: Theme.spacingTight

        RailButton {
            glyph: "＋"
            label: qsTr("RX")
            enabled: Session.connected
                     && Session.channels.count < Session.capabilities.maxRxChannels
            onClicked: Session.addChannel(Session.centerFrequency)
        }

        RailButton {
            glyph: "⤢"
            label: qsTr("TUTTA")
            enabled: root.scope.viewSpan < 0.999
            onClicked: root.scope.resetZoom()
        }

        // Larghezze di vista preimpostate. Cercare lo zoom giusto con la
        // rotellina, un frammento alla volta, è il modo lento di arrivare a
        // una manciata di valori che si usano sempre gli stessi.
        //
        // Una larghezza più grande della banda campionata non esiste: il
        // pulsante non si accende, invece di portare a una vista identica a
        // «tutta» e far credere che non funzioni.
        Repeater {
            model: [12000, 24000, 48000, 96000]

            delegate: RailButton {
                required property int modelData

                readonly property real fraction:
                    Session.sampleRate > 0 ? modelData / Session.sampleRate : 1

                glyph: modelData >= 1000 ? (modelData / 1000) + "k" : String(modelData)
                label: ""
                implicitHeight: 30
                enabled: Session.connected && fraction < 0.999
                active: Math.abs(root.scope.viewSpan - fraction) < 0.01
                onClicked: root.scope.zoomToSpan(modelData)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 2
            height: 1
            color: Theme.border
        }

        RailButton {
            glyph: "▤"
            label: qsTr("PIATTO")
            active: root.panadapter.waterfallMode === PanadapterView.Flat
            onClicked: root.panadapter.waterfallMode = PanadapterView.Flat
        }

        RailButton {
            glyph: "◭"
            label: qsTr("RILIEVO")
            active: root.panadapter.waterfallMode === PanadapterView.Relief
            onClicked: root.panadapter.waterfallMode = PanadapterView.Relief
        }

        RailButton {
            glyph: "◐"
            label: qsTr("AUTO")
            active: root.panadapter.autoRange
            onClicked: root.panadapter.autoRange = !root.panadapter.autoRange
        }

        // La registrazione non sta qui: vive nella barra di testa accanto al
        // PTT, con gli altri comandi che mandano qualcosa in onda o su disco.
        // Averla in due posti sarebbe solo un modo per non sapere mai quale
        // dei due dice la verità.
        Item { Layout.fillHeight: true }
    }
}
