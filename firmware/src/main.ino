// vim: foldmethod=marker:foldmarker={{{,}}}
#include <iomanip>
#include <limits>
#include <sstream>

#include <Adafruit_MAX1704X.h>
#if defined(INKTERFACE_LCD5B)
#define EPD_BLACK 0x18C3
#define EPD_WHITE 0xFFFF
#include "lcd5b_display.h"
#else
#include <Adafruit_ThinkInk.h>
#endif
#include <NimBLEDevice.h>
#include <esp_sleep.h>

#if defined(GABEN_STARTUP)
#include "gaben.h"
#endif

#define SERVICE_UUID                                                                               \
    NimBLEUUID { "95c7b479-8e84-4ce7-a121-faf74bf48c84" }
#define TOPLINE_UUID                                                                               \
    NimBLEUUID { "d6f4c07e-4a21-4c69-bd15-43a38a871900" }
#define MIDLINE_UUID                                                                               \
    NimBLEUUID { "d6f4c07e-4a21-4c69-bd15-43a38a871901" }
#define BOTLINE_UUID                                                                               \
    NimBLEUUID { "d6f4c07e-4a21-4c69-bd15-43a38a871902" }
#define KEYVAL_UUID                                                                                \
    NimBLEUUID { "d6f4c07e-4a21-4c69-bd15-43a38a871903" }
#define VECTOR_UUID                                                                                \
    NimBLEUUID { "d6f4c07e-4a21-4c69-bd15-43a38a871904" }
#define ARTWORK_UUID                                                                               \
    NimBLEUUID { "d6f4c07e-4a21-4c69-bd15-43a38a871905" }
#if defined(INKTERFACE_LCD5B)
#define BACKLIGHT_POWER_UUID                                                                      \
    NimBLEUUID { "d6f4c07e-4a21-4c69-bd15-43a38a871907" }
#endif
#define FLUSH_UUID                                                                                 \
    NimBLEUUID { "d6f4c07e-4a21-4c69-bd15-43a38a8719FF" }

#if defined(INKTERFACE_LCD5B)
static constexpr int16_t SPARKBOX_HEIGHT = 174;
static constexpr int16_t SPARKBOX_WIDTH = 324;
#else
static constexpr int16_t SPARKBOX_HEIGHT = 100;
static constexpr int16_t SPARKBOX_WIDTH = 209;
#endif

// Full-panel artwork framebuffer, 1bpp row-major with MSB-first bytes (the
// layout Adafruit_GFX::drawBitmap() expects). Allocate it lazily in PSRAM so
// internal SRAM remains available for the GFX working image and RGB DMA buffers.
#define ART_MAX_WIDTH 648
#define ART_MAX_HEIGHT 480
#define ART_BUFFER_SIZE ((ART_MAX_WIDTH / 8) * ART_MAX_HEIGHT)
#define ART_JPEG_MAX_SIZE (512 * 1024)
static uint8_t *ART_BUFFER = nullptr;
static uint8_t *ART_JPEG_BUFFER = nullptr;
static uint32_t ART_JPEG_EXPECTED_SIZE = 0;
static uint32_t ART_JPEG_RECEIVED_SIZE = 0;

static bool artBufferReady()
{
    if (ART_BUFFER != nullptr) {
        return true;
    }
    ART_BUFFER = (uint8_t *)ps_malloc(ART_BUFFER_SIZE);
    if (ART_BUFFER == nullptr) {
        ART_BUFFER = (uint8_t *)malloc(ART_BUFFER_SIZE);
    }
    return ART_BUFFER != nullptr;
}

#define INTERFACE_VERSION "IFv01"

// minimum voltage battery can reach before we go into deep sleep
#define BATT_MINV 2.9

NimBLEServer *BLE_SERVER = nullptr;
std::string BLE_NAME = "INKTF";

static std::string makeBleName()
{
    uint32_t addr = (uint64_t)NimBLEDevice::getAddress() & 0xFFFFFF;
    std::stringstream name;
#if defined(INKTERFACE_LCD5B)
    // The host app uses this short model marker to choose the LCD refresh cadence.
    name << "INKTF-5B-";
#else
    name << "INKTF-";
#endif
    name << std::uppercase << std::hex << std::setfill('0') << std::setw(6) << addr;
    return name.str();
}

Adafruit_MAX17048 maxlipo;
bool MAXLIPO_PRESENT = false;

bool INVERTED = false;
#define FG_COLOR (INVERTED ? EPD_WHITE : EPD_BLACK)
#define BG_COLOR (INVERTED ? EPD_BLACK : EPD_WHITE)

class DualPrint : public Print
{ // {{{
  public:
    DualPrint(Print &a, Print *b)
        : _a(a)
        , _b(b)
    {
    }
    size_t write(uint8_t c) override
    {
        size_t written = _a.write(c);
        if (_b != nullptr) {
            _b->write(c);
        }
        return written;
    }
    size_t write(const uint8_t *buffer, size_t size) override
    {
        size_t written = _a.write(buffer, size);
        if (_b != nullptr) {
            _b->write(buffer, size);
        }
        return written;
    }

  private:
    Print &_a;
    Print *_b;
}; // }}}

#if defined(INKTERFACE_LCD5B)
DualPrint Debug(Serial, nullptr);
LCD5BDisplay MF_DISPLAY(EPD_BLACK);

static bool beginDisplay() { return MF_DISPLAY.begin(); }
#else
DualPrint Debug(Serial, &Serial1);
class CustomDisp : public ThinkInk_583_Mono_AAAMFGN
{ // {{{
  public:
    CustomDisp(int16_t SID, int16_t SCLK, int16_t DC, int16_t RST, int16_t CS, int16_t SRCS,
               int16_t MISO, int16_t BUSY = -1)
        : ThinkInk_583_Mono_AAAMFGN(SID, SCLK, DC, RST, CS, SRCS, MISO, BUSY){};

    CustomDisp(int16_t DC, int16_t RST, int16_t CS, int16_t SRCS, int16_t BUSY = -1,
               SPIClass *spi = &SPI)
        : ThinkInk_583_Mono_AAAMFGN(DC, RST, CS, SRCS, BUSY, spi){};

