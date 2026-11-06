#pragma once

#include "Oled72x40.hpp"
#include "GPIOAllocator.hpp"
#include "app_event_loop.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>

class LockManager;
class MqttManager;
class NfcManager;

/**
 * @class DebugDisplay
 * @brief Shows device status on the OLED, alternating between two pages.
 *
 * Status page: lock state, NFC and MQTT connection, last tap, free heap, uptime.
 * Network page: link and signal, IP address, SSID, firmware version.
 *
 * The display is optional hardware. When nothing answers on its pins begin()
 * releases them and no task is started.
 */
class DebugDisplay {
public:
    DebugDisplay(const LockManager& lockManager, const NfcManager& nfcManager,
                 const MqttManager& mqttManager, bool ethernetEnabled);

    /**
     * @brief Claims the display pins and starts the refresh task.
     * @return true if a display was found.
     */
    bool begin();

private:
    static void taskEntry(void* instance);
    void task();
    void drawStatusPage();
    void drawNetworkPage();

    const LockManager& m_lockManager;
    const NfcManager& m_nfcManager;
    const MqttManager& m_mqttManager;
    const bool m_ethernetEnabled;

    std::unique_ptr<Oled72x40> m_oled;
    std::optional<GPIOAllocator::GPIOLease> m_sdaLease;
    std::optional<GPIOAllocator::GPIOLease> m_sclLease;

    AppEventLoop::SubscriptionHandle m_nfcEvent;
    std::mutex m_lastTapMutex;
    std::string m_lastTap = "-";

    static constexpr const char* TAG = "DebugDisplay";
};
