// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/PanadapterView.h"
#include "app/PanadapterRenderer.h"

namespace dsdr::app {

PanadapterView::PanadapterView(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    setMirrorVertically(false);
    setSampleCount(1); // il waterfall non ha bordi da antialiasare
}

QQuickRhiItemRenderer *PanadapterView::createRenderer()
{
    return new PanadapterRenderer;
}

void PanadapterView::setFeed(core::SpectrumFeed *feed)
{
    if (m_feed == feed)
        return;

    disconnect(m_feedConnection);
    m_feed = feed;

    if (m_feed) {
        // Ridisegniamo solo quando c'è davvero una riga nuova: nessun timer
        // che consuma GPU mentre la banda è ferma.
        m_feedConnection = connect(m_feed, &core::SpectrumFeed::framesAvailable,
                                   this, [this] { update(); });
        m_feed->setLevelRange(static_cast<float>(m_floorDb), static_cast<float>(m_ceilingDb));
    }

    emit feedChanged();
    update();
}

void PanadapterView::setFloorDb(qreal db)
{
    if (qFuzzyCompare(m_floorDb, db))
        return;
    m_floorDb = db;
    if (m_feed)
        m_feed->setLevelRange(static_cast<float>(m_floorDb), static_cast<float>(m_ceilingDb));
    emit levelRangeChanged();
    update();
}

void PanadapterView::setCeilingDb(qreal db)
{
    if (qFuzzyCompare(m_ceilingDb, db))
        return;
    m_ceilingDb = db;
    if (m_feed)
        m_feed->setLevelRange(static_cast<float>(m_floorDb), static_cast<float>(m_ceilingDb));
    emit levelRangeChanged();
    update();
}

void PanadapterView::setSpectrumRatio(qreal ratio)
{
    ratio = qBound(0.1, ratio, 0.9);
    if (qFuzzyCompare(m_spectrumRatio, ratio))
        return;
    m_spectrumRatio = ratio;
    emit spectrumRatioChanged();
    update();
}

void PanadapterView::setViewStart(qreal value)
{
    // Il bordo sinistro non può portare la finestra fuori dalla banda.
    value = qBound(0.0, value, 1.0 - m_viewSpan);
    if (qFuzzyCompare(m_viewStart, value))
        return;
    m_viewStart = value;
    emit viewChanged();
    update();
}

void PanadapterView::setViewSpan(qreal value)
{
    // Sotto lo 0,5% di banda non si guadagna leggibilità: i bin dello spettro
    // finiscono per essere meno dei pixel.
    value = qBound(0.005, value, 1.0);
    if (qFuzzyCompare(m_viewSpan, value))
        return;
    m_viewSpan = value;
    if (m_viewStart + m_viewSpan > 1.0)
        m_viewStart = 1.0 - m_viewSpan;
    emit viewChanged();
    update();
}

void PanadapterView::setTraceColor(const QColor &color)
{
    if (m_traceColor == color)
        return;
    m_traceColor = color;
    emit colorsChanged();
    update();
}

void PanadapterView::setFillColor(const QColor &color)
{
    if (m_fillColor == color)
        return;
    m_fillColor = color;
    emit colorsChanged();
    update();
}

void PanadapterView::setBackgroundColor(const QColor &color)
{
    if (m_backgroundColor == color)
        return;
    m_backgroundColor = color;
    emit colorsChanged();
    update();
}

} // namespace dsdr::app
