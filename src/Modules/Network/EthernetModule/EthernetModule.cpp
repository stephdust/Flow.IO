#include "Modules/Network/EthernetModule/EthernetModule.h"

#include "Core/EventBus/EventPayloads.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::EthernetModule)
#include "Core/ModuleLog.h"
#include "Board/BoardSpec.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"

#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_event.h>
#include <esp_eth_mac.h>
#include <esp_eth_phy.h>
#include <esp_netif_ip_addr.h>
#include <string.h>

namespace {
const char* stateName(EthernetState s)
{
    switch (s) {
        case EthernetState::Disabled: return "Disabled";
        case EthernetState::Starting: return "Starting";
        case EthernetState::WaitingIp: return "WaitingIp";
        case EthernetState::Connected: return "Connected";
        case EthernetState::ErrorWait: return "ErrorWait";
        default: return "Unknown";
    }
}
}  // namespace

EthernetModule::EthernetModule(const BoardSpec& board)
{
    const EthernetW5500Spec* eth = boardEthernetW5500(board);
    if (eth) {
        ethCfg_ = *eth;
        hasEthPins_ = ethCfg_.enabled &&
                      ethCfg_.mosiPin >= 0 &&
                      ethCfg_.misoPin >= 0 &&
                      ethCfg_.sclkPin >= 0 &&
                      ethCfg_.csPin >= 0 &&
                      ethCfg_.intPin >= 0 &&
                      ethCfg_.rstPin >= 0;
    }
}

void EthernetModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Ethernet;
    constexpr uint8_t kCfgBranchId = 1U;
    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);

    services_ = &services;
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    setState_(EthernetState::Disabled);
    resetRuntimeState_();
}

void EthernetModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (cfgData_.enabled) {
        if (!hasEthPins_) {
            LOGE("ethernet enabled in config but board has no valid W5500 pin mapping");
            setState_(EthernetState::ErrorWait);
            return;
        }
        if (!services.has(ServiceId::NetworkAccess)) {
            if (services.add(ServiceId::NetworkAccess, &netAccessSvc_)) {
                serviceRegistered_ = true;
            } else {
                LOGE("service registration failed: %s", toString(ServiceId::NetworkAccess));
            }
        }
        setState_(EthernetState::Starting);
        LOGI("Ethernet enabled (W5500 DHCP)");
    } else {
        setState_(EthernetState::Disabled);
        LOGI("Ethernet disabled");
    }
}

