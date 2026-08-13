// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Greyline.h"

#include <QTimer>
#include <QtMath>

#include <cmath>

namespace dsdr::core {

namespace {

constexpr double kDeg = M_PI / 180.0;
constexpr double kRad = 180.0 / M_PI;

/// Raggio terrestre medio. Non è il raggio equatoriale né quello polare: è
/// quello che rende minima l'approssimazione sferica su percorsi lunghi, ed è
/// il valore che usa tutto il mondo radioamatoriale per le distanze.
constexpr double kEarthRadiusKm = 6371.0;

/// I raggi dei quattro dischi attorno al punto antisolare.
///
///   90°  il Sole è all'orizzonte   → confine del giorno
///   84°  crepuscolo civile         → si legge ancora fuori
///   78°  crepuscolo nautico        → si vede l'orizzonte del mare
///   72°  crepuscolo astronomico    → oltre, è notte piena
constexpr double kCivilRadius = 90.0;
constexpr double kNauticalRadius = 84.0;
constexpr double kAstroRadius = 78.0;
constexpr double kNightRadius = 72.0;

double normalizeLon(double lon)
{
    lon = std::fmod(lon + 180.0, 360.0);
    if (lon < 0.0)
        lon += 360.0;
    return lon - 180.0;
}

double normalizeDeg(double deg)
{
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0)
        deg += 360.0;
    return deg;
}

/// Distanza angolare fra due punti, in gradi.
///
/// Con la formula dell'aversino e non con il coseno della distanza: la seconda
/// è più corta da scrivere e perde precisione proprio sui percorsi brevi, dove
/// il coseno di un angolo piccolo è quasi uno e i decimali che contano
/// spariscono nell'arrotondamento.
double angularDistance(double lat1, double lon1, double lat2, double lon2)
{
    const double dLat = (lat2 - lat1) * kDeg;
    const double dLon = (lon2 - lon1) * kDeg;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
        + std::cos(lat1 * kDeg) * std::cos(lat2 * kDeg)
            * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a)) * kRad;
}

} // namespace

Greyline::Greyline(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Greyline::recompute);
    m_timer->setInterval(m_refreshSeconds * 1000);
    m_timer->start();
    recompute();
}

Greyline::~Greyline() = default;

void Greyline::setUtc(const QDateTime &t)
{
    if (m_utc == t)
        return;
    m_utc = t;
    recompute();
}

void Greyline::resetUtc()
{
    if (!m_utc.isValid())
        return;
    m_utc = QDateTime();
    recompute();
}

void Greyline::setHourOffset(double hours)
{
    // Ventiquattro ore avanti e indietro: oltre, la mappa torna quasi identica
    // — il Sole gira — e quello che cambia davvero è la declinazione, che si
    // muove di un grado in tre giorni. Chi vuole marzo mette la data.
    hours = std::clamp(hours, -24.0, 24.0);
    if (qFuzzyCompare(m_hourOffset + 1.0, hours + 1.0))
        return;
    m_hourOffset = hours;
    recompute();
}

void Greyline::setResolution(int points)
{
    points = std::clamp(points, 24, 1440);
    if (points == m_resolution)
        return;
    m_resolution = points;
    recompute();
}

void Greyline::setRefreshSeconds(int seconds)
{
    seconds = std::max(0, seconds);
    if (seconds == m_refreshSeconds)
        return;
    m_refreshSeconds = seconds;
    if (seconds == 0) {
        m_timer->stop();
    } else {
        m_timer->setInterval(seconds * 1000);
        m_timer->start();
    }
    emit refreshSecondsChanged();
}

void Greyline::setGreylineHalfWidth(double degrees)
{
    degrees = std::clamp(degrees, 1.0, 20.0);
    if (qFuzzyCompare(m_halfWidth, degrees))
        return;
    m_halfWidth = degrees;
    emit changed();
}

QDateTime Greyline::referenceTime() const
{
    const QDateTime base = m_utc.isValid() ? m_utc : QDateTime::currentDateTimeUtc();
    return base.addMSecs(static_cast<qint64>(m_hourOffset * 3600000.0)).toUTC();
}

