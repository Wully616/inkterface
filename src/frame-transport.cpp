#include "frame-transport.hpp"

#include <limits>
#include <cstdint>
#include <cstdio>
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

quint32 crc32(const QByteArray &bytes)
{
    quint32 crc = 0xFFFFFFFFU;
    for (const char byte : bytes) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

bool isEsp32SerialPort(const QSerialPortInfo &port)
{
    if (port.hasVendorIdentifier()) {
        const quint16 vendor = port.vendorIdentifier();
        if (vendor == 0x303A || vendor == 0x10C4 || vendor == 0x1A86) {
            return true;
        }
    }
    const QString description =
        port.description() + QStringLiteral(" ") + port.manufacturer();
    return description.contains(QStringLiteral("Espressif"), Qt::CaseInsensitive) ||
           description.contains(QStringLiteral("ESP32"), Qt::CaseInsensitive) ||
           description.contains(QStringLiteral("USB JTAG"), Qt::CaseInsensitive) ||
           description.contains(QStringLiteral("Waveshare"), Qt::CaseInsensitive) ||
           description.contains(QStringLiteral("CP210"), Qt::CaseInsensitive) ||
           description.contains(QStringLiteral("CH340"), Qt::CaseInsensitive) ||
           description.contains(QStringLiteral("CH910"), Qt::CaseInsensitive);
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

UsbFrameTransport::UsbFrameTransport(QObject *parent)
    : QObject(parent)
{
    m_probeTimer.setSingleShot(true);
    m_helloTimer.setInterval(250);
    m_pingTimer.setInterval(15000);
    m_pingResponseTimer.setSingleShot(true);
    m_retryTimer.setSingleShot(true);
    m_frameTimer.setSingleShot(true);
    connect(&m_probeTimer, &QTimer::timeout, this, &UsbFrameTransport::handleProbeTimeout);
    connect(&m_helloTimer, &QTimer::timeout, this, &UsbFrameTransport::handleHelloTimer);
    connect(&m_pingTimer, &QTimer::timeout, this, &UsbFrameTransport::handlePingTimer);
    connect(&m_pingResponseTimer, &QTimer::timeout, this,
            &UsbFrameTransport::handlePingResponseTimeout);
    connect(&m_retryTimer, &QTimer::timeout, this, &UsbFrameTransport::handleRetryTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &UsbFrameTransport::handleFrameTimeout);
}

void UsbFrameTransport::probePanel(const QString &panelName)
{
    if (m_expectedPanelName != panelName) {
        stop();
        m_expectedPanelName = panelName;
    }
    if (m_expectedPanelName.isEmpty()) {
        return;
    }
    m_ready = true;
    if (!m_connected && !m_probeTimer.isActive() && !m_retryTimer.isActive()) {
        beginProbeCycle();
    }
}

void UsbFrameTransport::stop()
{
    m_ready = false;
    m_probeTimer.stop();
    m_helloTimer.stop();
    m_pingTimer.stop();
    m_pingResponseTimer.stop();
    m_retryTimer.stop();
    m_frameTimer.stop();
    closePort();
    m_candidates.clear();
    m_readBuffer.clear();
    m_writeBuffer.clear();
    m_writeOffset = 0;
    m_inFlight = false;
    m_retryPending = false;
    m_retryCount = 0;
    m_hasPendingFrame = false;
    m_pendingFrame = {};
    m_inFlightFrame = {};
}

void UsbFrameTransport::setReady(bool ready)
{
    if (ready) {
        m_ready = true;
        if (!m_expectedPanelName.isEmpty() && !m_connected) {
            beginProbeCycle();
        }
    } else {
        stop();
    }
}

void UsbFrameTransport::sendFrame(const PanelFrame &frame)
{
    if (frame.encoding != PanelFrame::Encoding::Jpeg || frame.bytes.isEmpty() ||
        frame.width == 0 || frame.height == 0 ||
        static_cast<quint64>(frame.bytes.size()) > std::numeric_limits<quint32>::max()) {
        return;
    }
    if (!m_ready || !m_connected || m_inFlight || m_retryPending) {
        m_pendingFrame = frame;
        m_hasPendingFrame = true;
        return;
    }
    m_retryCount = 0;
    sendFrameNow(frame);
}

void UsbFrameTransport::beginProbeCycle()
{
    if (!m_ready || m_connected || m_expectedPanelName.isEmpty()) {
        return;
    }
    closePort();
    m_candidates.clear();
    for (const auto &candidate : QSerialPortInfo::availablePorts()) {
        if (isEsp32SerialPort(candidate)) {
            m_candidates.append(candidate);
        }
    }
    m_candidateIndex = 0;
    tryNextPort();
}

void UsbFrameTransport::tryNextPort()
{
    m_probeTimer.stop();
    m_helloTimer.stop();
    closePort();
    if (!m_ready) {
        return;
    }
    if (m_candidateIndex >= m_candidates.size()) {
        m_retryTimer.start(3000);
        return;
    }

    const QSerialPortInfo info = m_candidates.at(m_candidateIndex++);
    m_port = new QSerialPort(info, this);
    m_port->setBaudRate(QSerialPort::Baud115200);
    if (!m_port->open(QIODevice::ReadWrite)) {
        qDebug() << "could not open candidate LCD USB port" << info.portName()
                 << m_port->errorString();
        m_port->deleteLater();
        m_port = nullptr;
        tryNextPort();
        return;
    }
    connect(m_port, &QSerialPort::readyRead, this, &UsbFrameTransport::handleReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &UsbFrameTransport::handlePortError);
    connect(m_port, &QSerialPort::bytesWritten, this, &UsbFrameTransport::handleBytesWritten);
    qDebug() << "probing LCD USB port" << info.portName();
    m_readBuffer.clear();
    sendHello();
    m_helloTimer.start();
    m_probeTimer.start(2500);
}

void UsbFrameTransport::closePort()
{
    const bool wasConnected = m_connected;
    m_connected = false;
    if (m_port) {
        disconnect(m_port, nullptr, this, nullptr);
        m_port->close();
        m_port->deleteLater();
        m_port = nullptr;
    }
    if (wasConnected) {
        emit panelConnectionChanged(false);
    }
}

void UsbFrameTransport::sendHello()
{
    if (m_port && m_port->isOpen() && !m_connected) {
        m_port->write("INKTF_USB_HELLO\n");
    }
}

void UsbFrameTransport::handleReadyRead()
{
    if (!m_port) {
        return;
    }
    m_readBuffer.append(m_port->readAll());
    while (true) {
        const qsizetype newline = m_readBuffer.indexOf('\n');
        if (newline < 0) {
            if (m_readBuffer.size() > 4096) {
                m_readBuffer.clear();
            }
            return;
        }
        QByteArray line = m_readBuffer.left(newline).trimmed();
        m_readBuffer.remove(0, newline + 1);
        processLine(line);
    }
}

void UsbFrameTransport::processLine(const QByteArray &line)
{
    static const QByteArray panelPrefix("INKTF_USB_PANEL ");
    const qsizetype panelPrefixIndex = line.indexOf(panelPrefix);
    if (panelPrefixIndex >= 0) {
        const QByteArray panelNameBytes =
            line.mid(panelPrefixIndex + panelPrefix.size()).split(' ').first();
        const QString panelName = QString::fromUtf8(panelNameBytes);
        if (panelName != m_expectedPanelName) {
            qDebug() << "USB panel identity mismatch:" << panelName << "expected"
                     << m_expectedPanelName;
            tryNextPort();
            return;
        }
        m_probeTimer.stop();
        m_helloTimer.stop();
        m_retryTimer.stop();
        m_connected = true;
        qInfo() << "USB frame transport connected to" << panelName;
        emit panelConnectionChanged(true);
        emit linkActivity();
        m_pingResponseTimer.stop();
        m_pingTimer.start();
        if (m_hasPendingFrame && !m_inFlight) {
            PanelFrame pending = std::move(m_pendingFrame);
            m_hasPendingFrame = false;
            m_retryCount = 0;
            sendFrameNow(pending);
        }
        return;
    }
    if (line.contains("INKTF_USB_PONG")) {
        m_pingResponseTimer.stop();
        emit linkActivity();
        return;
    }
    static const QByteArray ackPrefix("INKTF_USB_ACK ");
    const qsizetype ackPrefixIndex = line.indexOf(ackPrefix);
    if (ackPrefixIndex >= 0) {
        const QList<QByteArray> fields = line.mid(ackPrefixIndex + ackPrefix.size()).split(' ');
        if (fields.size() >= 2 && m_inFlight &&
            fields.at(0).toUInt() == m_inFlightSequence) {
            emit linkActivity();
            finishFrame(fields.at(1).startsWith("OK"));
        }
    }
}

void UsbFrameTransport::sendFrameNow(const PanelFrame &frame)
{
    if (!m_connected || !m_port || !m_port->isOpen()) {
        m_pendingFrame = frame;
        m_hasPendingFrame = true;
        return;
    }
    m_pingResponseTimer.stop();
    m_inFlightFrame = frame;
    m_inFlight = true;
    m_inFlightSequence = m_nextSequence++;
    m_writeOffset = 0;
    m_writeBuffer = QStringLiteral("INKTF_USB_FRAME %1 %2 %3 %4 %5\n")
                        .arg(static_cast<qulonglong>(m_inFlightSequence))
                        .arg(static_cast<qulonglong>(frame.width))
                        .arg(static_cast<qulonglong>(frame.height))
                        .arg(static_cast<qulonglong>(frame.bytes.size()))
                        .arg(static_cast<qulonglong>(crc32(frame.bytes)))
                        .toLatin1();
    m_writeBuffer.append(frame.bytes);
    m_frameTimer.start(45000);
    qDebug() << "sending LCD JPEG frame over USB" << frame.width << "x" << frame.height
             << frame.bytes.size() << "bytes";
    pumpWriteBuffer();
}

void UsbFrameTransport::pumpWriteBuffer()
{
    if (!m_inFlight || !m_port || !m_port->isOpen() ||
        m_port->bytesToWrite() > 64 * 1024 || m_writeOffset >= m_writeBuffer.size()) {
        return;
    }
    const qsizetype count = qMin<qsizetype>(16 * 1024, m_writeBuffer.size() - m_writeOffset);
    const qint64 written = m_port->write(m_writeBuffer.constData() + m_writeOffset, count);
    if (written < 0) {
        finishFrame(false);
        return;
    }
    m_writeOffset += written;
    if (written == 0) {
        return;
    }
    if (m_writeOffset < m_writeBuffer.size()) {
        pumpWriteBuffer();
    }
}

void UsbFrameTransport::finishFrame(bool success)
{
    if (!m_inFlight) {
        return;
    }
    m_frameTimer.stop();
    if (!success && m_retryCount == 0 && m_connected) {
        m_retryCount = 1;
        m_inFlight = false;
        m_retryPending = true;
        m_writeBuffer.clear();
        m_writeOffset = 0;
        m_retryTimer.start(250);
        return;
    }
    m_inFlight = false;
    m_retryPending = false;
    m_writeBuffer.clear();
    m_writeOffset = 0;
    m_inFlightFrame = {};
    m_retryCount = 0;
    emit frameFinished(success);
    if (m_hasPendingFrame && m_connected) {
        PanelFrame pending = std::move(m_pendingFrame);
        m_hasPendingFrame = false;
        sendFrameNow(pending);
    }
}

void UsbFrameTransport::handlePortError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }
    if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError) {
        qWarning() << "LCD USB serial port disconnected"
                   << (m_port ? m_port->errorString() : QString());
        m_probeTimer.stop();
        m_helloTimer.stop();
        m_pingTimer.stop();
        m_pingResponseTimer.stop();
        m_frameTimer.stop();
        m_inFlight = false;
        m_retryPending = false;
        m_hasPendingFrame = false;
        m_pendingFrame = {};
        m_inFlightFrame = {};
        m_writeBuffer.clear();
        m_writeOffset = 0;
        closePort();
        if (m_ready) {
            m_retryTimer.start(1000);
        }
    }
}

