// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include "core/AdsbMessage.h"
#include "core/AdsbCpr.h"
class QTimer;
namespace dsdr::core {
class AdsbReceiver;
// Lives on the UI thread; receiver must outlive this controller.
class AdsbController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastFrame READ lastFrame NOTIFY changed)
    Q_PROPERTY(QString icao READ icao NOTIFY changed)
    Q_PROPERTY(int typeCode READ typeCode NOTIFY changed)
    Q_PROPERTY(QString callsign READ callsign NOTIFY changed)
    Q_PROPERTY(bool hasBarometricAltitude READ hasBarometricAltitude NOTIFY changed)
    Q_PROPERTY(int barometricAltitudeFeet READ barometricAltitudeFeet NOTIFY changed)
    Q_PROPERTY(bool hasGroundSpeed READ hasGroundSpeed NOTIFY changed)
    Q_PROPERTY(double groundSpeedKnots READ groundSpeedKnots NOTIFY changed)
    Q_PROPERTY(bool hasGroundTrack READ hasGroundTrack NOTIFY changed)
    Q_PROPERTY(double groundTrackDegrees READ groundTrackDegrees NOTIFY changed)
    Q_PROPERTY(bool hasAirspeed READ hasAirspeed NOTIFY changed)
    Q_PROPERTY(double airspeedKnots READ airspeedKnots NOTIFY changed)
    Q_PROPERTY(bool trueAirspeed READ trueAirspeed NOTIFY changed)
    Q_PROPERTY(bool hasMagneticHeading READ hasMagneticHeading NOTIFY changed)
    Q_PROPERTY(double magneticHeadingDegrees READ magneticHeadingDegrees NOTIFY changed)
    Q_PROPERTY(bool hasVerticalRate READ hasVerticalRate NOTIFY changed)
    Q_PROPERTY(int verticalRateFeetPerMinute READ verticalRateFeetPerMinute NOTIFY changed)
    Q_PROPERTY(bool hasPosition READ hasPosition NOTIFY changed)
    Q_PROPERTY(double latitude READ latitude NOTIFY changed)
    Q_PROPERTY(double longitude READ longitude NOTIFY changed)
    Q_PROPERTY(qulonglong frameCount READ frameCount NOTIFY changed)
    Q_PROPERTY(qulonglong dropped READ dropped NOTIFY changed)
public:
    explicit AdsbController(AdsbReceiver *receiver, QObject *parent = nullptr);
    bool enabled() const;
    void setEnabled(bool enabled);
    void detach() noexcept;
    QString status() const;
    QString lastFrame() const { return m_lastFrame; }
    QString icao() const { return m_message.icao; }
    int typeCode() const { return m_message.typeCode; }
    QString callsign() const { return m_message.callsign; }
    bool hasBarometricAltitude() const { return m_message.barometricAltitudeFeet.has_value(); }
    int barometricAltitudeFeet() const { return m_message.barometricAltitudeFeet.value_or(0); }
    bool hasGroundSpeed() const { return m_message.groundSpeedKnots.has_value(); }
    double groundSpeedKnots() const { return m_message.groundSpeedKnots.value_or(0); }
    bool hasGroundTrack() const { return m_message.groundTrackDegrees.has_value(); }
    double groundTrackDegrees() const { return m_message.groundTrackDegrees.value_or(0); }
    bool hasAirspeed() const { return m_message.airspeedKnots.has_value(); }
    double airspeedKnots() const { return m_message.airspeedKnots.value_or(0); }
    bool trueAirspeed() const { return m_message.trueAirspeed.value_or(false); }
    bool hasMagneticHeading() const { return m_message.magneticHeadingDegrees.has_value(); }
    double magneticHeadingDegrees() const { return m_message.magneticHeadingDegrees.value_or(0); }
    bool hasVerticalRate() const { return m_message.verticalRateFeetPerMinute.has_value(); }
    int verticalRateFeetPerMinute() const { return m_message.verticalRateFeetPerMinute.value_or(0); }
    bool hasPosition() const { return m_position.has_value(); }
    double latitude() const { return m_position ? m_position->latitude : 0; }
    double longitude() const { return m_position ? m_position->longitude : 0; }
    qulonglong frameCount() const { return m_count; }
    qulonglong dropped() const;
public slots:
    void poll();
signals:
    void changed();
private:
    AdsbReceiver *m_receiver;
    AdsbMessage m_message;
    AdsbCpr m_cpr;
    std::optional<AdsbCpr::Position> m_position;
    std::uint64_t m_generation = 0;
    QString m_lastFrame, m_previousStatus;
    qulonglong m_count = 0, m_previousDropped = 0;
};
}