void Greyline::solarPosition(const QDateTime &utc, double &declination,
                             double &subsolarLon)
{
    const QDateTime t = utc.toUTC();

    // Giorno giuliano e secoli da J2000.0.
    const double jd = static_cast<double>(t.date().toJulianDay())
        + (t.time().msecsSinceStartOfDay() / 86'400'000.0) - 0.5;
    const double centuries = (jd - 2451545.0) / 36525.0;

    // Longitudine media e anomalia media del Sole. I termini quadratici ci
    // sono: valgono qualche millesimo di grado, e non costano niente.
    const double meanLon = normalizeDeg(280.46646 + 36000.76983 * centuries
                                        + 0.0003032 * centuries * centuries);
    const double meanAnomaly = (357.52911 + 35999.05029 * centuries
                                - 0.0001537 * centuries * centuries) * kDeg;

    // Equazione del centro: la correzione che porta dall'orbita circolare
    // ideale a quella vera, che è un'ellisse.
    const double centre = (1.914602 - 0.004817 * centuries
                           - 0.000014 * centuries * centuries) * std::sin(meanAnomaly)
        + (0.019993 - 0.000101 * centuries) * std::sin(2 * meanAnomaly)
        + 0.000289 * std::sin(3 * meanAnomaly);

    const double trueLon = (meanLon + centre) * kDeg;
    const double obliquity = (23.439291 - 0.0130042 * centuries) * kDeg;

    declination = std::asin(std::sin(obliquity) * std::sin(trueLon)) * kRad;

    // L'equazione del tempo, in minuti: la differenza fra il mezzogiorno
    // dell'orologio e quello del Sole. Va da −14 minuti a febbraio a +16 a
    // novembre, e ignorarla sposterebbe il terminatore di quattro gradi —
    // quattrocento chilometri alle nostre latitudini.
    const double y = std::tan(obliquity / 2.0) * std::tan(obliquity / 2.0);
    const double meanLonRad = meanLon * kDeg;
    const double eccentricity = 0.016708634 - 0.000042037 * centuries;
    const double equationOfTime = 4.0 * kRad
        * (y * std::sin(2 * meanLonRad)
           - 2.0 * eccentricity * std::sin(meanAnomaly)
           + 4.0 * eccentricity * y * std::sin(meanAnomaly) * std::cos(2 * meanLonRad)
           - 0.5 * y * y * std::sin(4 * meanLonRad)
           - 1.25 * eccentricity * eccentricity * std::sin(2 * meanAnomaly));

    // Il punto subsolare sta dove è mezzogiorno solare vero.
    const double utcHours = t.time().msecsSinceStartOfDay() / 3'600'000.0;
    subsolarLon = normalizeLon(-15.0 * (utcHours + equationOfTime / 60.0 - 12.0));
}

QVariantList Greyline::circleAround(const QPointF &center, double radiusDeg) const
{
    QVariantList points;
    points.reserve(m_resolution + 1);

    const double lat0 = center.y() * kDeg;
    const double lon0 = center.x() * kDeg;
    const double radius = radiusDeg * kDeg;
    const double sinLat0 = std::sin(lat0);
    const double cosLat0 = std::cos(lat0);
    const double sinR = std::sin(radius);
    const double cosR = std::cos(radius);

    for (int i = 0; i <= m_resolution; ++i) {
        const double bearing = (2.0 * M_PI * i) / m_resolution;
        const double lat = std::asin(sinLat0 * cosR + cosLat0 * sinR * std::cos(bearing));
        const double lon = lon0
            + std::atan2(sinR * std::sin(bearing) * cosLat0,
                         cosR - sinLat0 * std::sin(lat));
        points.append(QPointF(normalizeLon(lon * kRad), lat * kRad));
    }
    return points;
}

void Greyline::recompute()
{
    solarPosition(referenceTime(), m_declination, m_subsolarLon);

    m_subsolar = QPointF(m_subsolarLon, m_declination);
    m_antisolar = QPointF(normalizeLon(m_subsolarLon + 180.0), -m_declination);

    m_civil = circleAround(m_antisolar, kCivilRadius);
    m_nautical = circleAround(m_antisolar, kNauticalRadius);
    m_astro = circleAround(m_antisolar, kAstroRadius);
    m_night = circleAround(m_antisolar, kNightRadius);

    emit changed();
}

