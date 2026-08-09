// SPDX-License-Identifier: GPL-3.0-or-later
// Catena di ricezione: quello che vale per tutta la banda, non per un canale.
//
// Un impulso — una scarica, un recinto elettrico, un alimentatore switching —
// non appartiene a un ricevitore: arriva su tutta la banda campionata e va
// tolto una volta sola, prima che i canali decimino la loro fetta
// (SPEC-003 §4). Per questo l'interruttore sta qui e non nella scheda del
// canale, dove pure era finito alla prima stesura.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("CATENA RX")
    draggable: true

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrButton {
            implicitWidth: 62
            implicitHeight: 26
            text: qsTr("NB")
            checkable: true
            checked: Session.noiseBlanker
            enabled: Session.connected
            onToggled: Session.setNoiseBlanker(checked, Session.noiseBlankerThreshold)
        }

        Text {
            text: qsTr("Soglia")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        // Il campo è quello della specifica: sotto 2 il blanker scambia per
        // impulso il segnale, sopra 8 non scatta più su niente.
        DsdrSlider {
            Layout.fillWidth: true
            from: 2; to: 8
            stepSize: 0.5
            enabled: Session.noiseBlanker
            value: Session.noiseBlankerThreshold
            onMoved: Session.setNoiseBlanker(true, value)
        }

        Text {
            text: Session.noiseBlankerThreshold.toFixed(1) + "×"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Session.noiseBlanker ? Theme.textPrimary : Theme.textDisabled
            Layout.preferredWidth: 38
        }
    }

    // ── Quanto sta lavorando ─────────────────────────────────────────────
    //
    // Un blanker acceso che non trova impulsi e uno che sta tagliuzzando
    // l'audio suonano diversi ma sembrano uguali: senza questa barra, l'unico
    // modo di scoprire di aver esagerato con la soglia è accorgersi che la
    // voce si è fatta ruvida, e a quel punto si dà la colpa alla propagazione.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight
        visible: Session.noiseBlanker

        Text {
            text: qsTr("Attività")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 6
            radius: 3
            color: Theme.surfaceSunken
            border.width: 1
            border.color: Theme.border

            Rectangle {
                // Una scarica sola tocca pochi campioni su centomila: la scala
                // è compressa, altrimenti la barra resterebbe sempre vuota e
                // sembrerebbe rotta. Oltre il 2 % di campioni ricuciti si sta
                // togliendo più di quanto si dovrebbe.
                width: parent.width * Math.min(1, Math.sqrt(Session.noiseBlankerActivity / 0.02))
                height: parent.height
                radius: parent.radius
                color: Session.noiseBlankerActivity > 0.02 ? Theme.warning : Theme.accent

                Behavior on width {
                    NumberAnimation { duration: Theme.animationFast }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: Session.noiseBlanker && Session.noiseBlankerActivity > 0.02
        text: qsTr("Il blanker sta togliendo molto: alza la soglia, o è la banda a essere così.")
        font.pixelSize: Theme.fontSmall
        color: Theme.warning
        wrapMode: Text.WordWrap
    }
}