    // experimenting with adding windowed/partial refresh
    void partialWindow(uint16_t x = 8, uint16_t w = 198, uint16_t y = 92, uint16_t h = 110,
                       bool pt_scan = true)
    {
        uint16_t hrst = x;
        uint16_t hred = x + w;
        uint16_t vrst = y;
        uint16_t vred = y + h;
        uint8_t buf[9] = {0};
        buf[0] = (hrst & 0x300) >> 8; // bits 9:8 of HRST, top 6 bits unused
        buf[1] = hrst & 0xf8;         // bits 7:3 of HRST, bot 3 bits must be 0
        buf[2] = (hred & 0x300) >> 8; // bits 9:8 of HRED, top 6 bits unused
        buf[3] = (hred & 0xf8) | 0x7; // bits 7:3 of HRED, bot 3 bits must be 1
        buf[4] = (vrst & 0x300) >> 8; // bits 9:8 of VRST, top 6 bits unused
        buf[5] = vrst & 0xff;         // bits 7:0 of VRST
        buf[6] = (vred & 0x300) >> 8; // bits 9:8 of VRED, top 6 bits unused
        buf[7] = vred & 0xff;         // bits 7:0 of VRED
        buf[8] = pt_scan ? 1 : 0;     // bottom bit, PT_SCAN flag
        EPD_command(0x90);
        EPD_data(buf, sizeof(buf));
    }
    void partialIn() { EPD_command(0x91); }
    void partialOut() { EPD_command(0x90); }
}; // }}}

// ThinkInk_583_Mono_AAAMFGN MF_DISPLAY(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);
CustomDisp MF_DISPLAY(EPD_DC, EPD_RESET, EPD_CS, -1 /* SRAM_CS */, EPD_BUSY);

static bool beginDisplay()
{
    MF_DISPLAY.begin(THINKINK_MONO);
    return true;
}
#endif

static unsigned long DISP_DEBOUNCE = 0;

struct Point { // {{{
    float x;
    float y;

    Point()
        : x(0)
        , y(0)
    {
    }
    Point(float _x, float _y)
        : x(_x)
        , y(_y)
    {
    }
}; // }}}

struct Points { // {{{
    float yMin;
    float yMax;
    std::vector<Point> points;

    Points()
        : yMin(0)
        , yMax(0)
        , points()
    {
    }

    void clear()
    {
        yMin = 0;
        yMax = 0;
        points.clear();
    }
}; // }}}

struct KeyVal { // {{{
    std::string key;
    std::string val;

    KeyVal()
        : key{""}
        , val{""}
    {
    }
}; // }}}
typedef std::vector<KeyVal> KeyVals;

struct State { // {{{
    bool connected = false;
    std::string topLine{"Starting up..."};
    std::string midLine{"No User"};
    std::string botLine{"No Activity"};
    std::string hostMsg{""};
    std::string battLine{""};

    KeyVals keyvals{9};
    std::vector<Points> sparks{6};

    // when true the display shows the host-provided ART_BUFFER frame
    // instead of the telemetry layout
    bool artMode = false;
    bool artJpegMode = false;
    uint16_t artWidth = 0;
    uint16_t artHeight = 0;
    uint32_t artJpegSize = 0;

    void reset()
    {
        keyvals.clear();
        keyvals.resize(9);
        sparks.clear();
        sparks.resize(6);

        artMode = false;
        artJpegMode = false;
        artWidth = 0;
        artHeight = 0;
        artJpegSize = 0;

        BLE_NAME = makeBleName();

        connected = false;
        topLine = "Waiting on connection...";
        midLine = BLE_NAME;
        botLine = "";
        hostMsg = "";
        keyvals[0].key = "OS";
        keyvals[0].val = "--";
        keyvals[1].key = "BIOS";
        keyvals[1].val = "--";
        keyvals[2].key = "STEAM";
        keyvals[2].val = "--";
        keyvals[3].key = "CPU";
        keyvals[3].val = "-- dC";
        keyvals[4].key = "GPU";
        keyvals[4].val = "-- dC";
        keyvals[5].key = "FAN";
        keyvals[5].val = "-- RPM";
        keyvals[6].key = "CPU";
        keyvals[6].val = "--%";
        keyvals[7].key = "GPU";
        keyvals[7].val = "--%";
        keyvals[8].key = "MEM";
        keyvals[8].val = "--%";
    }
} STATE; // }}}

void drawStatic();
void drawArt();

class ServerCallbacks : public NimBLEServerCallbacks
{ // {{{
    void onConnect(NimBLEServer *server, NimBLEConnInfo &conn) override
    {
        Debug.println("got connection");
        // we don't want any other devices to see us once we are connected
        // to a host
        NimBLEDevice::stopAdvertising();
        STATE.connected = true;
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &conn, int reason) override
    {
        Debug.print("got disconnect event, connected count: ");
        Debug.println(server->getConnectedCount());
        // connected count appears to be updated after this callback is
        // triggered, so the count will be at least 1 higher than reality
        if (server->getConnectedCount() <= 1) {
            if (STATE.connected) {
                DISP_DEBOUNCE = 100;
            }
            STATE.reset();
        }
        NimBLEDevice::startAdvertising();
    }
} SERVER_CALLBACKS; // }}}

class StatusLineCallbacks : public NimBLECharacteristicCallbacks
{ // {{{
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &conn) override
    {
        std::string value = characteristic->getValue();
        auto uuid = characteristic->getUUID();
        if (uuid == TOPLINE_UUID && STATE.topLine != value) {
            STATE.topLine = value;
        } else if (uuid == MIDLINE_UUID && STATE.midLine != value) {
            STATE.midLine = value;
        } else if (uuid == BOTLINE_UUID && STATE.botLine != value) {
            STATE.botLine = value;
        } else if (uuid != TOPLINE_UUID && uuid != MIDLINE_UUID && uuid != BOTLINE_UUID) {
            Debug.print("Got value (");
            Debug.print(value.c_str());
            Debug.print(") for unknown UUID (");
            Debug.print(uuid.toString().c_str());
            Debug.println("), ignoring.");
            return;
        }
    }
} STATUS_CALLBACKS; // }}}

class KeyValCallbacks : public NimBLECharacteristicCallbacks
{ // {{{
    typedef struct __attribute__((packed)) {
        uint8_t index;
        char key[32];
        char val[32];
    } Msg;

    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &conn) override
    {
        std::string value = characteristic->getValue();
        Msg msg;
        if (value.length() == sizeof(Msg)) {
            memcpy(&msg, value.data(), sizeof(Msg));
            STATE.keyvals[msg.index].key = msg.key;
            STATE.keyvals[msg.index].val = msg.val;
        } else {
            Debug.print("got bad keyval write, size: ");
            Debug.println(value.length());
        }
    }
} KEYVAL_CALLBACKS; // }}}