double Greyline::sunAltitude(double lat, double lon) const
{
    // L'altitudine è novanta gradi meno la distanza dal punto subsolare. È
    // esatto, non un'approssimazione: la definizione di punto subsolare è
    // proprio quella del punto in cui il Sole sta allo zenit.
    return 90.0 - angularDistance(lat, lon, m_declination, m_subsolarLon);
}

bool Greyline::inGreyline(double lat, double lon) const
{
    return std::abs(sunAltitude(lat, lon)) <= m_halfWidth;
}

int Greyline::condition(double lat, double lon) const
{
    const double altitude = sunAltitude(lat, lon);
    if (std::abs(altitude) <= m_halfWidth)
        return 1;
    return altitude > 0.0 ? 0 : 2;
}

double Greyline::bearing(double fromLat, double fromLon,
                         double toLat, double toLon) const
{
    const double phi1 = fromLat * kDeg;
    const double phi2 = toLat * kDeg;
    const double dLon = (toLon - fromLon) * kDeg;

    const double y = std::sin(dLon) * std::cos(phi2);
    const double x = std::cos(phi1) * std::sin(phi2)
        - std::sin(phi1) * std::cos(phi2) * std::cos(dLon);
    return normalizeDeg(std::atan2(y, x) * kRad);
}

double Greyline::longPathBearing(double fromLat, double fromLon,
                                 double toLat, double toLon) const
{
    return normalizeDeg(bearing(fromLat, fromLon, toLat, toLon) + 180.0);
}

double Greyline::distanceKm(double fromLat, double fromLon,
                            double toLat, double toLon) const
{
    return angularDistance(fromLat, fromLon, toLat, toLon) * kDeg * kEarthRadiusKm;
}

QVariantList Greyline::greatCircle(double fromLat, double fromLon,
                                   double toLat, double toLon, int points) const
{
    QVariantList segments;
    points = std::clamp(points, 8, 720);

    const double phi1 = fromLat * kDeg;
    const double lambda1 = fromLon * kDeg;
    const double phi2 = toLat * kDeg;
    const double lambda2 = toLon * kDeg;

    const double delta = angularDistance(fromLat, fromLon, toLat, toLon) * kDeg;
    if (delta < 1e-9)
        return segments;

    const double sinDelta = std::sin(delta);

    QVariantList current;
    double previousLon = 0.0;
    bool started = false;

    for (int i = 0; i <= points; ++i) {
        const double f = static_cast<double>(i) / points;
        // Interpolazione sferica: i punti restano sulla sfera invece di
        // tagliarla. Interpolare lat e lon separatamente darebbe una curva
        // plausibile e sbagliata, che alle alte latitudini sbaglia di
        // centinaia di chilometri.
        const double a = std::sin((1 - f) * delta) / sinDelta;
        const double b = std::sin(f * delta) / sinDelta;

        const double x = a * std::cos(phi1) * std::cos(lambda1)
            + b * std::cos(phi2) * std::cos(lambda2);
        const double y = a * std::cos(phi1) * std::sin(lambda1)
            + b * std::cos(phi2) * std::sin(lambda2);
        const double z = a * std::sin(phi1) + b * std::sin(phi2);

        const double lat = std::atan2(z, std::sqrt(x * x + y * y)) * kRad;
        const double lon = std::atan2(y, x) * kRad;

        // Lo scavalcamento dell'antimeridiano spezza la rotta in due tratti.
        // Senza, in equirettangolare comparirebbe una riga che attraversa
        // tutta la mappa, e sembrerebbe un percorso.
        if (started && std::abs(lon - previousLon) > 180.0) {
            segments.append(QVariant::fromValue(current));
            current.clear();
        }

        current.append(QPointF(lon, lat));
        previousLon = lon;
        started = true;
    }

    if (!current.isEmpty())
        segments.append(QVariant::fromValue(current));
    return segments;
}

