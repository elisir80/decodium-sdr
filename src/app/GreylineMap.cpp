// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/GreylineMap.h"

#include <QDataStream>
#include <QFile>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace dsdr::app {

namespace {

/// Le coste stanno nella risorsa in due interi a sedici bit per punto, in
/// centesimi di grado. Un centesimo di grado è poco più di un chilometro: su
/// una mappa larga mille punti è un decimo di pixel, e più precisione sarebbe
/// byte spesi per niente.
constexpr const char *kCoastlineResource = ":/data/coste.bin";

bool isValid(const QPointF &p)
{
    return !std::isnan(p.x()) && !std::isnan(p.y());
}

} // namespace

GreylineMap::GreylineMap(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    // Il rendering nel framebuffer e non in un'immagine: la mappa occupa mezzo
    // pannello e la copia della QImage a ogni frame si vedrebbe.
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    loadCoastlines();
}

void GreylineMap::loadCoastlines()
{
    QFile file(QString::fromLatin1(kCoastlineResource));
    if (!file.open(QIODevice::ReadOnly))
        return;

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint32 lineCount = 0;
    stream >> lineCount;
    m_coastlines.reserve(static_cast<int>(lineCount));

    for (quint32 i = 0; i < lineCount && stream.status() == QDataStream::Ok; ++i) {
        quint32 pointCount = 0;
        stream >> pointCount;

        QVector<QPointF> line;
        line.reserve(static_cast<int>(pointCount));
        for (quint32 j = 0; j < pointCount; ++j) {
            qint16 lon = 0;
            qint16 lat = 0;
            stream >> lon >> lat;
            line.append(QPointF(lon / 100.0, lat / 100.0));
        }
        if (line.size() >= 2)
            m_coastlines.append(line);
    }
}

void GreylineMap::setEngine(core::Greyline *engine)
{
    if (m_engine == engine)
        return;
    if (m_engine)
        disconnect(m_engine, nullptr, this, nullptr);
    m_engine = engine;
    if (m_engine)
        connect(m_engine, &core::Greyline::changed, this, [this] { update(); });
    emit engineChanged();
    update();
}

void GreylineMap::setHome(const QPointF &home)
{
    if (m_home == home)
        return;
    m_home = home;
    emit pointsChanged();
    update();
}

void GreylineMap::setTarget(const QPointF &target)
{
    if (m_target == target)
        return;
    m_target = target;
    emit pointsChanged();
    update();
}

bool GreylineMap::hasTarget() const
{
    return isValid(m_target);
}

void GreylineMap::setLongPath(bool longPath)
{
    if (m_longPath == longPath)
        return;
    m_longPath = longPath;
    emit pointsChanged();
    update();
}

void GreylineMap::setBandOpacity(qreal opacity)
{
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (qFuzzyCompare(m_bandOpacity, opacity))
        return;
    m_bandOpacity = opacity;
    emit styleChanged();
    update();
}

void GreylineMap::setGraticule(bool on)
{
    if (m_graticule == on)
        return;
    m_graticule = on;
    emit styleChanged();
    m_basemap = QPixmap();
    update();
}

QPointF GreylineMap::geoAt(qreal x, qreal y) const
{
    if (width() <= 0 || height() <= 0)
        return QPointF(qQNaN(), qQNaN());
    return QPointF(x / width() * 360.0 - 180.0, 90.0 - y / height() * 180.0);
}

void GreylineMap::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        // La basemap si rifà solo qui: le coste non si muovono, e ridisegnarle
        // a ogni minuto insieme al terminatore sarebbe quattromila segmenti
        // per niente.
        m_basemap = QPixmap();
        update();
    }
}

