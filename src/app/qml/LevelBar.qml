// SPDX-License-Identifier: GPL-3.0-or-later
// Barra di livello 0…1, con una soglia oltre la quale cambia colore.
//
// La scala è la radice del valore e non il valore: un indicatore lineare su un
// segnale audio passa il tempo schiacciato in fondo a sinistra, e chi lo
// guarda conclude che il microfono non funziona. La radice avvicina il
// comportamento a quello di un indicatore in decibel senza il salto a −∞ del
// logaritmo quando il segnale è zero.
import QtQuick
import DecodiumSdr

Rectangle {
    id: root

    /// Livello da mostrare, 0…1.
    property real value: 0.0

    /// Sopra questo livello la barra passa al colore d'avvertimento.
    property real warnAbove: 0.9

    implicitHeight: 8
    radius: 4
    color: Theme.surfaceSunken
    border.width: 1
    border.color: Theme.border

    Rectangle {
        width: parent.width * Math.min(1, Math.sqrt(Math.max(0, root.value)))
        height: parent.height
        radius: parent.radius
        color: root.value >= root.warnAbove ? Theme.danger : Theme.accent

        // Discesa animata, salita immediata: un picco che si vede a metà non
        // è un picco, e un indicatore che scende di scatto sfarfalla.
        Behavior on width {
            enabled: root.value < root.warnAbove
            NumberAnimation { duration: Theme.animationFast }
        }
    }
}
