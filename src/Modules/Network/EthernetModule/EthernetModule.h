#pragma once
/**
 * @file EthernetModule.h
 * @brief W5500 Ethernet connectivity module (DHCP).
 */

#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Board/BoardTypes.h"

#include <driver/spi_master.h>
#include <esp_eth.h>
#include <esp_eth_netif_glue.h>
#include <esp_netif.h>

enum class EthernetState : uint8_t {
    Disabled = 0,
    Starting,
    WaitingIp,
    Connected,
    ErrorWait
};

struct EthernetConfig {
    bool enabled = false;
};

class EthernetModule : public Module {
public:
    EthernetModule() = default;
    explicit EthernetModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::Ethernet; }
    const char* taskName() const override { return "ethernet"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 4096; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 2; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static constexpr uint32_t kErrorRetryMs = 3000U;
    static constexpr spi_host_device_t kSpiHost_ = SPI2_HOST;

    EthernetConfig cfgData_{};
    EthernetState state_ = EthernetState::Disabled;
    uint32_t stateTs_ = 0U;
    bool serviceRegistered_ = false;

    DataStore* dataStore_ = nullptr;
    ServiceRegistry* services_ = nullptr;

    esp_eth_handle_t ethHandle_ = nullptr;
    esp_eth_mac_t* mac_ = nullptr;
    esp_eth_phy_t* phy_ = nullptr;
    esp_netif_t* ethNetif_ = nullptr;
    esp_eth_netif_glue_handle_t ethGlue_ = nullptr;
    spi_device_handle_t spiHandle_ = nullptr;
    bool spiBusInitialized_ = false;
    bool driverStarted_ = false;
    EthernetW5500Spec ethCfg_{};
    bool hasEthPins_ = false;

    volatile bool linkUp_ = false;
    volatile bool gotIp_ = false;
    volatile bool ipDirty_ = false;
    volatile bool linkDirty_ = false;
    volatile uint32_t ipAddr_ = 0U;

    ConfigVariable<bool,0> enabledVar_{
        NVS_KEY(NvsKeys::Ethernet::Enabled), "enabled", "ethernet",
        ConfigType::Bool, &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };

    static void onEthEventStatic_(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData);
    static void onIpEventStatic_(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData);
    void onEthEvent_(esp_event_base_t eventBase, int32_t eventId, void* eventData);
    void onIpEvent_(esp_event_base_t eventBase, int32_t eventId, void* eventData);

    void setState_(EthernetState next);
    void resetRuntimeState_();
    bool ensureDriverStarted_();
    bool installDriver_();
    bool startDhcpClient_();
    void syncRuntimeState_();

    bool isWebReachable_() const;
    NetworkAccessMode mode_() const;
    bool getIp_(char* out, size_t len) const;
    bool notifyWifiConfigChanged_();

    NetworkAccessService netAccessSvc_{
        ServiceBinding::bind<&EthernetModule::isWebReachable_>,
        ServiceBinding::bind<&EthernetModule::mode_>,
        ServiceBinding::bind<&EthernetModule::getIp_>,
        ServiceBinding::bind<&EthernetModule::notifyWifiConfigChanged_>,
        this
    };
};
