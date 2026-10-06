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
};

class FrameTransport
{
  public:
    virtual ~FrameTransport() = default;
    virtual void setReady(bool ready) = 0;
    virtual void sendFrame(const PanelFrame &frame) = 0;
    virtual void writeCompleted() = 0;
};

// Adapts complete host-rendered frames to the existing BLE artwork opcodes.
class BleFrameTransport final : public FrameTransport
{
  public:
    using PacketWriter = std::function<void(const QByteArray &)>;

    explicit BleFrameTransport(PacketWriter writer);

    void setReady(bool ready) override;
    void sendFrame(const PanelFrame &frame) override;
    void writeCompleted() override;

  private:
    void enqueueFrame(const PanelFrame &frame);
    void pump();

    PacketWriter m_writer;
    QQueue<QByteArray> m_packets;
    PanelFrame m_pendingFrame;
    bool m_ready = false;
    bool m_inFlight = false;
    bool m_hasPendingFrame = false;
};

#endif // FRAME_TRANSPORT_HPP
