// SPDX-License-Identifier: GPL-3.0-or-later
// Tema DECODIUM dark — coerente con DECODIUM 4 "Core Shannon".
//
// CONSTITUTION §6: nessun colore hardcoded nei componenti. Se serve una tinta
// nuova, si aggiunge qui e la si usa per nome.
pragma Singleton

import QtQuick

QtObject {
    // ── Superfici ────────────────────────────────────────────────────────
    readonly property color background: "#070B11"
    readonly property color surface: "#0E141C"
    readonly property color surfaceRaised: "#141C27"
    readonly property color surfaceSunken: "#050810"
    readonly property color border: "#1E2A38"
    readonly property color borderStrong: "#2C3E52"

    // ── Testo ────────────────────────────────────────────────────────────
    readonly property color textPrimary: "#E6EDF5"
    readonly property color textSecondary: "#8FA3B8"
    readonly property color textDisabled: "#4C5D71"

    // ── Accenti ──────────────────────────────────────────────────────────
    readonly property color accent: "#6EE7FF"
    readonly property color accentDim: "#1E88C7"
    readonly property color success: "#81C784"
    readonly property color warning: "#FFB34D"
    readonly property color danger: "#E57373"
    readonly property color transmit: "#FF5252"

    // ── Spettro ──────────────────────────────────────────────────────────
    readonly property color spectrumBackground: "#070B11"
    readonly property color spectrumTrace: "#6EE7FF"
    readonly property color spectrumFill: "#1E88C7"
    readonly property color spectrumGrid: "#16202C"

    // ── Segmenti del piano bande ─────────────────────────────────────────
    //
    // Tinte tenute basse di proposito: la striscia sta sotto lo spettro e deve
    // farsi leggere con la coda dell'occhio, non competere con i segnali.
    readonly property color segmentCw: "#1E88C7"
    readonly property color segmentDigi: "#FFB34D"
    readonly property color segmentBeacon: "#B388FF"
    readonly property color segmentPhone: "#81C784"

    /// Opacità con cui le tinte dei segmenti vengono stese.
    readonly property real segmentOpacity: 0.22

    // ── Metriche ─────────────────────────────────────────────────────────
    readonly property int spacingTight: 4
    readonly property int spacing: 8
    readonly property int spacingLoose: 16
    readonly property int radius: 6
    readonly property int radiusSmall: 3
    readonly property int controlHeight: 30
    readonly property int headerHeight: 52

    // ── Tipografia ───────────────────────────────────────────────────────
    readonly property string monoFamily: "Cascadia Mono, Consolas, DejaVu Sans Mono, monospace"
    readonly property int fontSmall: 11
    readonly property int fontNormal: 13
    readonly property int fontLarge: 16
    readonly property int fontDisplay: 30

    readonly property int animationFast: 120
    readonly property int animationNormal: 220
}