double Greyline::greylineOverlapKm(double fromLat, double fromLon,
                                   double toLat, double toLon) const
{
    // Si campiona il percorso e si contano i tratti che stanno nella fascia.
    // Duecento passi su un percorso mezzo mondo sono cento chilometri per
    // passo: più fine di così misurerebbe il rumore del campionamento.
    constexpr int kSteps = 200;

    const double total = distanceKm(fromLat, fromLon, toLat, toLon);
    if (total < 1.0)
        return 0.0;

    const double phi1 = fromLat * kDeg;
    const double lambda1 = fromLon * kDeg;
    const double phi2 = toLat * kDeg;
    const double lambda2 = toLon * kDeg;
    const double delta = total / kEarthRadiusKm;
    const double sinDelta = std::sin(delta);
    if (sinDelta < 1e-12)
        return 0.0;

    int inside = 0;
    for (int i = 0; i <= kSteps; ++i) {
        const double f = static_cast<double>(i) / kSteps;
        const double a = std::sin((1 - f) * delta) / sinDelta;
        const double b = std::sin(f * delta) / sinDelta;

        const double x = a * std::cos(phi1) * std::cos(lambda1)
            + b * std::cos(phi2) * std::cos(lambda2);
        const double y = a * std::cos(phi1) * std::sin(lambda1)
            + b * std::cos(phi2) * std::sin(lambda2);
        const double z = a * std::sin(phi1) + b * std::sin(phi2);

        const double lat = std::atan2(z, std::sqrt(x * x + y * y)) * kRad;
        const double lon = std::atan2(y, x) * kRad;
        if (inGreyline(lat, lon))
            ++inside;
    }

    return total * static_cast<double>(inside) / (kSteps + 1);
}

QPointF Greyline::fromLocator(const QString &locator) const
{
    const QString grid = locator.trimmed().toUpper();
    // Quattro o sei caratteri. Due soli sarebbero un campo da mille chilometri
    // di lato: non è una posizione, è un continente.
    if (grid.size() != 4 && grid.size() != 6)
        return QPointF(qQNaN(), qQNaN());

    const auto field = [](QChar c) { return c.unicode() - u'A'; };
    const auto square = [](QChar c) { return c.unicode() - u'0'; };

    if (grid[0] < u'A' || grid[0] > u'R' || grid[1] < u'A' || grid[1] > u'R')
        return QPointF(qQNaN(), qQNaN());
    if (!grid[2].isDigit() || !grid[3].isDigit())
        return QPointF(qQNaN(), qQNaN());

    double lon = field(grid[0]) * 20.0 + square(grid[2]) * 2.0 - 180.0;
    double lat = field(grid[1]) * 10.0 + square(grid[3]) * 1.0 - 90.0;

    if (grid.size() == 6) {
        if (grid[4] < u'A' || grid[4] > u'X' || grid[5] < u'A' || grid[5] > u'X')
            return QPointF(qQNaN(), qQNaN());
        lon += field(grid[4]) * (2.0 / 24.0);
        lat += field(grid[5]) * (1.0 / 24.0);
        // Il centro del sottoquadrato, non il suo angolo: un locatore indica
        // un'area, e il suo angolo sud-ovest è il punto più lontano dal
        // significato.
        lon += 1.0 / 24.0;
        lat += 0.5 / 24.0;
    } else {
        lon += 1.0;
        lat += 0.5;
    }

    return QPointF(lon, lat);
}

QString Greyline::toLocator(double lat, double lon) const
{
    lat = std::clamp(lat, -90.0, 90.0);
    lon = normalizeLon(lon);

    double adjustedLon = lon + 180.0;
    double adjustedLat = lat + 90.0;

    QString grid;
    grid += QChar(u'A' + static_cast<int>(adjustedLon / 20.0));
    grid += QChar(u'A' + static_cast<int>(adjustedLat / 10.0));

    adjustedLon = std::fmod(adjustedLon, 20.0);
    adjustedLat = std::fmod(adjustedLat, 10.0);

    grid += QChar(u'0' + static_cast<int>(adjustedLon / 2.0));
    grid += QChar(u'0' + static_cast<int>(adjustedLat / 1.0));

    adjustedLon = std::fmod(adjustedLon, 2.0);
    adjustedLat = std::fmod(adjustedLat, 1.0);

    grid += QChar(u'A' + static_cast<int>(adjustedLon * 12.0));
    grid += QChar(u'A' + static_cast<int>(adjustedLat * 24.0));

    return grid;
}

} // namespace dsdr::core
