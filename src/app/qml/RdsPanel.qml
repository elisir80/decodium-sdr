// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — pannello dati RDS per un canale Wide-FM.
//
// Il decoder RDS vive nel DSP e aggiorna il modello dei canali. Questo
// componente è la vista esplicita di quei dati: senza di lui PS e RadioText
// arrivano correttamente fino a QML, ma restano invisibili all'operatore.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    required property int channelIndex
    required property bool fmStereo
    required property bool fmRds
    required property bool rdsAutomaticAf
    required property int rdsRegion
    required property bool rdsSynced
    required property int rdsCountryCode
    required property int rdsProgramCoverage
    required property int rdsReferenceNumber
    required property string rdsPi
    required property string rdsCallsign
    required property string rdsProgramType
    required property string rdsAlternateFrequencies
    required property string rdsProgramService
    required property string rdsRadioText

    readonly property bool wideFm: true

    objectName: "rds-panel"
    Layout.fillWidth: true
    implicitHeight: body.implicitHeight + 2 * Theme.spacing
    radius: Theme.radiusSmall
    color: Theme.surfaceSunken
    border.width: 1
    border.color: rdsSynced && fmRds ? Theme.accent : Theme.border

    ColumnLayout {
        id: body

        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacingTight

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: qsTr("FM BROADCAST · RDS")
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                font.bold: true
                color: Theme.textSecondary
            }

            Item { Layout.fillWidth: true }

            Text {
                objectName: "rds-status"
                text: !root.fmRds ? qsTr("DISATTIVO")
                      : root.rdsSynced ? qsTr("SYNC") : qsTr("RICERCA")
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                font.bold: true
                color: !root.fmRds ? Theme.textDisabled
                      : root.rdsSynced ? Theme.success : Theme.warning
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight

            DsdrButton {
                objectName: "rds-stereo"
                Layout.fillWidth: true
                implicitHeight: 24
                text: qsTr("STEREO")
                checkable: true
                checked: root.fmStereo
                onToggled: Session.setChannelFmStereo(root.channelIndex, checked)
            }

            DsdrButton {
                objectName: "rds-enable"
                Layout.fillWidth: true
                implicitHeight: 24
                text: qsTr("RDS")
                checkable: true
                checked: root.fmRds
                onToggled: Session.setChannelFmRds(root.channelIndex, checked)
            }

            Text {
                text: qsTr("PTY")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            Repeater {
                model: [qsTr("EU"), qsTr("US")]

                delegate: DsdrButton {
                    required property int index
                    required property string modelData

                    implicitWidth: 38
                    implicitHeight: 24
                    text: modelData
                    checkable: true
                    checked: root.rdsRegion === index
                    onClicked: Session.setChannelRdsRegion(root.channelIndex, index)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight
            enabled: root.fmRds
            opacity: root.fmRds ? 1.0 : 0.55

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight

                Text {
                    text: qsTr("PS")
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                    Layout.preferredWidth: 48
                }

                Text {
                    objectName: "rds-ps"
                    Layout.fillWidth: true
                    text: root.rdsProgramService.trim() || "—"
                    font.pixelSize: Theme.fontNormal
                    font.bold: true
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight

                Text {
                    text: qsTr("RT")
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                    Layout.preferredWidth: 48
                }

                Text {
                    objectName: "rds-radio-text"
                    Layout.fillWidth: true
                    text: root.rdsRadioText.trim() || "—"
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: Theme.spacingTight
                rowSpacing: 2

                Text { text: qsTr("PI"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                Text {
                    objectName: "rds-pi"
                    text: root.rdsPi || "—"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    Layout.fillWidth: true
                }

                Text { text: qsTr("PTY"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                Text {
                    objectName: "rds-pty"
                    text: root.rdsProgramType || "—"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text { text: qsTr("AF"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                Text {
                    objectName: "rds-af"
                    text: root.rdsAlternateFrequencies || "—"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text { text: qsTr("CALL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                Text {
                    objectName: "rds-callsign"
                    text: root.rdsCallsign || "—"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight

                Text {
                    Layout.fillWidth: true
                    text: root.rdsSynced
                          ? qsTr("PI %1 · copertura %2 · riferimento %3")
                                .arg(root.rdsPi || "—")
                                .arg(root.rdsProgramCoverage >= 0 ? root.rdsProgramCoverage : "—")
                                .arg(root.rdsReferenceNumber >= 0 ? root.rdsReferenceNumber : "—")
                          : qsTr("Nessun gruppo RDS valido ricevuto")
                    font.pixelSize: Theme.fontSmall
                    color: root.rdsSynced ? Theme.textDisabled : Theme.warning
                    elide: Text.ElideRight
                }

                DsdrButton {
                    objectName: "rds-af-follow"
                    implicitWidth: 72
                    implicitHeight: 24
                    text: qsTr("SEGUI AF")
                    enabled: root.rdsSynced && root.rdsAlternateFrequencies.length > 0
                    onClicked: Session.followRdsAf(root.channelIndex)
                }

                DsdrButton {
                    objectName: "rds-af-auto"
                    implicitWidth: 72
                    implicitHeight: 24
                    text: qsTr("AF AUTO")
                    checkable: true
                    checked: root.rdsAutomaticAf
                    enabled: root.rdsSynced
                    onToggled: Session.setChannelRdsAutomaticAf(root.channelIndex, checked)
                }
            }
        }
    }
}
