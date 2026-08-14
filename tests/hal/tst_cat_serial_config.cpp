// SPDX-License-Identifier: GPL-3.0-or-later
// Una configurazione seriale sbagliata non dà un errore di protocollo: dà una
// radio apparentemente muta. Questa prova tiene legati i valori della UI a
// quelli che Qt applica veramente alla porta, senza aprirne una.
#include "hal/backends/audiorig/CatSerialPort.h"
#include "hal/backends/audiorig/CatController.h"

#include <QTest>

using namespace dsdr::hal::audiorig;

namespace {

class CapturingDriver final : public ICatDriver
{
public:
    QString driverId() const override { return QStringLiteral("capture"); }
    bool open(const QString &portName, const CatSerialConfig &serial) override
    {
        receivedPort = portName;
        receivedSerial = serial;
        return false;
    }
    void close() override {}
    bool isOpen() const override { return false; }
    QString radioModel() const override { return {}; }
    bool poll(CatState &) override { return false; }
    bool setFrequency(qint64) override { return false; }
    bool setMode(dsdr::DemodMode) override { return false; }
    bool setPtt(bool) override { return false; }
    QString errorString() const override { return QStringLiteral("expected test failure"); }
    QList<int> candidateBaudRates() const override { return {}; }
    int probe(const QString &) override { return -1; }

    QString receivedPort;
    CatSerialConfig receivedSerial;
};

} // namespace

class TestCatSerialConfig : public QObject
{
    Q_OBJECT

private slots:
    void defaultIsSafe8N1();
    void explicitSettingsReachQt();
    void invalidSettingsAreRefused();
    void controllerForwardsEverySerialSetting();
};

void TestCatSerialConfig::defaultIsSafe8N1()
{
    CatSerialConfig serial;
    QtSerialPortConfig applied;
    QString error;

    QVERIFY2(toQtSerialPortConfig(serial, &applied, &error), qPrintable(error));
    QCOMPARE(applied.dataBits, QSerialPort::Data8);
    QCOMPARE(applied.parity, QSerialPort::NoParity);
    QCOMPARE(applied.stopBits, QSerialPort::OneStop);
    QCOMPARE(applied.flowControl, QSerialPort::NoFlowControl);
    QVERIFY(!serial.dtr);
    QVERIFY(!serial.rts);
}

void TestCatSerialConfig::explicitSettingsReachQt()
{
    CatSerialConfig serial;
    serial.dataBits = 7;
    serial.parity = 1;
    serial.stopBits = 2;
    serial.flowControl = 1;
    serial.dtr = true;
    serial.rts = true;

    QtSerialPortConfig applied;
    QString error;
    QVERIFY2(toQtSerialPortConfig(serial, &applied, &error), qPrintable(error));
    QCOMPARE(applied.dataBits, QSerialPort::Data7);
    QCOMPARE(applied.parity, QSerialPort::EvenParity);
    QCOMPARE(applied.stopBits, QSerialPort::TwoStop);
    QCOMPARE(applied.flowControl, QSerialPort::HardwareControl);
    QVERIFY(serial.dtr);
    QVERIFY(serial.rts);
}

void TestCatSerialConfig::invalidSettingsAreRefused()
{
    CatSerialConfig serial;
    serial.dataBits = 9;

    QtSerialPortConfig applied;
    QString error;
    QVERIFY(!toQtSerialPortConfig(serial, &applied, &error));
    QVERIFY(!error.isEmpty());

    serial.dataBits = 8;
    serial.stopBits = 3;
    error.clear();
    QVERIFY(!toQtSerialPortConfig(serial, &applied, &error));
    QVERIFY(!error.isEmpty());
}

void TestCatSerialConfig::controllerForwardsEverySerialSetting()
{
    auto driver = std::make_unique<CapturingDriver>();
    CapturingDriver *captured = driver.get();
    CatController controller(std::move(driver));

    controller.open(QStringLiteral("cu.usbserial-test"), 38400, 7, 2, 15, 2,
                    true, true);

    QCOMPARE(captured->receivedPort, QStringLiteral("cu.usbserial-test"));
    QCOMPARE(captured->receivedSerial.baudRate, 38400);
    QCOMPARE(captured->receivedSerial.dataBits, 7);
    QCOMPARE(captured->receivedSerial.parity, 2);
    QCOMPARE(captured->receivedSerial.stopBits, 15);
    QCOMPARE(captured->receivedSerial.flowControl, 2);
    QVERIFY(captured->receivedSerial.dtr);
    QVERIFY(captured->receivedSerial.rts);
}

QTEST_MAIN(TestCatSerialConfig)
#include "tst_cat_serial_config.moc"
