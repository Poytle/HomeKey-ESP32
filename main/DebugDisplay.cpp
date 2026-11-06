#include "DebugDisplay.hpp"
#include "LockManager.hpp"
#include "MqttManager.hpp"
#include "NfcManager.hpp"
#include "config.hpp"
#include "eventStructs.hpp"
#include "ETH.h"
#include "WiFi.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "fmt/format.h"
#include "fmt/ranges.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr uint32_t PAGE_SECONDS = 4;

const char* lockStateName(int state) {
    switch (state) {
        case LockManager::UNLOCKED:  return "UNLOCKED";
        case LockManager::LOCKED:    return "LOCKED";
        case LockManager::JAMMED:    return "JAMMED";
        case LockManager::UNLOCKING: return "UNLOCKING";
        case LockManager::LOCKING:   return "LOCKING";
        default:                     return "UNKNOWN";
    }
}
}

DebugDisplay::DebugDisplay(const LockManager& lockManager, const NfcManager& nfcManager,
                           const MqttManager& mqttManager, bool ethernetEnabled)
    : m_lockManager(lockManager),
      m_nfcManager(nfcManager),
      m_mqttManager(mqttManager),
      m_ethernetEnabled(ethernetEnabled) {}

bool DebugDisplay::begin() {
    if (DEBUG_OLED_SDA_PIN == PIN_UNSET || DEBUG_OLED_SCL_PIN == PIN_UNSET) {
        return false;
    }
    auto sda = GPIOAllocator::instance().acquire(gpio_num_t(DEBUG_OLED_SDA_PIN), GPIO_MODE_DISABLE, GPIOAllocator::PinRole::I2cSda, GPIOAllocator::PinConsumer::Hardware, "OLED_SDA");
    auto scl = GPIOAllocator::instance().acquire(gpio_num_t(DEBUG_OLED_SCL_PIN), GPIO_MODE_DISABLE, GPIOAllocator::PinRole::I2cScl, GPIOAllocator::PinConsumer::Hardware, "OLED_SCL");
    if (!sda || !scl) {
        ESP_LOGW(TAG, "Could not acquire the OLED pins, display disabled.");
        return false;
    }

    m_oled = std::make_unique<Oled72x40>(gpio_num_t(DEBUG_OLED_SDA_PIN), gpio_num_t(DEBUG_OLED_SCL_PIN));
    if (!m_oled->begin()) {
        ESP_LOGI(TAG, "No OLED found, display disabled.");
        m_oled.reset();
        return false;
    }
    m_sdaLease = std::move(*sda);
    m_sclLease = std::move(*scl);

    m_nfcEvent = AppEventLoop::subscribe(NFC_EVENT, NFC_TAP_EVENT, [this](const uint8_t* data, size_t size) {
        if (size == 0 || data == nullptr) return;
        std::span<const uint8_t> payload(data, size);
        std::error_code ec;
        NfcEvent nfc_event = alpaca::deserialize<NfcEvent>(payload, ec);
        if (ec) return;
        std::string tap;
        if (nfc_event.type == HOMEKEY_TAP) {
            EventHKTap s = alpaca::deserialize<EventHKTap>(nfc_event.data, ec);
            if (ec) return;
            tap = s.status ? "HK OK" : "HK FAIL";
        } else if (nfc_event.type == TAG_TAP) {
            EventTagTap s = alpaca::deserialize<EventTagTap>(nfc_event.data, ec);
            if (ec) return;
            tap = fmt::format("{:02X}", fmt::join(s.uid, ""));
        } else {
            return;
        }
        std::scoped_lock lock(m_lastTapMutex);
        m_lastTap = std::move(tap);
    });

    if (xTaskCreate(taskEntry, "debug_display", 4096, this, 1, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task.");
        return false;
    }
    return true;
}

void DebugDisplay::taskEntry(void* instance) {
    static_cast<DebugDisplay*>(instance)->task();
}

void DebugDisplay::task() {
    uint32_t seconds = 0;
    while (true) {
        m_oled->clear();
        if ((seconds / PAGE_SECONDS) % 2 == 0) {
            drawStatusPage();
        } else {
            drawNetworkPage();
        }
        if (!m_oled->flush()) {
            ESP_LOGD(TAG, "Failed to update the display.");
        }
        ++seconds;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void DebugDisplay::drawStatusPage() {
    std::string lastTap;
    {
        std::scoped_lock lock(m_lastTapMutex);
        lastTap = m_lastTap;
    }
    const uint32_t uptime = static_cast<uint32_t>(esp_timer_get_time() / 1000000);

    m_oled->text(0, lockStateName(m_lockManager.getCurrentState()));
    m_oled->text(1, fmt::format("NFC:{} MQ:{}", m_nfcManager.isConnected() ? "OK" : "NO",
                                m_mqttManager.isConnected() ? "OK" : "NO").c_str());
    m_oled->text(2, fmt::format("Tap:{}", lastTap).c_str());
    m_oled->text(3, fmt::format("Heap:{}K", esp_get_free_heap_size() / 1024).c_str());
    m_oled->text(4, fmt::format("Up {}:{:02}:{:02}", uptime / 3600, uptime / 60 % 60, uptime % 60).c_str());
}

void DebugDisplay::drawNetworkPage() {
    IPAddress ip;
    std::string ssid;
    if (m_ethernetEnabled) {
        m_oled->text(0, "Ethernet");
        ip = ETH.localIP();
    } else if (WiFi.getMode() & WIFI_MODE_AP) {
        m_oled->text(0, "AP mode");
        ip = WiFi.softAPIP();
        ssid = WiFi.softAPSSID().c_str();
    } else if (WiFi.isConnected()) {
        m_oled->text(0, fmt::format("WiFi {}dBm", WiFi.RSSI()).c_str());
        ip = WiFi.localIP();
        ssid = WiFi.SSID().c_str();
    } else {
        m_oled->text(0, "WiFi down");
    }
    // A full IPv4 address can run past 12 characters, so it spans two rows.
    m_oled->text(1, fmt::format("{}.{}.", ip[0], ip[1]).c_str());
    m_oled->text(2, fmt::format("{}.{}", ip[2], ip[3]).c_str());
    m_oled->text(3, ssid.c_str());
    m_oled->text(4, esp_app_get_description()->version);
}
