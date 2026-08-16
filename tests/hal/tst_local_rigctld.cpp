// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/audiorig/LocalRigctldDriver.h"

#include <QtTest>

using namespace dsdr::hal::audiorig;

class LocalRigctldTest final : public QObject
{
    Q_OBJECT

private slots:
    void defaultsComeFromTheSelectedHamlibModel();
    void missingListingKeepsSafeGenericSerialDefaults();
    void hardwareHandshakeLeavesControlLinesToHamlib();
    void bareUnixSerialPortGetsDevicePrefix();
};

void LocalRigctldTest::defaultsComeFromTheSelectedHamlibModel()
{
    const QString listing = QStringLiteral(
        "serial_speed: \"Serial port baud rate\"\n"
        "\tDefault: 0, Value: 4800\n"
        "\tRange: 300..115200, step 1.0\n"
        "data_bits: \"Serial port data bits\"\n"
        "\tDefault: 8, Value: 8\n"
        "stop_bits: \"Serial port stop bits\"\n"
        "\tDefault: 1, Value: 2\n"
        "serial_parity: \"Serial port parity\"\n"
        "\tDefault: None, Value: None\n"
        "serial_handshake: \"Serial port handshake\"\n"
        "\tDefault: None, Value: Hardware\n");

    const QVariantMap defaults = LocalRigctldDriver::serialDefaultsFromListing(listing);
    QCOMPARE(defaults.value(QStringLiteral("baud")).toInt(), 4800);
    QCOMPARE(defaults.value(QStringLiteral("dataBits")).toInt(), 8);
    QCOMPARE(defaults.value(QStringLiteral("parity")).toInt(), 0);
    QCOMPARE(defaults.value(QStringLiteral("stopBits")).toInt(), 2);
    QCOMPARE(defaults.value(QStringLiteral("flowControl")).toInt(), 1);
    QCOMPARE(defaults.value(QStringLiteral("dtr")).toBool(), false);
    QCOMPARE(defaults.value(QStringLiteral("rts")).toBool(), false);
}

void LocalRigctldTest::missingListingKeepsSafeGenericSerialDefaults()
{
    const QVariantMap defaults = LocalRigctldDriver::serialDefaultsFromListing({});
    QCOMPARE(defaults.value(QStringLiteral("baud")).toInt(), 9600);
    QCOMPARE(defaults.value(QStringLiteral("dataBits")).toInt(), 8);
    QCOMPARE(defaults.value(QStringLiteral("stopBits")).toInt(), 1);
    QCOMPARE(defaults.value(QStringLiteral("flowControl")).toInt(), 0);
    QCOMPARE(defaults.value(QStringLiteral("dtr")).toBool(), false);
    QCOMPARE(defaults.value(QStringLiteral("rts")).toBool(), false);
}

void LocalRigctldTest::hardwareHandshakeLeavesControlLinesToHamlib()
{
    QCOMPARE(LocalRigctldDriver::serialControlLineStateName(1, false),
             QStringLiteral("Unset"));
    QCOMPARE(LocalRigctldDriver::serialControlLineStateName(1, true),
             QStringLiteral("Unset"));
    QCOMPARE(LocalRigctldDriver::serialControlLineStateName(0, false),
             QStringLiteral("OFF"));
    QCOMPARE(LocalRigctldDriver::serialControlLineStateName(0, true),
             QStringLiteral("ON"));
}

void LocalRigctldTest::bareUnixSerialPortGetsDevicePrefix()
{
#ifdef Q_OS_WIN
    QCOMPARE(LocalRigctldDriver::serialDevicePathForRigctld(QStringLiteral("COM4")),
             QStringLiteral("COM4"));
#else
    QCOMPARE(LocalRigctldDriver::serialDevicePathForRigctld(
                 QStringLiteral("tty.usbserial-31230")),
             QStringLiteral("/dev/tty.usbserial-31230"));
    QCOMPARE(LocalRigctldDriver::serialDevicePathForRigctld(
                 QStringLiteral("/dev/cu.usbserial-31230")),
             QStringLiteral("/dev/cu.usbserial-31230"));
#endif
}

QTEST_GUILESS_MAIN(LocalRigctldTest)
#include "tst_local_rigctld.moc"
