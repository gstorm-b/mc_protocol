// serial_bridge.h -- the QSerialPort end of a virtual COM pair that hosts a mc::MockPlc: the PLC
// side of QDV-14. Bytes read from the port go into the mock; whatever the mock answers is written
// back to the port. Test infrastructure, not library code: it links mc::mock, which mc_device
// itself never does. The pair itself (two ports wired to each other, e.g. COM50 and COM51) is
// installed on the machine; the tests learn its names from MC_TEST_SERIAL_PAIR.
#pragma once

#include "mc/core/frame_config.h"
#include "mc/device/serial_transport.h"
#include "mc/mock/mock_plc.h"

#include <QObject>
#include <QSerialPort>
#include <QString>

#include <optional>

// "COM50,COM51" -> {device port, bridge port}; nullopt when the variable is unset or malformed.
struct SerialPairNames {
    QString devicePort; // the end McDevice opens
    QString bridgePort; // the end SerialBridge opens
};
std::optional<SerialPairNames> serialPairFromEnvironment();

class SerialBridge : public QObject {
    Q_OBJECT
  public:
    // The mock answers `frame`; the port is opened by open() with `line` (portName is the bridge
    // end of the pair).
    SerialBridge(const mc::FrameConfig& frame, const mc::SerialSettings& line,
                 QObject* parent = nullptr);
    ~SerialBridge() override;

    // Opens the port ReadWrite; false with errorString() set when it cannot be opened.
    bool open();
    QString errorString() const;

    // The PLC behind the port, for seeding memory and for checking what a write changed.
    mc::MockPlc& plc();

  private:
    void onReadyRead();

    mc::SerialSettings m_line;
    QSerialPort m_port;
    mc::MockPlc m_plc;
    QString m_error;
};