void GreylineMap::rebuildBasemap()
{
    const QSize size(static_cast<int>(width()), static_cast<int>(height()));
    if (size.isEmpty())
        return;

    // Il fondo è il **giorno**, non la notte: le bande scuriscono, e su un
    // fondo già nero non avrebbero niente da scurire. Al primo tentativo la
    // mappa era quasi tutta uguale e il terminatore sembrava una riga senza
    // significato — che è esattamente il difetto che questo pannello esiste
    // per non avere.
    m_basemap = QPixmap(size);
    m_basemap.fill(QColor(26, 46, 68));

    QPainter painter(&m_basemap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_graticule) {
        painter.setPen(QPen(QColor(255, 255, 255, 26), 1));
        // Ogni trenta gradi: due ore di rotazione terrestre per meridiano, che
        // è il passo con cui si ragiona guardando una mappa di propagazione.
        for (int lon = -150; lon <= 150; lon += 30) {
            const qreal x = xFor(lon);
            painter.drawLine(QPointF(x, 0), QPointF(x, height()));
        }
        for (int lat = -60; lat <= 60; lat += 30) {
            const qreal y = yFor(lat);
            painter.drawLine(QPointF(0, y), QPointF(width(), y));
        }

        // L'equatore un po' più marcato: è il riferimento da cui si legge
        // tutto il resto.
        painter.setPen(QPen(QColor(255, 255, 255, 48), 1));
        painter.drawLine(QPointF(0, yFor(0)), QPointF(width(), yFor(0)));
    }

    // Le coste chiare: devono restare leggibili anche sotto quattro bande
    // sovrapposte, cioè con metà della luce tolta.
    painter.setPen(QPen(QColor(150, 200, 236, 235), 1.0));
    for (const QVector<QPointF> &line : std::as_const(m_coastlines)) {
        QPainterPath path;
        path.moveTo(xFor(line.first().x()), yFor(line.first().y()));
        for (int i = 1; i < line.size(); ++i)
            path.lineTo(xFor(line.at(i).x()), yFor(line.at(i).y()));
        painter.drawPath(path);
    }
}

void GreylineMap::paintBand(QPainter *painter, const QVariantList &points,
                            qreal alpha) const
{
    if (points.size() < 3 || !m_engine)
        return;

    // Il disco attorno al punto antisolare, in equirettangolare, scavalca
    // l'antimeridiano: quando due punti consecutivi distano più di mezzo giro
    // in longitudine il poligono è uscito da un bordo e rientra dall'altro, e
    // va chiuso passando per il polo più vicino. Senza, resta una banda
    // orizzontale che attraversa tutta la mappa — e sembra un dato.
    const qreal poleLat = m_engine->antisolar().y() >= 0 ? 90.0 : -90.0;

    QPainterPath path;
    qreal previousLon = 0.0;
    bool started = false;

    for (const QVariant &value : points) {
        const QPointF geo = value.toPointF();
        const qreal lon = geo.x();
        const qreal lat = geo.y();

        if (started && std::abs(lon - previousLon) > 180.0) {
            const qreal exitLon = lon > previousLon ? 180.0 : -180.0;
            const qreal entryLon = -exitLon;
            path.lineTo(xFor(exitLon), yFor(lat));
            path.lineTo(xFor(exitLon), yFor(poleLat));
            path.lineTo(xFor(entryLon), yFor(poleLat));
            path.lineTo(xFor(entryLon), yFor(lat));
        }

        if (!started) {
            path.moveTo(xFor(lon), yFor(lat));
            started = true;
        } else {
            path.lineTo(xFor(lon), yFor(lat));
        }
        previousLon = lon;
    }
    path.closeSubpath();

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, static_cast<int>(alpha * 255)));
    painter->drawPath(path);
}