void EthernetModule::loop()
{
    if (cfgData_.enabled || driverStarted_) {
        syncRuntimeState_();
    }

    switch (state_) {
        case EthernetState::Disabled:
            vTaskDelay(pdMS_TO_TICKS(300));
            break;

        case EthernetState::Starting:
            if (ensureDriverStarted_()) {
                setState_(EthernetState::WaitingIp);
            } else {
                setState_(EthernetState::ErrorWait);
            }
            vTaskDelay(pdMS_TO_TICKS(80));
            break;

        case EthernetState::WaitingIp:
            if (gotIp_) {
                setState_(EthernetState::Connected);
            }
            vTaskDelay(pdMS_TO_TICKS(120));
            break;

        case EthernetState::Connected:
            if (!gotIp_) {
                setState_(EthernetState::WaitingIp);
            }
            vTaskDelay(pdMS_TO_TICKS(150));
            break;

        case EthernetState::ErrorWait:
            if ((millis() - stateTs_) >= kErrorRetryMs) {
                setState_(EthernetState::Starting);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
    }
}

void EthernetModule::onEthEventStatic_(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData)
{
    EthernetModule* self = static_cast<EthernetModule*>(arg);
    if (self) self->onEthEvent_(eventBase, eventId, eventData);
}

void EthernetModule::onIpEventStatic_(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData)
{
    EthernetModule* self = static_cast<EthernetModule*>(arg);
    if (self) self->onIpEvent_(eventBase, eventId, eventData);
}

void EthernetModule::onEthEvent_(esp_event_base_t eventBase, int32_t eventId, void* eventData)
{
    (void)eventBase;
    (void)eventData;
    switch (eventId) {
        case ETHERNET_EVENT_CONNECTED:
            linkUp_ = true;
            linkDirty_ = true;
            LOGI("ETH link up");
            (void)startDhcpClient_();
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            linkUp_ = false;
            gotIp_ = false;
            ipAddr_ = 0U;
            linkDirty_ = true;
            ipDirty_ = true;
            LOGW("ETH link down");
            break;
        default:
            break;
    }
}

void EthernetModule::onIpEvent_(esp_event_base_t eventBase, int32_t eventId, void* eventData)
{
    (void)eventBase;
    if (eventId == IP_EVENT_ETH_GOT_IP) {
        const ip_event_got_ip_t* got = static_cast<const ip_event_got_ip_t*>(eventData);
        if (!got) return;
        ipAddr_ = got->ip_info.ip.addr;
        gotIp_ = true;
        ipDirty_ = true;
        linkUp_ = true;
        linkDirty_ = true;
        LOGI("ETH got IP " IPSTR, IP2STR(&got->ip_info.ip));
        char ipBuf[16] = {0};
        snprintf(ipBuf, sizeof(ipBuf), IPSTR, IP2STR(&got->ip_info.ip));
        LOGI("Network connected via Ethernet ip=%s", ipBuf);
        return;
    }

    if (eventId == IP_EVENT_ETH_LOST_IP) {
        gotIp_ = false;
        ipAddr_ = 0U;
        ipDirty_ = true;
        LOGW("ETH lost IP");
        LOGW("Network disconnected on Ethernet");
    }
}

void EthernetModule::setState_(EthernetState next)
{
    if (state_ == next) return;
    state_ = next;
    stateTs_ = millis();
    LOGD("state=%s", stateName(state_));
}

void EthernetModule::resetRuntimeState_()
{
    linkUp_ = false;
    gotIp_ = false;
    ipAddr_ = 0U;
    ipDirty_ = true;
    linkDirty_ = true;
    if (dataStore_) {
        setNetworkIp(*dataStore_, IpV4{});
        setNetworkReady(*dataStore_, false);
    }
}

bool EthernetModule::installDriver_()
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGE("esp_netif_init failed err=%d", (int)err);
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGE("esp_event_loop_create_default failed err=%d", (int)err);
        return false;
    }

    spi_bus_config_t buscfg{};
    buscfg.miso_io_num = ethCfg_.misoPin;
    buscfg.mosi_io_num = ethCfg_.mosiPin;
    buscfg.sclk_io_num = ethCfg_.sclkPin;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 0;
    err = spi_bus_initialize(kSpiHost_, &buscfg, SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        spiBusInitialized_ = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        LOGE("spi_bus_initialize failed err=%d", (int)err);
        return false;
    }

    spi_device_interface_config_t devcfg{};
    devcfg.command_bits = 16;
    devcfg.address_bits = 8;
    devcfg.mode = 0;
    devcfg.clock_speed_hz = (int)ethCfg_.spiClockHz;
    devcfg.spics_io_num = ethCfg_.csPin;
    devcfg.queue_size = 20;
    err = spi_bus_add_device(kSpiHost_, &devcfg, &spiHandle_);
    if (err != ESP_OK || !spiHandle_) {
        LOGE("spi_bus_add_device failed err=%d", (int)err);
        return false;
    }

    eth_w5500_config_t w5500Config = ETH_W5500_DEFAULT_CONFIG(spiHandle_);
    w5500Config.int_gpio_num = ethCfg_.intPin;

    eth_mac_config_t macConfig = ETH_MAC_DEFAULT_CONFIG();
    macConfig.sw_reset_timeout_ms = 1000;
    mac_ = esp_eth_mac_new_w5500(&w5500Config, &macConfig);
    if (!mac_) {
        LOGE("esp_eth_mac_new_w5500 failed");
        return false;
    }

    eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
    phyConfig.reset_gpio_num = ethCfg_.rstPin;
    phyConfig.phy_addr = ethCfg_.phyAddr;
    phy_ = esp_eth_phy_new_w5500(&phyConfig);
    if (!phy_) {
        LOGE("esp_eth_phy_new_w5500 failed");
        return false;
    }

    esp_eth_config_t ethConfig = ETH_DEFAULT_CONFIG(mac_, phy_);
    err = esp_eth_driver_install(&ethConfig, &ethHandle_);
    if (err != ESP_OK || !ethHandle_) {
        LOGE("esp_eth_driver_install failed err=%d", (int)err);
        return false;
    }

    esp_netif_config_t netifCfg = ESP_NETIF_DEFAULT_ETH();
    ethNetif_ = esp_netif_new(&netifCfg);
    if (!ethNetif_) {
        LOGE("esp_netif_new failed");
        return false;
    }

    ethGlue_ = esp_eth_new_netif_glue(ethHandle_);
    if (!ethGlue_) {
        LOGE("esp_eth_new_netif_glue failed");
        return false;
    }

    err = esp_netif_attach(ethNetif_, ethGlue_);
    if (err != ESP_OK) {
        LOGE("esp_netif_attach failed err=%d", (int)err);
        return false;
    }

    (void)startDhcpClient_();

    err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &EthernetModule::onEthEventStatic_, this);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGE("ETH event register failed err=%d", (int)err);
        return false;
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &EthernetModule::onIpEventStatic_, this);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGE("IP_EVENT_ETH_GOT_IP register failed err=%d", (int)err);
        return false;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &EthernetModule::onIpEventStatic_, this);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGE("IP_EVENT_ETH_LOST_IP register failed err=%d", (int)err);
        return false;
    }

    err = esp_eth_start(ethHandle_);
    if (err != ESP_OK) {
        LOGE("esp_eth_start failed err=%d", (int)err);
        return false;
    }

    driverStarted_ = true;
    LOGI("Ethernet driver started host=%d mosi=%d miso=%d sclk=%d cs=%d int=%d rst=%d",
         (int)kSpiHost_,
         (int)ethCfg_.mosiPin,
         (int)ethCfg_.misoPin,
         (int)ethCfg_.sclkPin,
         (int)ethCfg_.csPin,
         (int)ethCfg_.intPin,
         (int)ethCfg_.rstPin);
    return true;
}

