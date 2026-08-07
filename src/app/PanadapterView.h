// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — spettro + waterfall su GPU (§6.1).
//
// Un solo QQuickRhiItem rende entrambi. La spec §6.2 immagina due componenti
// distinti, ma il feed è un ring SPSC a consumatore unico: due item che lo
// leggessero in parallelo si ruberebbero le righe a vicenda. Le sovrapposizioni
// (griglia, flag VFO, etichette) restano QML puro, sopra questo item.
//
// Waterfall: texture ad anello, una riga nuova per frame, scorrimento via
// offset UV — la storia non viene mai ridisegnata.
#pragma once

#include "core/SpectrumFeed.h"

#include <QColor>
#include <QQuickRhiItem>

#include <vector>

namespace dsdr::app {

class PanadapterView : public QQuickRhiItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PanadapterView)

    Q_PROPERTY(dsdr::core::SpectrumFeed *feed READ feed WRITE setFeed NOTIFY feedChanged)
    Q_PROPERTY(qreal floorDb READ floorDb WRITE setFloorDb NOTIFY levelRangeChanged)
    Q_PROPERTY(qreal ceilingDb READ ceilingDb WRITE setCeilingDb NOTIFY levelRangeChanged)
    Q_PROPERTY(qreal spectrumRatio READ spectrumRatio WRITE setSpectrumRatio NOTIFY spectrumRatioChanged)
    Q_PROPERTY(QColor traceColor READ traceColor WRITE setTraceColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY colorsChanged)
    Q_PROPERTY(qreal viewStart READ viewStart WRITE setViewStart NOTIFY viewChanged)
    Q_PROPERTY(qreal viewSpan READ viewSpan WRITE setViewSpan NOTIFY viewChanged)

public:
    explicit PanadapterView(QQuickItem *parent = nullptr);

    QQuickRhiItemRenderer *createRenderer() override;

    core::SpectrumFeed *feed() const { return m_feed; }
    void setFeed(core::SpectrumFeed *feed);

    qreal floorDb() const { return m_floorDb; }
    void setFloorDb(qreal db);
    qreal ceilingDb() const { return m_ceilingDb; }
    void setCeilingDb(qreal db);

    qreal spectrumRatio() const { return m_spectrumRatio; }
    void setSpectrumRatio(qreal ratio);

    QColor traceColor() const { return m_traceColor; }
    void setTraceColor(const QColor &color);
    QColor fillColor() const { return m_fillColor; }
    void setFillColor(const QColor &color);
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &color);

    /// Porzione di banda mostrata, come frazioni di 0..1. `viewStart` è il
    /// bordo sinistro, `viewSpan` la larghezza: insieme sono lo zoom.
    qreal viewStart() const { return m_viewStart; }
    void setViewStart(qreal value);
    qreal viewSpan() const { return m_viewSpan; }
    void setViewSpan(qreal value);

signals:
    void feedChanged();
    void viewChanged();
    void levelRangeChanged();
    void spectrumRatioChanged();
    void colorsChanged();

private:
    core::SpectrumFeed *m_feed = nullptr;
    QMetaObject::Connection m_feedConnection;

    qreal m_floorDb = -130.0;
    qreal m_ceilingDb = -20.0;
    qreal m_spectrumRatio = 0.42;
    QColor m_traceColor{0x6E, 0xE7, 0xFF};
    QColor m_fillColor{0x1E, 0x88, 0xC7};
    QColor m_backgroundColor{0x07, 0x0B, 0x11};
    qreal m_viewStart = 0.0;
    qreal m_viewSpan = 1.0;
};

} // namespace dsdr::app
