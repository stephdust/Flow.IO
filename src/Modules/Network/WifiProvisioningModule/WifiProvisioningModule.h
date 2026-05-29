#pragma once
/**
 * @file WifiProvisioningModule.h
 * @brief WiFi provisioning overlay (STA fallback to AP portal).
 */

#include "Core/Module.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"

#include <DNSServer.h>
#include <WiFi.h>

class WifiProvisioningModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::WifiProvisioning; }
    const char* taskName() const override { return "wifiprov"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override {
#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
        return 5120;
#else
        return 4096;
#endif
    }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 2; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::Wifi;
        if (i == 1) return ModuleId::LogHub;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void onStart(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;
    uint32_t startDelayMs() const override { return Limits::Boot::WifiProvisioningStartDelayMs; }

private:
    enum class PortalReason : uint8_t {
        None = 0,
        MissingCredentials = 1,
        ConnectTimeout = 2
    };

#if defined(FLOW_PROFILE_MICRONOVA)
    static constexpr uint32_t kConnectTimeoutMs = 8000U;
#else
    static constexpr uint32_t kConnectTimeoutMs = 25000U;
#endif
    static constexpr uint32_t kConfigPollMs = 1000U;
    static constexpr uint16_t kDnsPort = 53;
    static constexpr uint32_t kApClientPollMs = 1000U;
    static constexpr uint32_t kApClientGraceMs = 120000U;
    static constexpr uint32_t kStaProbeIntervalMs = 30000U;
    static constexpr uint32_t kStaProbeWindowMs = 6000U;

    ConfigStore* cfgStore_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;

    DNSServer dns_;
    bool apActive_ = false;
    bool wifiConfigured_ = false;
    bool wifiEnabled_ = true;
    bool ethernetEnabled_ = false;
    bool configDirty_ = false;
    bool portalLatched_ = false;
    bool staProbeActive_ = false;
    uint8_t apClientCount_ = 0;
    uint32_t lastApClientSeenMs_ = 0;
    uint32_t lastApClientPollMs_ = 0;
    uint32_t lastStaProbeStartMs_ = 0;
    uint32_t bootMs_ = 0;
    uint32_t lastCfgPollMs_ = 0;
    wifi_event_id_t wifiEventHandlerId_ = 0;
    char apSsid_[40] = {0};
    char apPass_[32] = {0};
#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
    WiFiServer portalServer_{80};
    bool portalHttpActive_ = false;
    bool portalCredentialsSaved_ = false;
    bool portalSpiffsReady_ = false;
    bool portalRebootPending_ = false;
    char portalReqLine_[384] = {0};
    char portalPath_[384] = {0};
    char portalMethod_[8] = {0};
    char portalBody_[384] = {0};
    char portalSsid_[33] = {0};
    char portalPass_[65] = {0};
    char portalJson_[320] = {0};
    char portalEscSsid_[67] = {0};
    char portalEscPass_[131] = {0};
    char portalScanJson_[Limits::Wifi::Buffers::ScanStatusJson] = {0};
    uint8_t portalFileBuf_[256] = {0};
    uint32_t portalRebootAtMs_ = 0;
#endif

    bool isWebReachable_() const;
    NetworkAccessMode mode_() const;
    bool getIP_(char* out, size_t len) const;
    bool notifyWifiConfigChanged_();

    void buildApCredentials_();
    void handleStaProbePolicy_(uint32_t nowMs);
    void refreshApClientState_(uint32_t nowMs, bool fromEvent);
    void startStaProbe_(uint32_t nowMs);
    void stopStaProbe_(const char* reason);
    void refreshWifiConfig_();
    PortalReason evaluatePortalReason_() const;
    void ensurePortalStarted_();
    static void onWifiEventSys_(arduino_event_t* event);
    void onWifiEvent_(arduino_event_t* event);
    bool startCaptivePortal_(PortalReason reason);
    void stopCaptivePortal_();
    bool isStaConnected_() const;
    bool getStaIp_(char* out, size_t len) const;
    bool getApIp_(char* out, size_t len) const;
#if defined(FLOW_PROFILE_FLOW_CONNECT_DISPLAY)
    void startLightPortal_();
    void stopLightPortal_();
    void handleLightPortalClient_();
    bool handleLightPortalRequest_(WiFiClient& client,
                                   const char* method,
                                   const char* path,
                                   const char* query,
                                   const char* body);
    void sendHttpHeader_(WiFiClient& client, const char* status, const char* contentType);
    void sendJson_(WiFiClient& client, const char* status, const char* body);
    void sendPlain_(WiFiClient& client, const char* status, const char* body);
    bool sendSpiffsAsset_(WiFiClient& client, const char* path, const char* contentType);
    void sendPortalFallbackPage_(WiFiClient& client, const char* message, bool success);
    void sendWebMetaJson_(WiFiClient& client);
    void sendWifiConfigJson_(WiFiClient& client);
    void sendApStatusJson_(WiFiClient& client);
    void sendWifiScanJson_(WiFiClient& client);
    void sendSaveResponse_(WiFiClient& client, bool ok);
    void schedulePortalReboot_(uint32_t delayMs);
    bool readRequestLine_(WiFiClient& client, char* out, size_t outLen);
    bool readHeaderLine_(WiFiClient& client, char* out, size_t outLen);
    bool readRequestBody_(WiFiClient& client, size_t contentLen, char* out, size_t outLen);
    bool handleSaveRequest_(const char* query);
    static bool getQueryParam_(const char* query, const char* key, char* out, size_t outLen);
    static bool urlDecode_(const char* in, size_t len, char* out, size_t outLen);
    static int hexNibble_(char c);
    static bool jsonEscape_(const char* in, char* out, size_t outLen);
#endif

    NetworkAccessService netAccessSvc_{
        ServiceBinding::bind<&WifiProvisioningModule::isWebReachable_>,
        ServiceBinding::bind<&WifiProvisioningModule::mode_>,
        ServiceBinding::bind<&WifiProvisioningModule::getIP_>,
        ServiceBinding::bind<&WifiProvisioningModule::notifyWifiConfigChanged_>,
        this
    };
};
