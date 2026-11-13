// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    required property var controller
    title: qsTr("Ricezione ADS-B 1090 MHz — sperimentale")
    anchors.centerIn: parent
    modal: false
    width: 520
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 10
        CheckBox {
            text: qsTr("Abilita decoder ADS-B")
            checked: root.controller.enabled
            onToggled: root.controller.enabled = checked
        }
        Text { Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.WordWrap; text: root.controller.status }
        Text { Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("Imposta manualmente 1090 MHz e 2.000.000 campioni/s. Mostra i dati dell’ultima trama DF17/DF18 valida e la posizione aerea da coppie CPR recenti. Non vengono modificate sintonia o trasmissione.") }
        Text { color: Theme.textPrimary; text: qsTr("Trame valide: %1 · perse in coda: %2").arg(root.controller.frameCount).arg(root.controller.dropped) }
        Text { objectName: "adsbIcao"; color: Theme.textPrimary; text: qsTr("ICAO: %1").arg(root.controller.icao || qsTr("sconosciuto")) }
        Text { objectName: "adsbType"; color: Theme.textPrimary; text: qsTr("Tipo ADS-B: %1").arg(root.controller.typeCode >= 0 ? root.controller.typeCode : qsTr("sconosciuto")) }
        Text { objectName: "adsbCallsign"; color: Theme.textPrimary; text: qsTr("Nominativo: %1").arg(root.controller.callsign || qsTr("sconosciuto")) }
        Text { objectName: "adsbAltitude"; color: Theme.textPrimary; text: qsTr("Quota barometrica: %1").arg(root.controller.hasBarometricAltitude ? qsTr("%1 ft").arg(root.controller.barometricAltitudeFeet) : qsTr("sconosciuta")) }
        Text { objectName: "adsbSpeed"; color: Theme.textPrimary; text: qsTr("Velocità al suolo: %1").arg(root.controller.hasGroundSpeed ? qsTr("%1 kt").arg(root.controller.groundSpeedKnots.toFixed(1)) : qsTr("non disponibile")) }
        Text { objectName: "adsbTrack"; color: Theme.textPrimary; text: qsTr("Rotta al suolo: %1").arg(root.controller.hasGroundTrack ? qsTr("%1°").arg(root.controller.groundTrackDegrees.toFixed(1)) : qsTr("non disponibile")) }
        Text { objectName: "adsbAirspeed"; color: Theme.textPrimary; text: qsTr("Velocità aria: %1").arg(root.controller.hasAirspeed ? qsTr("%1 kt (%2)").arg(root.controller.airspeedKnots.toFixed(1)).arg(root.controller.trueAirspeed ? "TAS" : "IAS") : qsTr("non disponibile")) }
        Text { objectName: "adsbHeading"; color: Theme.textPrimary; text: qsTr("Prua magnetica: %1").arg(root.controller.hasMagneticHeading ? qsTr("%1°").arg(root.controller.magneticHeadingDegrees.toFixed(1)) : qsTr("non disponibile")) }
        Text { objectName: "adsbVerticalRate"; color: Theme.textPrimary; text: qsTr("Velocità verticale: %1").arg(root.controller.hasVerticalRate ? qsTr("%1 ft/min").arg(root.controller.verticalRateFeetPerMinute) : qsTr("non disponibile")) }
        Text { objectName: "adsbPosition"; color: Theme.textPrimary; text: qsTr("Posizione: %1").arg(root.controller.hasPosition ? qsTr("%1°, %2°").arg(root.controller.latitude.toFixed(5)).arg(root.controller.longitude.toFixed(5)) : qsTr("non disponibile")) }
        Text { Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.WrapAnywhere; text: qsTr("Ultima trama: %1").arg(root.controller.lastFrame || "—") }
    }
}
