#pragma once

#include <Adafruit_GFX.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <jpeg_decoder.h>

// Waveshare ESP32-S3-LCD-5B / ESP32-S3-Touch-LCD-5B display configuration.
// The board is an ESP32-S3-WROOM-1-N16R8 with an 1024x600 ST7262 RGB panel.
static constexpr int16_t LCD5B_WIDTH = 1024;
static constexpr int16_t LCD5B_HEIGHT = 600;
static constexpr uint16_t LCD5B_ACCENT_COLOR = 0x2D7F;
static constexpr uint16_t LCD5B_MUTED_COLOR = 0x8410;

// The LCD uses a four-color status canvas plus native-resolution JPEG artwork.
// Frames are rendered into the inactive RGB565 framebuffer and selected at a
// complete RGB frame boundary before the old framebuffer is reused.
class LCD5BDisplay : public Adafruit_GFX
{
  public:
    explicit LCD5BDisplay(uint16_t blackColor)
        : Adafruit_GFX(LCD5B_WIDTH, LCD5B_HEIGHT)
        , _blackColor(blackColor)
        , _palette{0xFFFF, blackColor, LCD5B_ACCENT_COLOR, LCD5B_MUTED_COLOR}
    {
    }

    bool begin(uint8_t = 0)
    {
        if (_initialized) {
            return true;
        }

        // The CH422G uses command addresses rather than register-index writes.
        // EXIO1/3 release touch/LCD reset; EXIO4 keeps the SD card deselected.
        if (!writeExpander(0x24, 0x01) || !setExpanderOutputs((1 << 1) | (1 << 3) | (1 << 4))) {
            return false;
        }
        delay(10);
        if (!setExpanderOutputs((1 << 1) | (1 << 4))) {
            return false;
        }
        delay(10);
        if (!setExpanderOutputs((1 << 1) | (1 << 3) | (1 << 4))) {
            return false;
        }
        delay(100);

        if (_panel == nullptr && !createPanel()) {
            return false;
        }
        if (!_callbacksRegistered) {
            esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
            callbacks.on_frame_buf_complete = onFrameBufferComplete;
            const esp_err_t error = esp_lcd_rgb_panel_register_event_callbacks(_panel, &callbacks, this);
            if (error != ESP_OK) {
                Serial.printf("LCD-5B callback registration failed: %s\n", esp_err_to_name(error));
                return false;
            }
            _callbacksRegistered = true;
        }
        if (!_panelInitialized) {
            esp_err_t error = esp_lcd_panel_reset(_panel);
            if (error == ESP_OK) {
                error = esp_lcd_panel_init(_panel);
            }
            if (error != ESP_OK) {
                Serial.printf("LCD-5B panel initialization failed: %s\n", esp_err_to_name(error));
                return false;
            }
            _panelInitialized = true;
        }

        if (_framebuffers[0] == nullptr || _framebuffers[1] == nullptr) {
            void *first = nullptr;
            void *second = nullptr;
            const esp_err_t error = esp_lcd_rgb_panel_get_frame_buffer(_panel, 2, &first, &second);
            if (error != ESP_OK || first == nullptr || second == nullptr) {
                Serial.printf("LCD-5B could not get double framebuffers: %s\n",
                              esp_err_to_name(error));
                return false;
            }
            _framebuffers[0] = static_cast<uint16_t *>(first);
            _framebuffers[1] = static_cast<uint16_t *>(second);
        }

        // Four indexed colors give the status layout a small accent palette
        // without requiring a second full-size RGB565 canvas. Color artwork is
        // JPEG-decoded directly into the inactive RGB framebuffer.
        if (_canvas == nullptr) {
            _canvas = static_cast<uint8_t *>(
                heap_caps_malloc(CANVAS_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            _canvasInInternalRam = _canvas != nullptr;
            if (_canvas == nullptr) {
                _canvas = static_cast<uint8_t *>(
                    heap_caps_malloc(CANVAS_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            }
        }
        if (_canvas == nullptr) {
            Serial.println("LCD-5B could not allocate its drawing buffer");
            return false;
        }
        Serial.printf("LCD-5B drawing buffer: %s, %u bytes; internal heap free: %u bytes\n",
                      _canvasInInternalRam ? "internal SRAM" : "PSRAM",
                      static_cast<unsigned>(CANVAS_BUFFER_SIZE),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));

        memset(_framebuffers[0], 0xFF, RGB_BUFFER_SIZE);
        memset(_framebuffers[1], 0xFF, RGB_BUFFER_SIZE);
        prepareColorMaps();
        clearBuffer();
        _initialized = true;
        return true;
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override
    {
        if (_canvas == nullptr || x < 0 || y < 0 || x >= LCD5B_WIDTH || y >= LCD5B_HEIGHT) {
            return;
        }
        setPixel(x, y, colorIndex(color));
    }

    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override
    {
        if (_canvas == nullptr || y < 0 || y >= LCD5B_HEIGHT || w <= 0) {
            return;
        }
        int32_t start = x;
        int32_t end = start + w;
        if (start < 0) {
            start = 0;
        }
        if (end > LCD5B_WIDTH) {
            end = LCD5B_WIDTH;
        }
        const uint8_t index = colorIndex(color);
        for (int32_t px = start; px < end; ++px) {
            setPixel(static_cast<int16_t>(px), y, index);
        }
    }

    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override
    {
        if (_canvas == nullptr || x < 0 || x >= LCD5B_WIDTH || h <= 0) {
            return;
        }
        int32_t start = y;
        int32_t end = start + h;
        if (start < 0) {
            start = 0;
        }
        if (end > LCD5B_HEIGHT) {
            end = LCD5B_HEIGHT;
        }
        const uint8_t index = colorIndex(color);
        for (int32_t py = start; py < end; ++py) {
            setPixel(x, static_cast<int16_t>(py), index);
        }
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
    {
        if (_canvas == nullptr || w <= 0 || h <= 0) {
            return;
        }
        int32_t startY = y;
        int32_t endY = startY + h;
        if (startY < 0) {
            startY = 0;
        }
        if (endY > LCD5B_HEIGHT) {
            endY = LCD5B_HEIGHT;
        }
        for (int32_t py = startY; py < endY; ++py) {
            drawFastHLine(x, static_cast<int16_t>(py), w, color);
        }
    }

    void fillScreen(uint16_t color)
    {
        if (_canvas != nullptr) {
            const uint8_t index = colorIndex(color);
            const uint8_t pattern = static_cast<uint8_t>(index * 0x55);
            memset(_canvas, pattern, CANVAS_BUFFER_SIZE);
        }
    }

    void clearBuffer()
    {
        if (_canvas != nullptr) {
            memset(_canvas, 0x00, CANVAS_BUFFER_SIZE);
        }
    }

    void display()
    {
        if (!_initialized || _canvas == nullptr || _panel == nullptr) {
            return;
        }

        const uint32_t rasterStartUs = micros();
        const uint8_t backIndex = 1 - _frontIndex;
        uint16_t *backBuffer = _framebuffers[backIndex];
        // Expand four palette indices at a time into the native 1024x600
        // RGB565 framebuffer. No letterboxing or resolution conversion remains.
        for (int16_t y = 0; y < LCD5B_HEIGHT; ++y) {
            uint32_t *destination = reinterpret_cast<uint32_t *>(
                backBuffer + static_cast<size_t>(y) * LCD5B_WIDTH);
            const uint8_t *sourceRow = _canvas + static_cast<size_t>(y) * CANVAS_ROW_BYTES;
            for (size_t byteX = 0; byteX < CANVAS_ROW_BYTES; ++byteX) {
                const uint32_t *lookup = _expandedByteLut[sourceRow[byteX]];
                destination[0] = lookup[0];
                destination[1] = lookup[1];
                destination += 2;
            }
        }
        _lastRasterUs = micros() - rasterStartUs;
        presentFrame(backIndex);
    }

    bool displayJpeg(const uint8_t *jpeg, size_t jpegSize)
    {
        if (!_initialized || jpeg == nullptr || jpegSize == 0 || _panel == nullptr) {
            return false;
        }

        const uint8_t backIndex = 1 - _frontIndex;
        uint16_t *backBuffer = _framebuffers[backIndex];
        esp_jpeg_image_cfg_t config = {};
        config.indata = const_cast<uint8_t *>(jpeg);
        config.indata_size = static_cast<uint32_t>(jpegSize);
        config.outbuf = reinterpret_cast<uint8_t *>(backBuffer);
        config.outbuf_size = RGB_BUFFER_SIZE;
        config.out_format = JPEG_IMAGE_FORMAT_RGB565;
        config.out_scale = JPEG_IMAGE_SCALE_0;
        config.flags.swap_color_bytes = 1;

        esp_jpeg_image_output_t imageInfo = {};
        if (esp_jpeg_get_image_info(&config, &imageInfo) != ESP_OK ||
            imageInfo.width != LCD5B_WIDTH || imageInfo.height != LCD5B_HEIGHT) {
            Serial.printf("LCD-5B rejected JPEG artwork dimensions %u x %u\n",
                          static_cast<unsigned>(imageInfo.width),
                          static_cast<unsigned>(imageInfo.height));
            return false;
        }

        config.priv.read = 0;
        const uint32_t decodeStartUs = micros();
        const esp_err_t error = esp_jpeg_decode(&config, &imageInfo);
        _lastRasterUs = micros() - decodeStartUs;
        if (error != ESP_OK || imageInfo.output_len != RGB_BUFFER_SIZE) {
            Serial.printf("LCD-5B JPEG decode failed: %s\n", esp_err_to_name(error));
            return false;
        }
        return presentFrame(backIndex);
    }

    uint32_t lastRasterUs() const { return _lastRasterUs; }
    uint32_t lastPresentWaitUs() const { return _lastPresentWaitUs; }

    void powerDown() {}

  private:
    bool presentFrame(uint8_t backIndex)
    {
        // The driver keeps scanning the current framebuffer while the CPU
        // prepares the other one; only switch after a complete scan boundary.
        ulTaskNotifyTake(pdTRUE, 0);
        const esp_err_t error = esp_lcd_panel_draw_bitmap(
            _panel, 0, 0, LCD5B_WIDTH, LCD5B_HEIGHT, _framebuffers[backIndex]);
        if (error != ESP_OK) {
            Serial.printf("LCD-5B framebuffer swap failed: %s\n", esp_err_to_name(error));
            return false;
        }
        // Register only after requesting the buffer change. A frame-complete
        // interrupt between the stale-notification drain and the request still
        // belongs to the old scan and must not release the buffer we're about
        // to reuse. If an interrupt lands in this small gap, the next frame
        // completion will wake us instead, which is safe (and costs one frame).
        _frameWaiter = xTaskGetCurrentTaskHandle();
        const uint32_t swapWaitStartUs = micros();
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        _lastPresentWaitUs = micros() - swapWaitStartUs;
        _frameWaiter = nullptr;
        _frontIndex = backIndex;

        // EXIO2 is the LCD backlight enable. Turn it on only after the first
        // complete frame has reached a framebuffer.
        if (!_backlightOn) {
            _backlightOn = setExpanderOutputs(_outputMask | (1 << 2));
        }
        return true;
    }

    static constexpr size_t CANVAS_ROW_BYTES = LCD5B_WIDTH / 4;
    static constexpr size_t CANVAS_BUFFER_SIZE = CANVAS_ROW_BYTES * LCD5B_HEIGHT;
    static constexpr size_t RGB_PIXEL_COUNT = LCD5B_WIDTH * LCD5B_HEIGHT;
    static constexpr size_t RGB_BUFFER_SIZE = RGB_PIXEL_COUNT * sizeof(uint16_t);
    static_assert(LCD5B_WIDTH % 4 == 0, "LCD-5B canvas width must be divisible by four");

    static bool IRAM_ATTR onFrameBufferComplete(esp_lcd_panel_handle_t,
                                                 const esp_lcd_rgb_panel_event_data_t *,
                                                 void *userContext)
    {
        LCD5BDisplay *self = static_cast<LCD5BDisplay *>(userContext);
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        if (self->_frameWaiter != nullptr) {
            vTaskNotifyGiveFromISR(self->_frameWaiter, &higherPriorityTaskWoken);
        }
        return higherPriorityTaskWoken == pdTRUE;
    }

    bool createPanel()
    {
        esp_lcd_rgb_panel_config_t config = {};
        config.clk_src = LCD_CLK_SRC_DEFAULT;
        config.timings.pclk_hz = 21000000;
        config.timings.h_res = LCD5B_WIDTH;
        config.timings.v_res = LCD5B_HEIGHT;
        config.timings.hsync_pulse_width = 24;
        config.timings.hsync_back_porch = 160;
        config.timings.hsync_front_porch = 160;
        config.timings.vsync_pulse_width = 2;
        config.timings.vsync_back_porch = 23;
        config.timings.vsync_front_porch = 12;
        config.timings.flags.hsync_idle_low = 1;
        config.timings.flags.vsync_idle_low = 1;
        config.timings.flags.pclk_active_neg = 1;
        config.timings.flags.de_idle_high = 0;
        config.timings.flags.pclk_idle_high = 0;
        config.data_width = 16;
        config.bits_per_pixel = 16;
        config.num_fbs = 2;
        // Copy each PSRAM-backed framebuffer into two internal SRAM DMA
        // buffers to absorb brief PSRAM/flash contention during RGB scanout.
        config.bounce_buffer_size_px = 10 * LCD5B_WIDTH;
        config.hsync_gpio_num = 46;
        config.vsync_gpio_num = 3;
        config.de_gpio_num = 5;
        config.pclk_gpio_num = 7;
        config.disp_gpio_num = GPIO_NUM_NC;
        const int dataPins[16] = {14, 38, 18, 17, 10, 39, 0, 45,
                                  48, 47, 21, 1, 2, 42, 41, 40};
        memcpy(config.data_gpio_nums, dataPins, sizeof(dataPins));
        config.flags.fb_in_psram = 1;

        const esp_err_t error = esp_lcd_new_rgb_panel(&config, &_panel);
        if (error != ESP_OK) {
            Serial.printf("LCD-5B panel creation failed: %s\n", esp_err_to_name(error));
            _panel = nullptr;
            return false;
        }
        return true;
    }

    void prepareColorMaps()
    {
        // Each source byte contains four palette indices. Pre-expand all 256
        // possible byte patterns into four RGB565 pixels for fast row copies.
        uint16_t expanded[4];
        for (uint16_t pattern = 0; pattern < 256; ++pattern) {
            for (size_t pixel = 0; pixel < 4; ++pixel) {
                const uint8_t index = (pattern >> (6 - pixel * 2)) & 0x03;
                expanded[pixel] = _palette[index];
            }
            memcpy(_expandedByteLut[pattern], expanded, sizeof(expanded));
        }
    }

    uint8_t colorIndex(uint16_t color) const
    {
        if (color == _blackColor) {
            return 1;
        }
        if (color == LCD5B_ACCENT_COLOR) {
            return 2;
        }
        if (color == LCD5B_MUTED_COLOR) {
            return 3;
        }
        return 0;
    }

    void setPixel(int16_t x, int16_t y, uint8_t index)
    {
        const size_t offset = static_cast<size_t>(y) * CANVAS_ROW_BYTES + x / 4;
        const uint8_t shift = static_cast<uint8_t>((3 - (x & 3)) * 2);
        const uint8_t mask = static_cast<uint8_t>(0x03 << shift);
        _canvas[offset] = static_cast<uint8_t>((_canvas[offset] & ~mask) | (index << shift));
    }

    static bool writeExpander(uint8_t command, uint8_t value)
    {
        Wire.beginTransmission(command);
        Wire.write(value);
        return Wire.endTransmission() == 0;
    }

    bool setExpanderOutputs(uint8_t mask)
    {
        if (!writeExpander(0x38, mask)) {
            return false;
        }
        _outputMask = mask;
        return true;
    }

    uint16_t _blackColor;
    uint16_t _palette[4];
    uint8_t *_canvas = nullptr;
    uint16_t *_framebuffers[2] = {nullptr, nullptr};
    alignas(uint32_t) uint32_t _expandedByteLut[256][2] = {};
    esp_lcd_panel_handle_t _panel = nullptr;
    uint8_t _frontIndex = 0;
    uint8_t _outputMask = 0;
    TaskHandle_t _frameWaiter = nullptr;
    bool _backlightOn = false;
    bool _canvasInInternalRam = false;
    bool _callbacksRegistered = false;
    bool _panelInitialized = false;
    bool _initialized = false;
    uint32_t _lastRasterUs = 0;
    uint32_t _lastPresentWaitUs = 0;
};
