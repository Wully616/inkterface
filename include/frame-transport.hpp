#ifndef FRAME_TRANSPORT_HPP
#define FRAME_TRANSPORT_HPP

#include <functional>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QString>
#include <QTimer>

struct PanelFrame {
    enum class Encoding { Monochrome1bpp, Jpeg };

    Encoding encoding = Encoding::Monochrome1bpp;
    quint16 width = 0;
    quint16 height = 0;
    QByteArray bytes;
    // Lossless host-rendered RGB565 pixels used to create small LCD tile updates.
    // The full-frame JPEG remains the keyframe and fallback representation.
    QByteArray rgb565;
};

class FrameTransport
{
  public:
    virtual ~FrameTransport() = default;
    virtual void setReady(bool ready) = 0;
    virtual void sendFrame(const PanelFrame &frame) = 0;
    virtual void writeCompleted() = 0;
};

// Adapts host-rendered frames to the BLE artwork protocol. LCD keyframes use
// JPEG; subsequent frames can be sent as changed RGB565 tiles.
class BleFrameTransport final : public FrameTransport
{
  public:
    using PacketWriter = std::function<void(const QByteArray &)>;

    explicit BleFrameTransport(PacketWriter writer);

    void setReady(bool ready) override;
    void setTileUpdatesSupported(bool supported) { m_tileUpdatesSupported = supported; }
    void sendFrame(const PanelFrame &frame) override;
    void writeCompleted() override;

  private:
    void enqueueFrame(const PanelFrame &frame);
    void enqueueFullFrame(const PanelFrame &frame);
    bool enqueueTileUpdate(const PanelFrame &frame);
    void pump();

    PacketWriter m_writer;
    QQueue<QByteArray> m_packets;
    PanelFrame m_pendingFrame;
    PanelFrame m_displayedFrame;
    PanelFrame m_inFlightFrame;
    quint16 m_nextUpdateId = 1;
    bool m_ready = false;
    bool m_inFlight = false;
    bool m_hasPendingFrame = false;
    bool m_hasDisplayedFrame = false;
    bool m_hasInFlightFrame = false;
    bool m_tileUpdatesSupported = false;
};

// Sends complete LCD-5B JPEG frames over the board's USB CDC serial port.
// BLE remains responsible for panel discovery and control; this transport
// identifies the selected panel through a serial handshake before sending.
class UsbFrameTransport final : public QObject, public FrameTransport
{
    Q_OBJECT

  public:
    explicit UsbFrameTransport(QObject *parent = nullptr);

    void probePanel(const QString &panelName);
    void stop();
    bool isConnected() const { return m_connected; }

    void setReady(bool ready) override;
    void sendFrame(const PanelFrame &frame) override;
    void writeCompleted() override {}

  signals:
    void panelConnectionChanged(bool connected);
    void frameFinished(bool success);
    void linkActivity();

  private slots:
    void handleReadyRead();
    void handlePortError(QSerialPort::SerialPortError error);
    void handleProbeTimeout();
    void handleHelloTimer();
    void handlePingTimer();
    void handlePingResponseTimeout();
    void handleRetryTimer();
    void handleFrameTimeout();
    void handleBytesWritten(qint64 bytes);

  private:
    void beginProbeCycle();
    void tryNextPort();
    void closePort();
    void sendHello();
    void processLine(const QByteArray &line);
    void sendFrameNow(const PanelFrame &frame);
    void pumpWriteBuffer();
    void finishFrame(bool success);

    QSerialPort *m_port = nullptr;
    QTimer m_probeTimer;
    QTimer m_helloTimer;
    QTimer m_pingTimer;
    QTimer m_pingResponseTimer;
    QTimer m_retryTimer;
    QTimer m_frameTimer;
    QList<QSerialPortInfo> m_candidates;
    QByteArray m_readBuffer;
    QByteArray m_writeBuffer;
    QString m_expectedPanelName;
    PanelFrame m_inFlightFrame;
    PanelFrame m_pendingFrame;
    qsizetype m_writeOffset = 0;
    int m_candidateIndex = 0;
    quint32 m_nextSequence = 1;
    quint32 m_inFlightSequence = 0;
    int m_retryCount = 0;
    bool m_connected = false;
    bool m_ready = false;
    bool m_inFlight = false;
    bool m_hasPendingFrame = false;
    bool m_retryPending = false;
};

#endif // FRAME_TRANSPORT_HPP
