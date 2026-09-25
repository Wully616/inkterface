#ifndef PANEL_HPP
#define PANEL_HPP

#include <QLowEnergyController>
#include <QObject>
#include <QTimer>

#include "panel-finder.hpp"
#include "panel-state.hpp"

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
    void onArtworkFrame(QByteArray monoBits, quint16 monoWidth, quint16 monoHeight,
                        QByteArray colorJpeg, quint16 colorWidth, quint16 colorHeight);
    void onArtworkClear();

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

    QTimer *m_connTimer = nullptr;
    QTimer *m_sendTimer = nullptr;
    int m_sendInterval = EINK_SEND_INTERVAL_MS;
    bool m_connecting = false;
    bool m_stopping = false;
    std::chrono::time_point<std::chrono::steady_clock> m_lastComms;

    void writeLine(const QUuid &uuid, const QString &value);
    void writeKeyVal(const uint8_t &index, const QString &key, const QString &value);
    void writePoints(const uint8_t &index, const PanelField *field);
    void flushDisplay();

    // artwork frames are chunked into a queue of messages that are written
    // one at a time, each on completion of the previous write
    QList<QByteArray> m_artQueue;
    bool m_artSending = false;
    // most recent frame, kept so a (re)connected panel gets the artwork too
    QByteArray m_pendingArtBits;
    quint16 m_pendingArtWidth = 0;
    quint16 m_pendingArtHeight = 0;
    QByteArray m_pendingColorArtJpeg;
    quint16 m_pendingColorArtWidth = 0;
    quint16 m_pendingColorArtHeight = 0;
    // desired display state, so a (re)connected panel is always reconciled:
    // when a game is running we re-send its frame, otherwise we send a clear
    // in case the panel is stuck in artwork mode from a dropped clear
    bool m_artworkActive = false;

    void queueArtworkFrame();
    void sendArtworkClear();
    void reconcileArtwork();
    void sendArtwork();
};

#endif /* PANEL_HPP */
