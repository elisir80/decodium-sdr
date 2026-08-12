// SPDX-License-Identifier: GPL-3.0-or-later
// Il compressore multibanda: quattro bande e un comando solo (SPEC-005 §4.1).
//
// Sedici manopole — soglia, rapporto, attacco, rilascio per banda — sono il
// motivo per cui un multibanda resta spento nella maggior parte delle stazioni
// che ce l'hanno: nessuno sa da dove cominciare, e provare a caso su un
// trasmettitore è una cosa che si fa addosso agli altri.
//
// Un numero solo, «punch», muove le quattro soglie insieme con la mano di chi
// l'ha già fatto mille volte. Quello che resta da guardare è dove sta
// lavorando, e per quello ci sono le quattro colonne: dicono di quanto ogni
// parte della voce si sta abbassando, che è l'unica cosa che serve sapere per
// decidere se si sta esagerando.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

ColumnLayout {
    id: root

    spacing: Theme.spacingTight

    readonly property var bandNames: [
        qsTr("50–250"), qsTr("250–700"), qsTr("700–1.8k"), qsTr("1.8k–4k"),
    ]

    readonly property var bandRoles: [
        qsTr("corpo"), qsTr("calore"), qsTr("parola"), qsTr("presenza"),
    ]

    /// Il fondo scala delle colonne, in decibel di riduzione. Dodici sono
    /// tanti: oltre, la voce non è più compressa — è un'altra voce.
    readonly property real fullScaleDb: 12

    // ── Le quattro colonne ───────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 96
        spacing: Theme.spacing

        Repeater {
            model: Session.cfcBandCount

            delegate: ColumnLayout {
                id: band

                required property int index

                readonly property real reduction: {
                    const values = Session.cfcReduction
                    return index < values.length ? values[index] : 0
                }

                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: band.reduction > 0.1 ? qsTr("−%1").arg(band.reduction.toFixed(1)) : "—"
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: band.reduction > 8 ? Theme.danger
                         : band.reduction > 0.1 ? Theme.spectrumPeak
                         : Theme.textDisabled
                }

                // La colonna scende dall'alto, perché quello che misura è una
                // riduzione: più scende, più sta togliendo. Una barra che sale
                // per dire «sta togliendo di più» si legge al contrario.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignHCenter
                    radius: 3
                    color: Theme.surfaceSunken
                    border.width: 1
                    border.color: Theme.border
                    clip: true

                    Rectangle {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 1
                        height: Math.max(0, Math.min(1, band.reduction / root.fullScaleDb))
                                * (parent.height - 2)
                        radius: 2
                        color: band.reduction > 8 ? Theme.danger : Theme.spectrumPeak
                        opacity: 0.85

                        Behavior on height {
                            NumberAnimation { duration: 60 }
                        }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.bandNames[band.index]
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.bandRoles[band.index]
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textDisabled
                }
            }
        }
    }

    // ── Il comando solo ──────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: qsTr("PUNCH")
            font.pixelSize: Theme.fontSmall
            font.bold: true
            font.letterSpacing: 1.4
            color: Theme.spectrumPeak
        }

        DsdrSlider {
            Layout.fillWidth: true
            from: 0; to: 10
            value: Session.cfcPunch
            enabled: Session.cfcEnabled
            onMoved: Session.cfcPunch = value
        }

        Text {
            text: Session.cfcPunch.toFixed(1)
            font.pixelSize: Theme.fontNormal
            font.family: Theme.monoFamily
            font.bold: true
            color: Theme.textPrimary
        }
    }

    Text {
        Layout.fillWidth: true
        text: Session.cfcPunch < 1
              ? qsTr("A zero è trasparente: le soglie stanno dove quasi niente le tocca.")
              : Session.cfcPunch > 8
              ? qsTr("Compressione da pile-up: si sente, ed è quello che si vuole quando dall'altra parte c'è del rumore.")
              : qsTr("Attorno a cinque la voce guadagna corpo senza sentirsi lavorata.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
        wrapMode: Text.WordWrap
    }
}