class VectorCallbacks : public NimBLECharacteristicCallbacks
{ // {{{
    typedef struct __attribute__((packed)) {
        uint8_t index;
        uint8_t count;
        float minVal;
        float maxVal;
        uint8_t values[32 * 2]; // 32 (x, y) pairs, 64 bytes
        // total 74 bytes
    } Msg;

    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &conn) override
    {
        std::string value = characteristic->getValue();
        Msg msg;
        if (value.length() >= 2) {
            memcpy(&msg, value.data(), sizeof(Msg));
            Debug.print("got vector for index (");
            Debug.print(msg.index);
            Debug.print(") with ");
            Debug.print(msg.count);
            Debug.print(" values, min ");
            Debug.print(msg.minVal);
            Debug.print(", max ");
            Debug.println(msg.maxVal);
            STATE.sparks[msg.index].clear();
            STATE.sparks[msg.index].yMin = msg.minVal;
            STATE.sparks[msg.index].yMax = msg.maxVal;
            for (int i = 0; i < msg.count; i += 2) {
                STATE.sparks[msg.index].points.emplace_back(msg.values[i] / 255.0,
                                                            msg.values[i + 1] / 255.0);
            }
        } else {
            Debug.print("got bad vectors write, size: ");
            Debug.println(value.length());
        }
    }
} VECTOR_CALLBACKS; // }}}

class ArtworkCallbacks : public NimBLECharacteristicCallbacks
{ // {{{
    // opcodes for the artwork transfer protocol, all multi-byte ints are LE:
    //   0x00 BEGIN: uint16 width, uint16 height; resets the frame
    //   0x01 DATA:  uint32 byte offset, then raw 1bpp payload bytes
    //   0x02 SHOW:  switch the display to the uploaded frame
    //   0x03 CLEAR: return the display to the telemetry layout
    //   0x04 JPEG_BEGIN: uint16 width, uint16 height, uint32 byte count
    //   0x05 JPEG_DATA:  uint32 byte offset, then JPEG payload bytes
    //   0x06 JPEG_SHOW:  decode and present the complete native-size color frame
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &conn) override
    {
        std::string value = characteristic->getValue();
        if (value.empty()) {
            return;
        }
        const uint8_t *data = (const uint8_t *)value.data();
        switch (data[0]) {
        case 0x00: {
            if (value.length() < 5) {
                Debug.println("got short artwork begin message");
                return;
            }
            uint16_t w = data[1] | (data[2] << 8);
            uint16_t h = data[3] | (data[4] << 8);
            if (w > ART_MAX_WIDTH || h > ART_MAX_HEIGHT) {
                Debug.println("rejecting oversized artwork frame");
                return;
            }
            if (!artBufferReady()) {
                Debug.println("no memory for artwork frame, rejecting upload");
                return;
            }
            STATE.artWidth = w;
            STATE.artHeight = h;
            STATE.artJpegMode = false;
            STATE.artJpegSize = 0;
            memset(ART_BUFFER, 0, ART_BUFFER_SIZE);
            break;
        }
        case 0x01: {
            if (value.length() < 6) {
                Debug.println("got short artwork data message");
                return;
            }
            uint32_t offset = data[1] | (data[2] << 8) | (data[3] << 16) | ((uint32_t)data[4] << 24);
            size_t len = value.length() - 5;
            if (ART_BUFFER == nullptr) {
                Debug.println("ignoring artwork data before a begin message");
                return;
            }
            if (offset + len > ART_BUFFER_SIZE) {
                Debug.println("rejecting artwork data past end of buffer");
                return;
            }
            memcpy(ART_BUFFER + offset, data + 5, len);
            break;
        }
        case 0x02: {
            if (STATE.artWidth == 0 || STATE.artHeight == 0) {
                Debug.println("ignoring artwork show, no frame uploaded");
                return;
            }
            Debug.println("showing artwork frame");
            STATE.artMode = true;
            STATE.artJpegMode = false;
            DISP_DEBOUNCE = 100;
            break;
        }
        case 0x03: {
            // only redraw if we were actually showing artwork, so the host can
            // safely re-assert a clear on every reconnect without flashing a
            // panel that is already showing telemetry
            if (STATE.artMode) {
                Debug.println("leaving artwork mode");
                STATE.artMode = false;
                STATE.artJpegMode = false;
                DISP_DEBOUNCE = 100;
            }
            break;
        }
#if defined(INKTERFACE_LCD5B)
        case 0x04: {
            if (value.length() < 9) {
                Debug.println("got short JPEG artwork begin message");
                return;
            }
            const uint16_t w = data[1] | (data[2] << 8);
            const uint16_t h = data[3] | (data[4] << 8);
            const uint32_t byteCount = static_cast<uint32_t>(data[5]) |
                                       (static_cast<uint32_t>(data[6]) << 8) |
                                       (static_cast<uint32_t>(data[7]) << 16) |
                                       (static_cast<uint32_t>(data[8]) << 24);
            if (w != LCD5B_WIDTH || h != LCD5B_HEIGHT || byteCount == 0 ||
                byteCount > ART_JPEG_MAX_SIZE) {
                Debug.println("rejecting unsupported or oversized JPEG artwork frame");
                ART_JPEG_EXPECTED_SIZE = 0;
                ART_JPEG_RECEIVED_SIZE = 0;
                return;
            }
            if (ART_JPEG_BUFFER == nullptr) {
                ART_JPEG_BUFFER = static_cast<uint8_t *>(ps_malloc(ART_JPEG_MAX_SIZE));
                if (ART_JPEG_BUFFER == nullptr) {
                    Debug.println("no PSRAM for JPEG artwork frame");
                    ART_JPEG_EXPECTED_SIZE = 0;
                    ART_JPEG_RECEIVED_SIZE = 0;
                    return;
                }
            }
            ART_JPEG_EXPECTED_SIZE = byteCount;
            ART_JPEG_RECEIVED_SIZE = 0;
            break;
        }
        case 0x05: {
            if (value.length() < 6 || ART_JPEG_BUFFER == nullptr ||
                ART_JPEG_EXPECTED_SIZE == 0) {
                Debug.println("ignoring JPEG data before a valid begin message");
                return;
            }
            const uint32_t offset = static_cast<uint32_t>(data[1]) |
                                    (static_cast<uint32_t>(data[2]) << 8) |
                                    (static_cast<uint32_t>(data[3]) << 16) |
                                    (static_cast<uint32_t>(data[4]) << 24);
            const size_t len = value.length() - 5;
            if (offset != ART_JPEG_RECEIVED_SIZE || offset + len > ART_JPEG_EXPECTED_SIZE) {
                Debug.println("rejecting out-of-order or oversized JPEG artwork data");
                ART_JPEG_EXPECTED_SIZE = 0;
                ART_JPEG_RECEIVED_SIZE = 0;
                return;
            }
            memcpy(ART_JPEG_BUFFER + offset, data + 5, len);
            ART_JPEG_RECEIVED_SIZE += len;
            break;
        }
        case 0x06: {
            if (ART_JPEG_EXPECTED_SIZE == 0 ||
                ART_JPEG_RECEIVED_SIZE != ART_JPEG_EXPECTED_SIZE) {
                Debug.println("ignoring incomplete JPEG artwork frame");
                return;
            }
            STATE.artWidth = LCD5B_WIDTH;
            STATE.artHeight = LCD5B_HEIGHT;
            STATE.artJpegSize = ART_JPEG_EXPECTED_SIZE;
            STATE.artJpegMode = true;
            STATE.artMode = true;
            Debug.println("showing full-color JPEG artwork frame");
            DISP_DEBOUNCE = 100;
            break;
        }
#endif
        default: {
            Debug.print("got unknown artwork opcode: ");
            Debug.println(data[0]);
            break;
        }
        }
    }
} ARTWORK_CALLBACKS; // }}}

