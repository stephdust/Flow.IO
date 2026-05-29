#pragma once
/**
 * @file FirmwareUpdateModule.h
 * @brief Supervisor firmware updater (Flow.IO + Nextion).
 */

#include "Core/Module.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/ConfigTypes.h"
#include "Core/CommandRegistry.h"

struct BoardSpec;

class FirmwareUpdateModule : public Module {
public:
    explicit FirmwareUpdateModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::FirmwareUpdate; }
    const char* taskName() const override { return "fwupdate"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 8192; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::Wifi;
        if (i == 2) return ModuleId::Command;
        if (i == 3) return ModuleId::WebInterface;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static constexpr size_t kUrlLen = 192;
    static constexpr size_t kMsgLen = 120;

    enum class UpdateState : uint8_t {
        Idle = 0,
        Queued,
        Downloading,
        Flashing,
        Rebooting,
        Done,
        Error
    };

    struct UpdateJob {
        bool pending = false;
        FirmwareUpdateTarget target = FirmwareUpdateTarget::FlowIO;
        char url[kUrlLen] = {0};
    };

    struct UpdateStatus {
        UpdateState state = UpdateState::Idle;
        FirmwareUpdateTarget target = FirmwareUpdateTarget::FlowIO;
        uint8_t progress = 0;
        uint32_t updatedAtMs = 0;
        char msg[kMsgLen] = {0};
    };

    struct ConfigData {
        char updateHost[64] = "";
        char updatePath[64] = "/binary";
        char flowioPath[64] = "/firmware-flowio.bin";
        char supervisorPath[64] = "/firmware-supervisor.bin";
        char nextionPath[64] = "/Nextion_Flowio_Intelligent_800x480.tft";
        char spiffsPath[64] = "/spiffs-supervisor.bin";
    } cfgData_{};

    ConfigVariable<char, 2> updateHostVar_{
        NVS_KEY("up_host"), "update_host", "fwupdate",
        ConfigType::CharArray, cfgData_.updateHost, ConfigPersistence::Persistent, sizeof(cfgData_.updateHost)
    };
    ConfigVariable<char, 2> updatePathVar_{
        NVS_KEY("up_base_path"), "update_path", "fwupdate",
        ConfigType::CharArray, cfgData_.updatePath, ConfigPersistence::Persistent, sizeof(cfgData_.updatePath)
    };
    ConfigVariable<char, 2> flowioPathVar_{
        NVS_KEY("up_flow_path"), "flowio_path", "fwupdate",
        ConfigType::CharArray, cfgData_.flowioPath, ConfigPersistence::Persistent, sizeof(cfgData_.flowioPath)
    };
    ConfigVariable<char, 2> supervisorPathVar_{
        NVS_KEY("up_sup_path"), "supervisor_path", "fwupdate",
        ConfigType::CharArray, cfgData_.supervisorPath, ConfigPersistence::Persistent, sizeof(cfgData_.supervisorPath)
    };
    ConfigVariable<char, 2> nextionPathVar_{
        NVS_KEY("up_nx_path"), "nextion_path", "fwupdate",
        ConfigType::CharArray, cfgData_.nextionPath, ConfigPersistence::Persistent, sizeof(cfgData_.nextionPath)
    };
    ConfigVariable<char, 2> spiffsPathVar_{
        NVS_KEY("up_cfgdocs_path"), "cfgdocs_path", "fwupdate",
        ConfigType::CharArray, cfgData_.spiffsPath, ConfigPersistence::Persistent, sizeof(cfgData_.spiffsPath)
    };

    ServiceRegistry* services_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    const WebInterfaceService* webInterfaceSvc_ = nullptr;
    const FlowCfgRemoteService* flowCfgSvc_ = nullptr;

    int8_t flowIoEnablePin_ = -1;
    int8_t flowIoBootPin_ = -1;
    int8_t nextionRxPin_ = -1;
    int8_t nextionTxPin_ = -1;
    int8_t nextionRebootPin_ = -1;
    uint32_t nextionUploadBaud_ = 115200U;

    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    UpdateJob queuedJob_{};
    UpdateStatus status_{};
    bool nextionRebootQueued_ = false;
    bool flowIoHardwareRebootQueued_ = false;
    bool busy_ = false;
    uint32_t activeTotalBytes_ = 0;
    uint32_t activeSentBytes_ = 0;

    static bool cmdStatus_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdFlowIo_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSupervisor_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdNextion_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdNextionReboot_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdFlowIoHardwareReboot_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSpiffs_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    bool startUpdate_(FirmwareUpdateTarget target, const char* url, char* errOut, size_t errOutLen);
    bool queueNextionReboot_(char* errOut, size_t errOutLen);
    bool queueFlowIoHardwareReboot_(char* errOut, size_t errOutLen);
    bool statusJson_(char* out, size_t outLen);
    bool isBusy_();
    bool configJson_(char* out, size_t outLen) const;
    bool checkManifestJson_(char* out, size_t outLen, char* errOut, size_t errOutLen);
    bool setConfig_(const char* updateHost,
                    const char* updatePath,
                    const char* flowioPath,
                    const char* supervisorPath,
                    const char* nextionPath,
                    const char* spiffsPath,
                    char* errOut,
                    size_t errOutLen);
    bool runJob_(const UpdateJob& job);
    bool runFlowIoUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runSupervisorUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runNextionUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runNextionReboot_(char* errOut, size_t errOutLen);
    bool runFlowIoHardwareReboot_(char* errOut, size_t errOutLen);
    bool runSpiffsUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool resolveUrl_(FirmwareUpdateTarget target,
                     const char* explicitUrl,
                     char* out,
                     size_t outLen,
                     char* errOut,
                     size_t errOutLen) const;
    bool resolveUpdateUrl_(const char* path,
                           char* out,
                           size_t outLen,
                           char* errOut,
                           size_t errOutLen) const;
    bool parseUrlArg_(const CommandRequest& req, char* out, size_t outLen) const;
    void setStatus_(UpdateState state, FirmwareUpdateTarget target, uint8_t progress, const char* msg);
    void setError_(FirmwareUpdateTarget target, const char* msg);
    void onProgressChunk_(uint32_t chunkBytes);
    void attachWebInterfaceSvcIfNeeded_();
    void attachFlowCfgSvcIfNeeded_();
    bool setFlowCfgPaused_(bool paused);
    static const char* stateStr_(UpdateState s);
    static const char* targetStr_(FirmwareUpdateTarget t);

    FirmwareUpdateService firmwareUpdateSvc_{
        ServiceBinding::bind<&FirmwareUpdateModule::startUpdate_>,
        ServiceBinding::bind<&FirmwareUpdateModule::statusJson_>,
        ServiceBinding::bind<&FirmwareUpdateModule::isBusy_>,
        ServiceBinding::bind<&FirmwareUpdateModule::configJson_>,
        ServiceBinding::bind<&FirmwareUpdateModule::checkManifestJson_>,
        ServiceBinding::bind<&FirmwareUpdateModule::setConfig_>,
        this
    };
};
