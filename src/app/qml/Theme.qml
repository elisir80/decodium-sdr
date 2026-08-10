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
    // La riga dei massimi: calda, per staccare dal ciano della traccia
    // istantanea senza gridare come il rosso della trasmissione.
    readonly property color spectrumPeak: "#FFC86E"

    // ── Display LCD ──────────────────────────────────────────────────────
    //
    // Il quadrante dello strumento non è una superficie dell'interfaccia: è un
    // vetro illuminato da dietro, e prende le sue tinte dal mondo degli
    // apparati, non da quello dei pannelli. Tenerle qui invece che dentro il
    // componente è la regola di sempre (CONSTITUTION §6); tenerle distinte da
    // `surface*` e `text*` è ciò che impedisce che un domani qualcuno le
    // «uniformi» al resto e spenga lo strumento.
    readonly property color lcdGlowCenter: "#2B3531"
    readonly property color lcdGlowMid: "#1D2623"
    readonly property color lcdGlowEdge: "#0F1513"
    readonly property color lcdEtch: "#E9E7E2"
    readonly property color lcdEtchDim: "#7F8A86"
    readonly property color lcdAlert: "#FF4A3D"
    readonly property color lcdCyan: "#4AA8FF"
    readonly property color lcdNeedle: "#F4F2EC"
    readonly property color lcdNeedleTail: "#8E918B"
    readonly property color lcdPivot: "#3A3D3A"

    /// Il riflesso sul vetro, dal bordo alto verso il centro, e il filo di
    /// luce sul bordo. Sono tinte trasparenti perché devono lasciar passare
    /// quello che hanno sotto: un riflesso opaco non è un riflesso.
    readonly property color lcdSheen: "#1AFFFFFF"
    readonly property color lcdSheenSoft: "#08FFFFFF"
    readonly property color lcdRim: "#1AFFFFFF"

    // ── Strumento di potenza ─────────────────────────────────────────────
    //
    // Le tre tinte di un wattmetro non sono decorazione: verde, ambra e rosso
    // dicono «va bene», «guarda» e «fermati» senza che si debba leggere un
    // numero, e chi trasmette guarda lo strumento con la coda dell'occhio.
    readonly property color meterSafe: "#46D67C"
    readonly property color meterCaution: "#FFB454"
    readonly property color meterDanger: "#FF4A4A"

    /// I segmenti spenti dell'arco: si devono vedere — sono la scala — senza
    /// competere con quelli accesi.
    readonly property color meterUnlit: "#14FFFFFF"
    readonly property color meterScale: "#3A424A"
    readonly property color meterScaleText: "#6A737C"
    readonly property color meterReadout: "#27C4D4"
    readonly property color meterDisplayBackground: "#000000"

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
