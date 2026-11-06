#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <driver/i2c_master.h>
#include <soc/gpio_num.h>

/**
 * @brief Text driver for a 0.42" 72x40 SSD1306 OLED on its own I2C bus.
 *
 * Fixed 6x8 cells give a 12 column by 5 row text grid. Draw into the frame
 * with text(), then send it with flush().
 */
class Oled72x40 {
public:
    static constexpr size_t kCols = 12;
    static constexpr size_t kRows = 5;

    Oled72x40(gpio_num_t sda, gpio_num_t scl);
    ~Oled72x40();
    Oled72x40(const Oled72x40&) = delete;
    Oled72x40& operator=(const Oled72x40&) = delete;

    /**
     * @brief Opens the bus and initializes the panel.
     * @return false when no display answers on the bus.
     */
    bool begin();

    void clear();

    /** @brief Writes a line to a text row, anything past kCols is cut off. */
    void text(size_t row, const char* str);

    bool flush();

private:
    static constexpr size_t WIDTH = 72;
    static constexpr size_t PAGES = 5;

    bool sendCommands(const uint8_t* cmds, size_t len);

    gpio_num_t m_sda;
    gpio_num_t m_scl;
    i2c_master_bus_handle_t m_bus = nullptr;
    i2c_master_dev_handle_t m_dev = nullptr;

    // Control byte followed by the frame, so it goes out in one transfer.
    std::array<uint8_t, 1 + WIDTH * PAGES> m_frame{};

    static constexpr const char* TAG = "Oled72x40";
};
