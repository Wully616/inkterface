#ifndef FRAME_TRANSPORT_HPP
#define FRAME_TRANSPORT_HPP

#include <functional>

#include <QByteArray>
#include <QQueue>

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

#endif // FRAME_TRANSPORT_HPP