#if defined(INKTERFACE_LCD5B)
class BacklightCallbacks : public NimBLECharacteristicCallbacks
{ // {{{
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override
    {
        const std::string value = characteristic->getValue();
        if (value.size() != 1) {
            Debug.println("ignoring malformed backlight power write");
            return;
        }
        const uint8_t enabled = static_cast<uint8_t>(value[0]);
        if (enabled > 1) {
            Debug.println("ignoring invalid backlight power value");
            return;
        }
        if (MF_DISPLAY.setBacklightEnabled(enabled != 0)) {
            Debug.print("LCD backlight ");
            Debug.println(enabled ? "on" : "off");
        } else {
            Debug.println("could not update LCD backlight enable output");
        }
    }
} BACKLIGHT_CALLBACKS; // }}}
#endif

class FlushCallbacks : public NimBLECharacteristicCallbacks
{ // {{{
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &conn) override
    {
        STATE.hostMsg = characteristic->getValue();
        DISP_DEBOUNCE = 100;
    }
} FLUSH_CALLBACKS; // }}}

#if defined(INKTERFACE_LCD5B) && defined(LCD5B_ANIMATION_TEST)
static constexpr uint8_t LCD_TEST_FRAME_RATES[] = {15, 26, 41};
static constexpr uint32_t LCD_TEST_PHASE_MS = 8000;

static void drawLcdAnimationFrame(uint8_t targetFps, uint32_t frameNumber)
{
    MF_DISPLAY.clearBuffer();
    MF_DISPLAY.fillScreen(BG_COLOR);
    MF_DISPLAY.drawRect(0, 0, LCD5B_WIDTH, LCD5B_HEIGHT, FG_COLOR);
    MF_DISPLAY.drawFastHLine(0, LCD5B_HEIGHT / 2, LCD5B_WIDTH, FG_COLOR);
    MF_DISPLAY.drawFastVLine(LCD5B_WIDTH / 2, 0, LCD5B_HEIGHT, FG_COLOR);

    MF_DISPLAY.setTextColor(FG_COLOR);
    MF_DISPLAY.setTextSize(2);
    MF_DISPLAY.setCursor(20, 18);
    MF_DISPLAY.print("LCD-5B FRAME TEST");
    MF_DISPLAY.setTextSize(1);
    MF_DISPLAY.setCursor(20, 54);
    MF_DISPLAY.print("TARGET ");
    MF_DISPLAY.print(targetFps);
    MF_DISPLAY.print(" FPS  |  MOVING BAR");

    const int16_t travel = LCD5B_WIDTH - 96;
    const uint32_t span = static_cast<uint32_t>(travel) * 2;
    const int16_t offset = static_cast<int16_t>((frameNumber * 12) % span);
    const int16_t barX = 32 + (offset <= travel ? offset : span - offset);
    MF_DISPLAY.fillRect(barX, 150, 64, 220, FG_COLOR);
    MF_DISPLAY.fillRect(barX + 12, 162, 40, 196, BG_COLOR);
    MF_DISPLAY.fillCircle(barX + 32, 260, 12, FG_COLOR);
    MF_DISPLAY.display();
}

static void runLcdAnimationTest()
{
    static uint8_t rateIndex = 0;
    static uint32_t phaseStartMs = 0;
    static uint32_t reportStartMs = 0;
    static uint32_t framesInWindow = 0;
    static uint32_t renderTotalUs = 0;
    static uint32_t renderMaxUs = 0;
    static uint32_t rasterTotalUs = 0;
    static uint32_t rasterMaxUs = 0;
    static uint32_t presentTotalUs = 0;
    static uint32_t presentMaxUs = 0;
    static uint32_t frameNumber = 0;
    static uint32_t nextFrameUs = 0;

    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();
    if (phaseStartMs == 0) {
        phaseStartMs = nowMs;
        reportStartMs = nowMs;
        nextFrameUs = nowUs;
        Serial.println("LCD animation test: cycling target rates 15, 26, 41 FPS");
    } else if (nowMs - phaseStartMs >= LCD_TEST_PHASE_MS) {
        rateIndex = (rateIndex + 1) % (sizeof(LCD_TEST_FRAME_RATES) / sizeof(LCD_TEST_FRAME_RATES[0]));
        phaseStartMs = nowMs;
        reportStartMs = nowMs;
        framesInWindow = 0;
        renderTotalUs = 0;
        renderMaxUs = 0;
        rasterTotalUs = 0;
        rasterMaxUs = 0;
        presentTotalUs = 0;
        presentMaxUs = 0;
        nextFrameUs = nowUs;
        Serial.printf("LCD animation target changed to %u FPS\n", LCD_TEST_FRAME_RATES[rateIndex]);
    }

    const uint8_t targetFps = LCD_TEST_FRAME_RATES[rateIndex];
    const uint32_t frameIntervalUs = 1000000UL / targetFps;
    if (static_cast<int32_t>(nowUs - nextFrameUs) >= 0) {
        const uint32_t renderStartUs = micros();
        drawLcdAnimationFrame(targetFps, frameNumber++);
        const uint32_t renderTimeUs = micros() - renderStartUs;
        const uint32_t rasterTimeUs = MF_DISPLAY.lastRasterUs();
        const uint32_t presentWaitUs = MF_DISPLAY.lastPresentWaitUs();
        nextFrameUs = renderStartUs + frameIntervalUs;

        ++framesInWindow;
        renderTotalUs += renderTimeUs;
        rasterTotalUs += rasterTimeUs;
        presentTotalUs += presentWaitUs;
        if (renderTimeUs > renderMaxUs) {
            renderMaxUs = renderTimeUs;
        }
        if (rasterTimeUs > rasterMaxUs) {
            rasterMaxUs = rasterTimeUs;
        }
        if (presentWaitUs > presentMaxUs) {
            presentMaxUs = presentWaitUs;
        }
        if (renderTimeUs < frameIntervalUs) {
            delayMicroseconds(frameIntervalUs - renderTimeUs);
        }
    }

    const uint32_t elapsedMs = millis() - reportStartMs;
    if (elapsedMs >= 1000) {
        const uint32_t actualFpsTenths = (framesInWindow * 10000UL + elapsedMs / 2) / elapsedMs;
        const uint32_t averageRenderUs = framesInWindow ? renderTotalUs / framesInWindow : 0;
        const uint32_t averageRasterUs = framesInWindow ? rasterTotalUs / framesInWindow : 0;
        const uint32_t averagePresentWaitUs = framesInWindow ? presentTotalUs / framesInWindow : 0;
        Serial.printf("LCD animation: target=%u FPS actual=%lu.%lu FPS frame(avg/max)=%lu/%lu us raster(avg/max)=%lu/%lu us present-wait(avg/max)=%lu/%lu us\n",
                      targetFps, static_cast<unsigned long>(actualFpsTenths / 10),
                      static_cast<unsigned long>(actualFpsTenths % 10),
                      static_cast<unsigned long>(averageRenderUs),
                      static_cast<unsigned long>(renderMaxUs),
                      static_cast<unsigned long>(averageRasterUs),
                      static_cast<unsigned long>(rasterMaxUs),
                      static_cast<unsigned long>(averagePresentWaitUs),
                      static_cast<unsigned long>(presentMaxUs));
        reportStartMs = millis();
        framesInWindow = 0;
        renderTotalUs = 0;
        renderMaxUs = 0;
        rasterTotalUs = 0;
        rasterMaxUs = 0;
        presentTotalUs = 0;
        presentMaxUs = 0;
    }
}
#endif

