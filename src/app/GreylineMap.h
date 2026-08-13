// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — la mappa della linea grigia, in proiezione equirettangolare.
//
// **Perché dipinta in C++ e non un Canvas QML.** Le coste sono quattromila e
// seicento punti, e un Canvas li farebbe attraversare come QVariant a ogni
// ridisegno. Qui la basemap si disegna una volta sola in una cache e si
// ricopia: quello che cambia a ogni minuto è il terminatore, che di punti ne
// ha centottanta.
//
// **Perché equirettangolare e non Mercatore.** Mercatore è la proiezione delle
// carte nautiche, e serve a una cosa sola: mantenere gli angoli, così una
// rotta costante è una retta. Qui non si naviga — si guarda dove passa il
// terminatore — e in cambio Mercatore gonfia le alte latitudini fino a
// rendere illeggibile proprio la calotta polare, che sulle bande alte è la
// zona che conta. In equirettangolare la longitudine è il tempo: mezzogiorno a
// destra, mezzanotte a sinistra, e il terminatore è una sinusoide che si legge
// come un orologio.
//
// **Niente tile, niente rete, niente OpenGL.** Un client SDR non deve
// telefonare a un server di mappe per dire dov'è il Sole, e su una stazione
// senza rete la mappa deve funzionare uguale.
#pragma once

#include "core/Greyline.h"

#include <QColor>
#include <QPixmap>
#include <QQuickPaintedItem>
#include <QVector>

namespace dsdr::app {

class GreylineMap : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GreylineMap)

    Q_PROPERTY(dsdr::core::Greyline *engine READ engine WRITE setEngine NOTIFY engineChanged)

    /// La stazione: da dove si trasmette. Fuori scala se non è impostata.
    Q_PROPERTY(QPointF home READ home WRITE setHome NOTIFY pointsChanged)

    /// Il corrispondente, se ce n'è uno scelto.
    Q_PROPERTY(QPointF target READ target WRITE setTarget NOTIFY pointsChanged)
    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY pointsChanged)

    /// Se disegnare la rotta lunga invece di quella breve.
    Q_PROPERTY(bool longPath READ longPath WRITE setLongPath NOTIFY pointsChanged)

    /// Quanto sono scure le bande. Si sommano fra loro: quattro dischi al 18
    /// per cento danno un centro al 55, che è notte senza essere nero.
    Q_PROPERTY(qreal bandOpacity READ bandOpacity WRITE setBandOpacity NOTIFY styleChanged)

    /// Il reticolo dei meridiani e paralleli, ogni trenta gradi.
    Q_PROPERTY(bool graticule READ graticule WRITE setGraticule NOTIFY styleChanged)

public:
    explicit GreylineMap(QQuickItem *parent = nullptr);

    core::Greyline *engine() const { return m_engine; }
    void setEngine(core::Greyline *engine);

    QPointF home() const { return m_home; }
    void setHome(const QPointF &home);

    QPointF target() const { return m_target; }
    void setTarget(const QPointF &target);
    bool hasTarget() const;

    bool longPath() const { return m_longPath; }
    void setLongPath(bool longPath);

    qreal bandOpacity() const { return m_bandOpacity; }
    void setBandOpacity(qreal opacity);

    bool graticule() const { return m_graticule; }
    void setGraticule(bool on);

    /// Il punto geografico sotto un punto dello schermo. Serve a scegliere un
    /// corrispondente con il dito invece che digitandone il locatore.
    Q_INVOKABLE QPointF geoAt(qreal x, qreal y) const;

    void paint(QPainter *painter) override;

signals:
    void engineChanged();
    void pointsChanged();
    void styleChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    /// Carica le coste dalla risorsa. Una volta sola, alla costruzione.
    void loadCoastlines();

    /// Ridisegna la basemap nella cache. Solo al cambio di misura: le coste
    /// non si muovono.
    void rebuildBasemap();

    qreal xFor(qreal lon) const { return (lon + 180.0) / 360.0 * width(); }
    qreal yFor(qreal lat) const { return (90.0 - lat) / 180.0 * height(); }

    void paintBand(QPainter *painter, const QVariantList &points, qreal alpha) const;

    core::Greyline *m_engine = nullptr;

    /// Le coste, come polilinee già spezzate all'antimeridiano.
    QVector<QVector<QPointF>> m_coastlines;
    QPixmap m_basemap;

    QPointF m_home{qQNaN(), qQNaN()};
    QPointF m_target{qQNaN(), qQNaN()};
    bool m_longPath = false;
    qreal m_bandOpacity = 0.20;
    bool m_graticule = true;
};

} // namespace dsdr::app