void UsbFrameTransport::handleProbeTimeout()
{
    if (!m_connected) {
        tryNextPort();
    }
}

void UsbFrameTransport::handleHelloTimer()
{
    sendHello();
}

void UsbFrameTransport::handlePingTimer()
{
    if (m_connected && m_port && !m_inFlight && !m_retryPending && !m_hasPendingFrame) {
        if (m_port->write("INKTF_USB_PING\n") >= 0) {
            m_pingResponseTimer.start(5000);
        }
    }
}

void UsbFrameTransport::handlePingResponseTimeout()
{
    if (!m_connected) {
        return;
    }
    qWarning() << "LCD USB panel stopped responding";
    m_pingTimer.stop();
    m_frameTimer.stop();
    m_inFlight = false;
    m_retryPending = false;
    m_hasPendingFrame = false;
    m_inFlightFrame = {};
    m_pendingFrame = {};
    m_writeBuffer.clear();
    m_writeOffset = 0;
    closePort();
    if (m_ready) {
        m_retryTimer.start(1000);
    }
}

void UsbFrameTransport::handleRetryTimer()
{
    if (!m_ready) {
        return;
    }
    if (m_retryPending && m_connected) {
        m_retryPending = false;
        sendFrameNow(m_inFlightFrame);
        return;
    }
    if (!m_connected) {
        beginProbeCycle();
    }
}

void UsbFrameTransport::handleFrameTimeout()
{
    finishFrame(false);
}

void UsbFrameTransport::handleBytesWritten([[maybe_unused]] qint64 bytes)
{
    pumpWriteBuffer();
}