void setup()
{ // {{{
    Serial.begin(115200);
#if defined(INKTERFACE_LCD5B)
    Wire.begin(8, 9);
#else
    Serial1.begin(115200);
#endif
    // USB serial and, on the Feather targets, the auxiliary UART feed Debug.

#if defined(INKTERFACE_LCD5B) && defined(LCD5B_ANIMATION_TEST)
    if (!beginDisplay()) {
        Serial.println("LCD animation test failed to initialize the display");
        while (true) {
            delay(1000);
        }
    }
    Serial.println("LCD animation test is running; BLE is intentionally disabled");
    return;
#endif

#if defined(STARTUP_DELAY_MS)
    delay(STARTUP_DELAY_MS);
#endif

    // TODO: add support for more battery tracking on different boards, etc.
    Debug.println("setting up i2c interface");
    MAXLIPO_PRESENT = maxlipo.begin();
    if (!MAXLIPO_PRESENT) {
        Debug.println("failed to setup MAX17048, not present on all boards!");
    }

    Debug.println("setting up ble device and service");
    NimBLEDevice::init("");
    // the panel magnets to a metal chassis that shields the host's BT antenna,
    // so use a healthy TX power; +9 dBm is the safe max on both feather boards
    NimBLEDevice::setPower(9);
    NimBLEDevice::setMTU(256); // bump the mtu to fit a decent number of points
    BLE_SERVER = NimBLEDevice::createServer();
    BLE_SERVER->setCallbacks(&SERVER_CALLBACKS);
    BLEService *service = BLE_SERVER->createService(SERVICE_UUID);
    BLECharacteristic *characteristic = nullptr;

    // status line characteristics, they share callbacks
    characteristic =
        service->createCharacteristic(TOPLINE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    characteristic->setValue(STATE.topLine.c_str());
    characteristic->setCallbacks(&STATUS_CALLBACKS);
    characteristic =
        service->createCharacteristic(MIDLINE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    characteristic->setValue(STATE.midLine.c_str());
    characteristic->setCallbacks(&STATUS_CALLBACKS);
    characteristic =
        service->createCharacteristic(BOTLINE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    characteristic->setValue(STATE.botLine.c_str());
    characteristic->setCallbacks(&STATUS_CALLBACKS);

    characteristic = service->createCharacteristic(KEYVAL_UUID, NIMBLE_PROPERTY::WRITE);
    characteristic->setCallbacks(&KEYVAL_CALLBACKS);
    characteristic = service->createCharacteristic(VECTOR_UUID, NIMBLE_PROPERTY::WRITE);
    characteristic->setCallbacks(&VECTOR_CALLBACKS);
    characteristic = service->createCharacteristic(ARTWORK_UUID, NIMBLE_PROPERTY::WRITE |
                                                                     NIMBLE_PROPERTY::WRITE_NR);
    characteristic->setCallbacks(&ARTWORK_CALLBACKS);

#if defined(INKTERFACE_LCD5B)
    characteristic = service->createCharacteristic(BACKLIGHT_POWER_UUID, NIMBLE_PROPERTY::WRITE);
    characteristic->setCallbacks(&BACKLIGHT_CALLBACKS);
#endif

    characteristic = service->createCharacteristic(FLUSH_UUID, NIMBLE_PROPERTY::WRITE);
    characteristic->setCallbacks(&FLUSH_CALLBACKS);

    BLE_SERVER->start();

    Debug.println("initializing display");
#if !defined(INKTERFACE_LCD5B)
    pinMode(EPD_EN, OUTPUT);
    digitalWrite(EPD_EN, HIGH);
#endif
    STATE.reset();
    beginDisplay();
    MF_DISPLAY.clearBuffer();
    MF_DISPLAY.fillScreen(BG_COLOR);
#if defined(GABEN_STARTUP)
    MF_DISPLAY.drawXBitmap(0, 0, GABEN_BITS, GABEN_WIDTH, GABEN_HEIGHT, FG_COLOR);
#else
    drawStatic();
#endif
    MF_DISPLAY.display();
    DISP_DEBOUNCE = 10;

    Debug.println("starting ble advert");
    BLE_NAME = makeBleName();
    BLEAdvertising *advert = NimBLEDevice::getAdvertising();
    // A BLE advertisement payload is capped at 31 bytes. Flags + the name +
    // our manufacturer data (the interface version the app uses to decide the
    // panel is "supported") come to ~26 bytes and fit. Adding the 128-bit
    // service UUID (18 bytes) overflows the packet, and on newer BlueZ stacks
    // (e.g. SteamOS 3.8.x) that overflow drops the manufacturer data, so the
    // app never sees the version and marks the panel UNSUPPORTED. Keep the
    // name + version in the primary advertisement and move the UUID into the
    // scan response, which has its own 31-byte budget.
    BLEAdvertisementData ad_data{};
    ad_data.setName(BLE_NAME);
    ad_data.setManufacturerData("\x5d\x05" INTERFACE_VERSION);
    advert->setAdvertisementData(ad_data);

    BLEAdvertisementData scan_data{};
    scan_data.addServiceUUID(SERVICE_UUID);
    advert->setScanResponseData(scan_data);
    advert->enableScanResponse(true);
    NimBLEDevice::startAdvertising();

#if defined(GABEN_STARTUP)
    // just delaying here so folks can look at gabe for a bit
    Debug.println("observing gabe");
    delay(2000);
#endif
} // }}}

void loop()
{ // {{{
#if defined(INKTERFACE_LCD5B) && defined(LCD5B_ANIMATION_TEST)
    runLcdAnimationTest();
    return;
#endif

    static unsigned long LAST_MS = 0;
    static unsigned long CONN_DEBOUNCE = 5000;
    static unsigned long BATT_DEBOUNCE = 1000;
    static float last_battp = 0;
    static float last_battv = 0;

    auto now = millis();
    auto delta = now - LAST_MS;
    if (now < LAST_MS) {
        // handling rollover
        Debug.println("handling time rollover");
        delta = (std::numeric_limits<unsigned long>::max() - LAST_MS) + now;
    }

    if (CONN_DEBOUNCE > 0 && CONN_DEBOUNCE > delta) {
        CONN_DEBOUNCE -= delta;
    } else if (CONN_DEBOUNCE > 0) {
        // NOTE: this should be handled in our SERVER_CALLBACKS but we don't
        //       always seem to get the onDisconnect() callback and get stuck
        //       with advertising stopped, so might as well just check here...
        bool advertising = NimBLEDevice::getAdvertising()->isAdvertising();
        uint8_t connections = BLE_SERVER->getConnectedCount();
        if (!advertising && connections == 0) {
            Debug.print("starting advertisement, we have no connections");
            NimBLEDevice::startAdvertising();
        } else if (advertising && connections > 0) {
            Debug.print("stopping advertisement, we have connections");
            NimBLEDevice::stopAdvertising();
        }
        CONN_DEBOUNCE = 5000;
    }

    if (MAXLIPO_PRESENT && BATT_DEBOUNCE > 0 && BATT_DEBOUNCE > delta) {
        BATT_DEBOUNCE -= delta;
    } else if (MAXLIPO_PRESENT && BATT_DEBOUNCE > 0) {
        float battp = maxlipo.cellPercent();
        float battv = maxlipo.cellVoltage();
        if (abs(battp - last_battp) > 1 || abs(battv - last_battv) > 0.01) {
            last_battp = battp;
            last_battv = battv;
            Debug.print("batt: ");
            Debug.print(battp, 1);
            Debug.print("% total, ");
            Debug.print(maxlipo.chargeRate(), 1);
            Debug.print("% rate, ");
            Debug.print(battv, 2);
            Debug.println(" V");
            std::stringstream line;
            line << std::fixed << std::setprecision(2) << battv << " V";
            STATE.battLine = line.str();
            DISP_DEBOUNCE = 10;
        }
        BATT_DEBOUNCE = 1000;
    }

    // without a fuel gauge last_battv stays 0 forever, which must not be
    // mistaken for a drained battery
    if (MAXLIPO_PRESENT && last_battv < BATT_MINV) {
        Debug.println("drawing low battery message");
        DISP_DEBOUNCE = 0;
        beginDisplay();
        MF_DISPLAY.clearBuffer();
        MF_DISPLAY.fillScreen(BG_COLOR);
        drawLowBatt();
        MF_DISPLAY.display();
        MF_DISPLAY.powerDown();
        Debug.println("going into deep sleep");
        // esp_sleep_enable_timer_wakeup(10 * 1000000ULL);
        esp_deep_sleep_start();
        goto loop_end;
    }

    if (DISP_DEBOUNCE > 0 && DISP_DEBOUNCE > delta) {
        DISP_DEBOUNCE -= delta;
    } else if (DISP_DEBOUNCE > 0) {
        Debug.println("drawing to display");
        DISP_DEBOUNCE = 0;
#if defined(INKTERFACE_LCD5B)
        const uint32_t updateStartUs = micros();
        uint32_t compositionUs = 0;
#endif
        beginDisplay();
        bool jpegPresented = false;
#if defined(INKTERFACE_LCD5B)
        if (STATE.artMode && STATE.artJpegMode) {
            jpegPresented = MF_DISPLAY.displayJpeg(ART_JPEG_BUFFER, STATE.artJpegSize);
            if (!jpegPresented) {
                Debug.println("failed to decode or present color artwork; showing telemetry");
                STATE.artMode = false;
                STATE.artJpegMode = false;
            }
        }
#endif
        if (!jpegPresented) {
            MF_DISPLAY.clearBuffer();
            MF_DISPLAY.fillScreen(BG_COLOR);
            if (STATE.artMode && !STATE.artJpegMode) {
                drawArt();
            } else {
                drawStatic();
            }
#if defined(INKTERFACE_LCD5B)
            compositionUs = micros() - updateStartUs;
#endif
            MF_DISPLAY.display();
        }
#if defined(INKTERFACE_LCD5B)
        const uint32_t totalUs = micros() - updateStartUs;
        if (jpegPresented) {
            const uint32_t renderUs = MF_DISPLAY.lastRasterUs();
            const uint32_t waitUs = MF_DISPLAY.lastPresentWaitUs();
            compositionUs = totalUs > renderUs + waitUs ? totalUs - renderUs - waitUs : 0;
        }
        Serial.printf("LCD update: compose=%lu raster=%lu present-wait=%lu total=%lu us\n",
                      static_cast<unsigned long>(compositionUs),
                      static_cast<unsigned long>(MF_DISPLAY.lastRasterUs()),
                      static_cast<unsigned long>(MF_DISPLAY.lastPresentWaitUs()),
                      static_cast<unsigned long>(totalUs));
#endif
        MF_DISPLAY.powerDown();
        Debug.println("drew to display");
    }

    // Debug.println("entering light sleep");
    // esp_sleep_enable_timer_wakeup(10 * 1000ULL);
    // esp_err_t err = esp_light_sleep_start();
    // Debug.println("woke from light sleep");
    // Debug.print("After sleep, err=%s\n", esp_err_to_name(err));
    // Debug.print("Wake reason=%d\n", esp_sleep_get_wakeup_cause());
    // delay(1);

loop_end:
    LAST_MS = now;
    delay(10);
} // }}}

void drawText(const char *text, const int16_t &x = -1, const int16_t &y = -1,
              const uint8_t &size = 1, const bool &wrap = false,
              const uint16_t &color = FG_COLOR)
{ // {{{
    if (x >= 0 && y >= 0) {
        MF_DISPLAY.setCursor(x, y);
    }
    MF_DISPLAY.setTextSize(size);
    MF_DISPLAY.setTextColor(color);
    MF_DISPLAY.setTextWrap(wrap);
    MF_DISPLAY.print(text);
} // }}}

void drawLogo(int16_t &x, const int16_t &y = 0)
{ // {{{
#if defined(INKTERFACE_LCD5B)
    MF_DISPLAY.fillRoundRect(x, y, 116, 116, 4, FG_COLOR);
    MF_DISPLAY.fillCircle(x + 58, y + 58, 36, BG_COLOR);
    MF_DISPLAY.fillCircle(x + 58, y + 58, 27, LCD5B_ACCENT_COLOR);
    x += 116;
#else
    MF_DISPLAY.fillRoundRect(x, y, 101, 101, 3, FG_COLOR);
    MF_DISPLAY.fillCircle(x + 50, y + 50, 31, BG_COLOR);
    MF_DISPLAY.fillCircle(x + 50, y + 50, 23, FG_COLOR);
    x += 101;
#endif
} // }}}

void drawSparkbox(int16_t &x, const int16_t &y, std::string &title, const std::string &value,
                  const Points &points)
{ // {{{
    const int16_t w = SPARKBOX_WIDTH;
    const int16_t h = SPARKBOX_HEIGHT;
#if defined(INKTERFACE_LCD5B)
    const int16_t hpad = 12;
    const int16_t vpad = 8;
    const int16_t title_h = 38;
    const int16_t graph_h = (h - title_h) - 48;
    const int16_t graph_w = w - 30;
    const int16_t graph_x = x + 15;
    const int16_t graph_y = (y + h) - 24;
    const uint8_t labelSize = 2;
    const uint8_t titleSize = 3;
    const uint16_t graphColor = LCD5B_ACCENT_COLOR;
#else
    const int16_t hpad = 8;
    const int16_t vpad = 6;
    const int16_t title_h = 26;
    const int16_t graph_h = (h - title_h) - 32;
    const int16_t graph_w = w - 20;
    const int16_t graph_x = x + 10;
    const int16_t graph_y = (y + h) - 16;
    const uint8_t labelSize = 1;
    const uint8_t titleSize = 2;
    const uint16_t graphColor = FG_COLOR;
#endif

    if (!title.empty()) {
        MF_DISPLAY.drawRoundRect(x, y, w, h, 4, FG_COLOR);
        MF_DISPLAY.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 4, FG_COLOR);
        MF_DISPLAY.fillRect(x, y + title_h, w, 1, FG_COLOR);
        drawText(title.c_str(), x + hpad, y + vpad, titleSize);
        drawText(value.c_str(), (x + (w - hpad)) - (6 * titleSize * strlen(value.c_str())),
                 y + vpad, titleSize);

        std::stringstream maxstrm;
        maxstrm << std::fixed << std::setprecision(0) << points.yMax;
        auto maxstr = maxstrm.str();
#if defined(INKTERFACE_LCD5B)
        drawText(maxstr.c_str(), x + hpad, y + title_h + vpad, labelSize, false,
                 LCD5B_MUTED_COLOR);
#else
        drawText(maxstr.c_str(), x + hpad, y + title_h + vpad, labelSize);
#endif

        std::stringstream minstrm;
        minstrm << std::fixed << std::setprecision(0) << points.yMin;
        auto minstr = minstrm.str();
#if defined(INKTERFACE_LCD5B)
        drawText(minstr.c_str(), x + hpad, y + h - (vpad + 7), labelSize, false,
                 LCD5B_MUTED_COLOR);
#else
        drawText(minstr.c_str(), x + hpad, y + h - (vpad + 7), labelSize);
#endif

        if (points.points.size() >= 2) {
            int16_t s_x = 0.0, s_y = 0.0, e_x = 0.0, e_y = 0.0;
            for (auto p = points.points.cbegin(); p != points.points.cend() - 1; ++p) {
                s_x = graph_x + (p->x * graph_w);
                e_x = graph_x + ((p + 1)->x * graph_w);
                s_y = graph_y + (p->y * graph_h * -1.0);
                e_y = graph_y + ((p + 1)->y * graph_h * -1.0);
                MF_DISPLAY.drawLine(s_x, s_y, e_x, e_y, graphColor);
                MF_DISPLAY.drawLine(s_x, s_y - 1, e_x, e_y - 1, graphColor);
                MF_DISPLAY.drawLine(s_x, s_y + 1, e_x, e_y + 1, graphColor);
                MF_DISPLAY.drawLine(s_x - 1, s_y, e_x - 1, e_y, graphColor);
                MF_DISPLAY.drawLine(s_x + 1, s_y, e_x + 1, e_y, graphColor);
            }
        }
    }

    x += w;
} // }}}

void drawDiscreteBox(int16_t &x, const int16_t &y, const std::string &title,
                     const std::string &value)
{ // {{{
#if defined(INKTERFACE_LCD5B)
    const int16_t w = 324;
    const int16_t h = 42;
    const int16_t hpad = 12;
    const int16_t vpad = 10;
    const uint8_t textSize = 3;
#else
    const int16_t w = 209;
    const int16_t h = 26;
    const int16_t hpad = 8;
    const int16_t vpad = 6;
    const uint8_t textSize = 2;
#endif

    if (!title.empty()) {
        MF_DISPLAY.drawRoundRect(x, y, w, h, 4, FG_COLOR);
        MF_DISPLAY.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 4, FG_COLOR);
        drawText(title.c_str(), x + hpad, y + vpad, textSize);
        drawText(value.c_str(), (x + (w - hpad)) - (6 * textSize * strlen(value.c_str())),
                 y + vpad, textSize);
    }

    x += w;
} // }}}

void drawStatic()
{ // {{{
    int16_t x = 0;
    int16_t y = 0;

#if defined(INKTERFACE_LCD5B)
    x = 16;
    y = 12;
    drawLogo(x, y);

    x = 156;
    y = 18;
    drawText(STATE.topLine.c_str(), x, y, 4);
    y += 42;
    drawText(STATE.midLine.c_str(), x, y, 3);
    y += 30;
    drawText(STATE.botLine.c_str(), x, y, 2);

    x = 16;
    y = 126;
    drawDiscreteBox(x, y, STATE.keyvals[0].key, STATE.keyvals[0].val);
    x += 10;
    drawDiscreteBox(x, y, STATE.keyvals[1].key, STATE.keyvals[1].val);
    x += 10;
    drawDiscreteBox(x, y, STATE.keyvals[2].key, STATE.keyvals[2].val);

    x = 16;
    y = 180;
    drawSparkbox(x, y, STATE.keyvals[3].key, STATE.keyvals[3].val, STATE.sparks[0]);
    x += 8;
    drawSparkbox(x, y, STATE.keyvals[4].key, STATE.keyvals[4].val, STATE.sparks[1]);
    x += 8;
    drawSparkbox(x, y, STATE.keyvals[5].key, STATE.keyvals[5].val, STATE.sparks[2]);

    x = 16;
    y += SPARKBOX_HEIGHT + 8;
    drawSparkbox(x, y, STATE.keyvals[6].key, STATE.keyvals[6].val, STATE.sparks[3]);
    x += 8;
    drawSparkbox(x, y, STATE.keyvals[7].key, STATE.keyvals[7].val, STATE.sparks[4]);
    x += 8;
    drawSparkbox(x, y, STATE.keyvals[8].key, STATE.keyvals[8].val, STATE.sparks[5]);

    x = 16;
    y = MF_DISPLAY.height() - 34;
    drawText(STATE.battLine.c_str(), x, y, 1);

    std::stringstream tag;
    tag << BLE_NAME << " " << GIT_REVISION << " " << INTERFACE_VERSION;
    x = 16;
    y = MF_DISPLAY.height() - 13;
    drawText(tag.str().c_str(), x, y, 1);

    x = MF_DISPLAY.width() - (6 * strlen(STATE.hostMsg.c_str())) - 16;
    drawText(STATE.hostMsg.c_str(), x, y, 1);
#else
    // fremont logo in top left corner
    x = 5;
    y = 5;
    drawLogo(x, y);

    // show connected fremont hostname/serial or connecting status
    x = 120;
    y = 15;
    drawText(STATE.topLine.c_str(), x, y, 3);
    y += 35;
    drawText(STATE.midLine.c_str(), x, y, 2);
    y += 30;
    drawText(STATE.botLine.c_str(), x, y, 2);

    // first row of boxes with no sparklines
    x = 5;
    y = 115;
    drawDiscreteBox(x, y, STATE.keyvals[0].key, STATE.keyvals[0].val);
    x += 5;
    drawDiscreteBox(x, y, STATE.keyvals[1].key, STATE.keyvals[1].val);
    x += 5;
    drawDiscreteBox(x, y, STATE.keyvals[2].key, STATE.keyvals[2].val);

    // second row
    x = 5;
    y += 26 + 5;
    drawSparkbox(x, y, STATE.keyvals[3].key, STATE.keyvals[3].val, STATE.sparks[0]);
    x += 5;
    drawSparkbox(x, y, STATE.keyvals[4].key, STATE.keyvals[4].val, STATE.sparks[1]);
    x += 5;
    drawSparkbox(x, y, STATE.keyvals[5].key, STATE.keyvals[5].val, STATE.sparks[2]);

    // third row
    x = 5;
    y += SPARKBOX_HEIGHT + 5;
    drawSparkbox(x, y, STATE.keyvals[6].key, STATE.keyvals[6].val, STATE.sparks[3]);
    x += 5;
    drawSparkbox(x, y, STATE.keyvals[7].key, STATE.keyvals[7].val, STATE.sparks[4]);
    x += 5;
    drawSparkbox(x, y, STATE.keyvals[8].key, STATE.keyvals[8].val, STATE.sparks[5]);

    // battery state
    x = 5;
    y = MF_DISPLAY.height() - 24;
    drawText(STATE.battLine.c_str(), x, y);

    // version tag
    std::stringstream tag;
    tag << BLE_NAME << " " << GIT_REVISION << " " << INTERFACE_VERSION;
    x = 5;
    y = MF_DISPLAY.height() - 12;
    drawText(tag.str().c_str(), x, y);

    // host message if provided (usually a timestamp)
    x = MF_DISPLAY.width() - (6 * strlen(STATE.hostMsg.c_str())) - 5;
    drawText(STATE.hostMsg.c_str(), x, y);
#endif
} // }}}

void drawArt()
{ // {{{
    if (ART_BUFFER == nullptr) {
        return;
    }
    // center the frame in case the host sent something smaller than the panel
    int16_t x = (MF_DISPLAY.width() - STATE.artWidth) / 2;
    int16_t y = (MF_DISPLAY.height() - STATE.artHeight) / 2;
    MF_DISPLAY.drawBitmap(x, y, ART_BUFFER, STATE.artWidth, STATE.artHeight, FG_COLOR);

    // keep the firmware version visible even in artwork mode; draw it on a
    // small solid swatch so it stays readable over dark box art
    std::stringstream tag;
    tag << GIT_REVISION << " " << INTERFACE_VERSION;
    std::string tagstr = tag.str();
    int16_t tw = 6 * (int16_t)tagstr.length();
    int16_t tx = 4;
    int16_t ty = MF_DISPLAY.height() - 12;
    MF_DISPLAY.fillRect(tx - 2, ty - 2, tw + 4, 11, BG_COLOR);
    drawText(tagstr.c_str(), tx, ty);
} // }}}

void drawLowBatt()
{ // {{{
    int16_t x = 0;
    int16_t y = 0;

    x = 5;
    y = 15;
    drawText("Please charge the battery!", x, y, 3);
    y += 35;
    drawText("Once charged, press reset on the back!", x, y, 2);

    // battery state
    x = 5;
    y = MF_DISPLAY.height() - 24;
    drawText(STATE.battLine.c_str(), x, y);

    // version tag
    std::stringstream tag;
    tag << BLE_NAME << " " << GIT_REVISION << " " << INTERFACE_VERSION;
    x = 5;
    y = MF_DISPLAY.height() - 12;
    drawText(tag.str().c_str(), x, y);

    // host message if provided (usually a timestamp)
    x = MF_DISPLAY.width() - (6 * strlen(STATE.hostMsg.c_str())) - 5;
    drawText(STATE.hostMsg.c_str(), x, y);
} // }}}
