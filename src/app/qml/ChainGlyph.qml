// SPDX-License-Identifier: GPL-3.0-or-later
// Il segno che ogni blocco porta addosso: cosa fa, disegnato.
//
// Un nome dice come si chiama uno stadio, non che cosa fa alla forma d'onda.
// «Gate» e «Limiter» sono due parole che a chi non le ha già imparate non
// dicono niente; una soglia con un gradino e una cima tosata si capiscono
// prima di averle lette.
//
// I tracciati sono quelli del disegno di riferimento, in un riquadro di 72×26
// che qui si scala al posto disponibile.
import QtQuick
import QtQuick.Shapes
import DecodiumSdr

Item {
    id: root

    /// Il tracciato, in coordinate del riquadro 72×26.
    property string path: ""
    property color stroke: Theme.accent
    property real thickness: 1.6

    implicitWidth: 72
    implicitHeight: 24

    Shape {
        id: shape

        width: 72
        height: 26
        // Il disegno nasce in un riquadro fisso e si adatta a quello che c'è:
        // così i glifi di due blocchi larghi diversamente restano confrontabili
        // fra loro, che è tutto il punto di avere un alfabeto.
        transform: Scale {
            xScale: root.width / 72
            yScale: root.height / 26
        }

        ShapePath {
            strokeColor: root.stroke
            strokeWidth: root.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg { path: root.path }
        }
    }
}
