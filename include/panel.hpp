#ifndef PANEL_HPP
#define PANEL_HPP

#include <QLowEnergyController>
#include <QObject>
#include <QTimer>

#include "frame-transport.hpp"
#include "panel-finder.hpp"
#include "panel-state.hpp"

class Dashboard;

static constexpr int EINK_SEND_INTERVAL_MS = 30000;
// PanelState samples telemetry every 2 seconds; send once per sample on the LCD.
static constexpr int LCD5B_SEND_INTERVAL_MS = 2000;

class Panel : public QObject
{
    Q_OBJECT

  public:
    explicit Panel(QObject *parent = nullptr);
    ~Panel()
    {
        if (m_controller) {
            m_controller->disconnectFromDevice();
        }
    }

    bool isConnected() const
    {
        auto state = m_controller ? m_controller->state() : QLowEnergyController::UnconnectedState;
        return state == QLowEnergyController::ConnectingState ||
               state == QLowEnergyController::ConnectedState ||
               state == QLowEnergyController::DiscoveringState ||
               state == QLowEnergyController::DiscoveredState;
    }

  public slots:
    void stop();

  private slots:
    void connCheck();
    void sendState();

    void clearConnection();
    void onControllerStateChanged(QLowEnergyController::ControllerState state);
    void onControllerServicesDiscovered();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onServiceError(QLowEnergyService::ServiceError error);
    void onServiceCharacteristicWritten(const QLowEnergyCharacteristic &characteristic,
                                        const QByteArray &value);

  private:
    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService *m_service = nullptr;
    QBluetoothDeviceInfo m_device;

    PanelFinder *m_finder = nullptr;
    PanelState *m_state = nullptr;
    Dashboard *m_dashboard = nullptr;
    BleFrameTransport m_frameTransport;

    QTimer *m_connTimer = nullptr;
    QTimer *m_sendTimer = nullptr;
    int m_sendInterval = EINK_SEND_INTERVAL_MS;
    bool m_connecting = false;
    bool m_frameTransportSupported = false;
    bool m_stopping = false;
    std::chrono::time_point<std::chrono::steady_clock> m_lastComms;

    void writeLine(const QUuid &uuid, const QString &value);
    void writeKeyVal(const uint8_t &index, const QString &key, const QString &value);
    void writePoints(const uint8_t &index, const PanelField *field);
    void flushDisplay();

    PanelFrame m_lastSentFrame;
    bool m_hasSentFrame = false;
    int m_lastLcdBacklightOnSent = -1;

    void sendLcdBacklightState();

};
#endif /* PANEL_HPP */
