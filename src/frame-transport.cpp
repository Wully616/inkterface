#include "frame-transport.hpp"

#include <limits>
#include <cstdint>
#include <utility>

#include <QDebug>

namespace
{
constexpr qsizetype BLE_FRAME_CHUNK_SIZE = 244;
}

BleFrameTransport::BleFrameTransport(PacketWriter writer)
    : m_writer(std::move(writer))
{
}

void BleFrameTransport::setReady(bool ready)
{
    if (m_ready == ready) {
        return;
    }
    m_ready = ready;
    if (!ready) {
        m_packets.clear();
        m_inFlight = false;
        m_hasPendingFrame = false;
        m_pendingFrame = {};
        return;
    }
    if (m_hasPendingFrame) {
        const PanelFrame pending = std::move(m_pendingFrame);
        m_hasPendingFrame = false;
        enqueueFrame(pending);
    }
    pump();
}

void BleFrameTransport::sendFrame(const PanelFrame &frame)
{
    if (frame.bytes.isEmpty() || frame.width == 0 || frame.height == 0 ||
        frame.bytes.size() > std::numeric_limits<uint32_t>::max()) {
        return;
    }
    if (!m_ready || m_inFlight) {
        m_pendingFrame = frame;
        m_hasPendingFrame = true;
        return;
    }
    m_packets.clear();
    enqueueFrame(frame);
    pump();
}

void BleFrameTransport::writeCompleted()
{
    if (!m_inFlight) {
        return;
    }
    m_inFlight = false;
    if (!m_packets.isEmpty()) {
        m_packets.dequeue();
    }
    if (m_packets.isEmpty() && m_hasPendingFrame) {
        const PanelFrame pending = std::move(m_pendingFrame);
        m_hasPendingFrame = false;
        enqueueFrame(pending);
    }
    pump();
}

void BleFrameTransport::enqueueFrame(const PanelFrame &frame)
{
    const bool jpeg = frame.encoding == PanelFrame::Encoding::Jpeg;
    QByteArray begin;
    begin.append(char(jpeg ? 0x04 : 0x00));
    begin.append(char(frame.width & 0xFF));
    begin.append(char((frame.width >> 8) & 0xFF));
    begin.append(char(frame.height & 0xFF));
    begin.append(char((frame.height >> 8) & 0xFF));
    if (jpeg) {
        const auto size = static_cast<uint32_t>(frame.bytes.size());
        begin.append(char(size & 0xFF));
        begin.append(char((size >> 8) & 0xFF));
        begin.append(char((size >> 16) & 0xFF));
        begin.append(char((size >> 24) & 0xFF));
    }
    m_packets.enqueue(std::move(begin));

    const char dataOpcode = char(jpeg ? 0x05 : 0x01);
    for (qsizetype offset = 0; offset < frame.bytes.size(); offset += BLE_FRAME_CHUNK_SIZE) {
        QByteArray packet;
        packet.reserve(5 + BLE_FRAME_CHUNK_SIZE);
        packet.append(dataOpcode);
        const auto byteOffset = static_cast<uint32_t>(offset);
        packet.append(char(byteOffset & 0xFF));
        packet.append(char((byteOffset >> 8) & 0xFF));
        packet.append(char((byteOffset >> 16) & 0xFF));
        packet.append(char((byteOffset >> 24) & 0xFF));
        packet.append(frame.bytes.constData() + offset,
                      qMin(BLE_FRAME_CHUNK_SIZE, frame.bytes.size() - offset));
        m_packets.enqueue(std::move(packet));
    }
    m_packets.enqueue(QByteArray(1, char(jpeg ? 0x06 : 0x02)));

    qDebug() << "queued" << (jpeg ? "color JPEG" : "monochrome") << "dashboard frame"
             << frame.width << "x" << frame.height << "(" << frame.bytes.size() << "bytes) in"
             << m_packets.size() - 2 << "chunks";
}

void BleFrameTransport::pump()
{
    if (!m_ready || m_inFlight || m_packets.isEmpty() || !m_writer) {
        return;
    }
    m_inFlight = true;
    m_writer(m_packets.head());
}