void GreylineMap::paint(QPainter *painter)
{
    if (width() <= 0 || height() <= 0)
        return;

    if (m_basemap.isNull() || m_basemap.size() != QSize(static_cast<int>(width()),
                                                        static_cast<int>(height()))) {
        rebuildBasemap();
    }
    painter->drawPixmap(0, 0, m_basemap);

    if (!m_engine)
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    // Dal disco più esterno al più interno: le trasparenze si sommano e danno
    // la sfumatura senza bisogno di gradienti.
    paintBand(painter, m_engine->civilBand(), m_bandOpacity);
    paintBand(painter, m_engine->nauticalBand(), m_bandOpacity);
    paintBand(painter, m_engine->astroBand(), m_bandOpacity);
    paintBand(painter, m_engine->nightBand(), m_bandOpacity);

    // Il terminatore vero, la riga in cui il Sole tocca l'orizzonte. Sottile e
    // ambra: è la linea che si sta cercando.
    {
        const QVariantList points = m_engine->terminator();
        painter->setPen(QPen(QColor(255, 190, 70, 200), 1.4));
        painter->setBrush(Qt::NoBrush);
        qreal previousLon = 0.0;
        bool started = false;
        QPainterPath path;
        for (const QVariant &value : points) {
            const QPointF geo = value.toPointF();
            if (started && std::abs(geo.x() - previousLon) > 180.0)
                started = false;
            if (!started) {
                path.moveTo(xFor(geo.x()), yFor(geo.y()));
                started = true;
            } else {
                path.lineTo(xFor(geo.x()), yFor(geo.y()));
            }
            previousLon = geo.x();
        }
        painter->drawPath(path);
    }

    // Il punto subsolare: dove il Sole sta allo zenit adesso.
    const QPointF sun = m_engine->subsolar();
    painter->setPen(QPen(QColor(120, 80, 10), 1));
    painter->setBrush(QColor(255, 214, 92));
    painter->drawEllipse(QPointF(xFor(sun.x()), yFor(sun.y())), 5.0, 5.0);

    // ── La rotta ─────────────────────────────────────────────────────────
    if (isValid(m_home) && isValid(m_target)) {
        QVariantList segments;
        if (m_longPath) {
            // La via lunga non è una rotta a sé: è l'altro arco dello stesso
            // cerchio massimo. Si disegna come i due tratti che dal QTH e dal
            // corrispondente vanno al punto opposto al corrispondente.
            const QPointF anti(std::fmod(m_target.x() + 360.0, 360.0) - 180.0,
                               -m_target.y());
            segments = m_engine->greatCircle(m_home.y(), m_home.x(),
                                             anti.y(), anti.x(), 90);
            const QVariantList second = m_engine->greatCircle(anti.y(), anti.x(),
                                                              m_target.y(), m_target.x(),
                                                              90);
            segments.append(second);
        } else {
            segments = m_engine->greatCircle(m_home.y(), m_home.x(),
                                             m_target.y(), m_target.x(), 90);
        }

        painter->setBrush(Qt::NoBrush);
        for (const QVariant &value : std::as_const(segments)) {
            const QVariantList part = value.toList();
            if (part.size() < 2)
                continue;
            QPainterPath path;
            const QPointF first = part.first().toPointF();
            path.moveTo(xFor(first.x()), yFor(first.y()));
            for (int i = 1; i < part.size(); ++i) {
                const QPointF p = part.at(i).toPointF();
                path.lineTo(xFor(p.x()), yFor(p.y()));
            }
            painter->setPen(QPen(QColor(90, 220, 200, 210), 1.6));
            painter->drawPath(path);
        }
    }

    // ── Le stazioni ──────────────────────────────────────────────────────
    //
    // Il colore dice la condizione di illuminazione, che è l'informazione
    // operativa: ambra vuol dire che quel punto è in linea grigia adesso.
    const auto markerColour = [this](const QPointF &p) {
        switch (m_engine->condition(p.y(), p.x())) {
        case 1:  return QColor(255, 184, 51);
        case 2:  return QColor(140, 128, 192);
        default: return QColor(90, 174, 255);
        }
    };

    if (isValid(m_target)) {
        const QPointF p(xFor(m_target.x()), yFor(m_target.y()));
        painter->setPen(QPen(QColor(10, 20, 30), 1));
        painter->setBrush(markerColour(m_target));
        painter->drawEllipse(p, 4.5, 4.5);
    }

    if (isValid(m_home)) {
        const QPointF p(xFor(m_home.x()), yFor(m_home.y()));
        painter->setPen(QPen(markerColour(m_home), 1.6));
        painter->setBrush(Qt::NoBrush);
        // La propria stazione è un cerchio con la croce: si distingue dai
        // corrispondenti senza dover leggere una legenda.
        painter->drawEllipse(p, 6.0, 6.0);
        painter->drawLine(p + QPointF(-9, 0), p + QPointF(9, 0));
        painter->drawLine(p + QPointF(0, -9), p + QPointF(0, 9));
    }
}

} // namespace dsdr::app