bool EthernetModule::ensureDriverStarted_()
{
    if (!hasEthPins_) return false;
    if (driverStarted_) return true;
    return installDriver_();
}

bool EthernetModule::startDhcpClient_()
{
    if (!ethNetif_) return false;

    esp_netif_dhcp_status_t dhcpStatus = ESP_NETIF_DHCP_INIT;
    const esp_err_t statusErr = esp_netif_dhcpc_get_status(ethNetif_, &dhcpStatus);
    if (statusErr == ESP_OK) {
        LOGI("ETH DHCP status before start=%d", (int)dhcpStatus);
    } else {
        LOGW("esp_netif_dhcpc_get_status failed err=%d", (int)statusErr);
    }

    const esp_err_t err = esp_netif_dhcpc_start(ethNetif_);
    if (err == ESP_OK || err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        LOGI("ETH DHCP client active err=%d", (int)err);
        return true;
    }

    LOGE("esp_netif_dhcpc_start failed err=%d", (int)err);
    return false;
}

void EthernetModule::syncRuntimeState_()
{
    if (!dataStore_) return;

    if (ipDirty_) {
        ipDirty_ = false;
        IpV4 ip{};
        const esp_ip4_addr_t ip4 = {ipAddr_};
        ip.b[0] = (uint8_t)esp_ip4_addr1_16(&ip4);
        ip.b[1] = (uint8_t)esp_ip4_addr2_16(&ip4);
        ip.b[2] = (uint8_t)esp_ip4_addr3_16(&ip4);
        ip.b[3] = (uint8_t)esp_ip4_addr4_16(&ip4);
        setNetworkIp(*dataStore_, ip);
    }

    if (linkDirty_) {
        linkDirty_ = false;
    }
    setNetworkReady(*dataStore_, gotIp_);
}

bool EthernetModule::isWebReachable_() const
{
    return gotIp_;
}

NetworkAccessMode EthernetModule::mode_() const
{
    return gotIp_ ? NetworkAccessMode::Station : NetworkAccessMode::None;
}

bool EthernetModule::getIp_(char* out, size_t len) const
{
    if (!out || len == 0) return false;
    out[0] = '\0';
    if (!gotIp_) return false;

    const esp_ip4_addr_t ip = {ipAddr_};
    snprintf(out, len, IPSTR, IP2STR(&ip));
    return out[0] != '\0';
}

bool EthernetModule::notifyWifiConfigChanged_()
{
    return false;
}
