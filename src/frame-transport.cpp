#include "frame-transport.hpp"

#include <limits>
#include <cstdint>
#include <cstring>
#include <utility>

#include <QDebug>
#include <QVector>

namespace
{
constexpr qsizetype BLE_FRAME_CHUNK_SIZE = 244;
constexpr int LCD_TILE_SIZE = 8;
constexpr qsizetype TILE_UPDATE_HEADER_SIZE = 10;
constexpr qsizetype TILE_UPDATE_COMMIT_SIZE = 3;
constexpr qsizetype TILE_PACKET_HEADER_SIZE = 9;
constexpr int TILE_UPDATE_FULL_FRAME_THRESHOLD_PERCENT = 80;

void appendUint16(QByteArray &bytes, quint16 value)
{
    bytes.append(char(value & 0xFF));
    bytes.append(char((value >> 8) & 0xFF));
}
}

BleFrameTransport::BleFrameTransport(PacketWriter writer)
    : m_writer(std::move(writer))
{
}

void BleFrameTransport::setReady(bool ready)
{
    if (m_ready == ready) {
        if (!ready) {
            m_packets.clear();
            m_inFlight = false;
            m_hasPendingFrame = false;
            m_pendingFrame = {};
            m_hasDisplayedFrame = false;
            m_displayedFrame = {};
            m_hasInFlightFrame = false;
            m_inFlightFrame = {};
            m_tileUpdatesSupported = false;
        }
        return;
    }
    m_ready = ready;
    if (!ready) {
        m_packets.clear();
        m_inFlight = false;
        m_hasPendingFrame = false;
        m_pendingFrame = {};
        m_hasDisplayedFrame = false;
        m_displayedFrame = {};
        m_hasInFlightFrame = false;
        m_inFlightFrame = {};
        m_tileUpdatesSupported = false;
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
    if (m_packets.isEmpty()) {
        if (m_hasInFlightFrame) {
            m_displayedFrame = std::move(m_inFlightFrame);
            m_hasDisplayedFrame = true;
            m_hasInFlightFrame = false;
        }
        if (m_hasPendingFrame) {
            const PanelFrame pending = std::move(m_pendingFrame);
            m_hasPendingFrame = false;
            enqueueFrame(pending);
        }
    }
    pump();
}

void BleFrameTransport::enqueueFrame(const PanelFrame &frame)
{
    m_packets.clear();
    m_inFlightFrame = frame;
    m_hasInFlightFrame = true;
    if (enqueueTileUpdate(frame)) {
        return;
    }
    enqueueFullFrame(frame);
}

bool BleFrameTransport::enqueueTileUpdate(const PanelFrame &frame)
{
    if (!m_tileUpdatesSupported || frame.encoding != PanelFrame::Encoding::Jpeg ||
        !m_hasDisplayedFrame ||
        m_displayedFrame.encoding != PanelFrame::Encoding::Jpeg ||
        frame.width != m_displayedFrame.width || frame.height != m_displayedFrame.height ||
        frame.rgb565.size() != static_cast<qsizetype>(frame.width) * frame.height * 2 ||
        m_displayedFrame.rgb565.size() != frame.rgb565.size()) {
        return false;
    }

    const auto *current = reinterpret_cast<const unsigned char *>(frame.rgb565.constData());
    const auto *previous =
        reinterpret_cast<const unsigned char *>(m_displayedFrame.rgb565.constData());
    const qsizetype rowBytes = static_cast<qsizetype>(frame.width) * 2;
    if (frame.width % LCD_TILE_SIZE != 0 || frame.height % LCD_TILE_SIZE != 0) {
        return false;
    }
    const int tileColumns = frame.width / LCD_TILE_SIZE;
    QVector<quint16> changedTiles;
    qsizetype tileUpdateBytes = TILE_UPDATE_HEADER_SIZE + TILE_UPDATE_COMMIT_SIZE;
    for (int y = 0; y < frame.height; y += LCD_TILE_SIZE) {
        for (int x = 0; x < frame.width; x += LCD_TILE_SIZE) {
            const qsizetype xByteOffset = static_cast<qsizetype>(x) * 2;
            bool changed = false;
            for (int row = 0; row < LCD_TILE_SIZE; ++row) {
                const qsizetype offset = static_cast<qsizetype>(y + row) * rowBytes + xByteOffset;
                if (std::memcmp(current + offset, previous + offset,
                                LCD_TILE_SIZE * 2) != 0) {
                    changed = true;
                    break;
                }
            }
            if (!changed) {
                continue;
            }
            const quint16 tileIndex = static_cast<quint16>(
                (y / LCD_TILE_SIZE) * tileColumns + x / LCD_TILE_SIZE);
            changedTiles.append(tileIndex);
            tileUpdateBytes += TILE_PACKET_HEADER_SIZE + LCD_TILE_SIZE * LCD_TILE_SIZE * 2;
            if (tileUpdateBytes * 100 >=
                static_cast<qsizetype>(frame.bytes.size()) * TILE_UPDATE_FULL_FRAME_THRESHOLD_PERCENT) {
                return false;
            }
        }
    }

    if (changedTiles.isEmpty()) {
        // The rendered pixels are unchanged even if their JPEG encoding differs.
        m_displayedFrame = frame;
        m_hasDisplayedFrame = true;
        m_hasInFlightFrame = false;
        return true;
    }

    QByteArray begin;
    begin.reserve(TILE_UPDATE_HEADER_SIZE);
    begin.append(char(0x07)); // TILE_BEGIN
    appendUint16(begin, frame.width);
    appendUint16(begin, frame.height);
    begin.append(char(LCD_TILE_SIZE));
    appendUint16(begin, static_cast<quint16>(changedTiles.size()));
    appendUint16(begin, m_nextUpdateId);
    m_packets.enqueue(std::move(begin));
    for (const quint16 tileIndex : changedTiles) {
        const int x = (tileIndex % tileColumns) * LCD_TILE_SIZE;
        const int y = (tileIndex / tileColumns) * LCD_TILE_SIZE;
        QByteArray packet;
        packet.reserve(TILE_PACKET_HEADER_SIZE + LCD_TILE_SIZE * LCD_TILE_SIZE * 2);
        packet.append(char(0x08)); // TILE_DATA
        appendUint16(packet, m_nextUpdateId);
        appendUint16(packet, static_cast<quint16>(x));
        appendUint16(packet, static_cast<quint16>(y));
        packet.append(char(LCD_TILE_SIZE));
        packet.append(char(LCD_TILE_SIZE));
        for (int row = 0; row < LCD_TILE_SIZE; ++row) {
            const qsizetype offset = static_cast<qsizetype>(y + row) * rowBytes +
                                     static_cast<qsizetype>(x) * 2;
            packet.append(frame.rgb565.constData() + offset, LCD_TILE_SIZE * 2);
        }
        m_packets.enqueue(std::move(packet));
    }
    QByteArray commit;
    commit.append(char(0x09)); // TILE_SHOW
    appendUint16(commit, m_nextUpdateId);
    m_packets.enqueue(std::move(commit));
    qDebug() << "queued LCD tile update" << frame.width << "x" << frame.height << "with"
             << changedTiles.size() << "tiles (" << tileUpdateBytes << "bytes)";
    ++m_nextUpdateId;
    return true;
}

void BleFrameTransport::enqueueFullFrame(const PanelFrame &frame)
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
