#pragma once
/**
 * @file SystemModule.h
 * @brief System command module (ping/reboot/factory reset).
 */
#include "Core/ModulePassive.h"
#include "Core/NvsKeys.h"
#include "Core/RuntimeUi.h"
#include "Core/Services/Services.h"

/**
 * @brief Passive module that registers system commands.
 */
class SystemModule : public ModulePassive, public IRuntimeUiValueProvider {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::System; }
    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }

    /** @brief Depends on log hub, command service and config service. */
    uint8_t dependencyCount() const override { return 3; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::Command;
        if (i == 2) return ModuleId::ConfigStore;
        return ModuleId::Unknown;
    }

    /** @brief Register system commands. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override;

private:
    struct SystemConfig {
        char lang[8] = "fr";
    };

    enum RuntimeUiValueId : uint8_t {
        RuntimeUiFirmware = 1,
        RuntimeUiUptimeMs = 2,
        RuntimeUiHeapFree = 3,
        RuntimeUiHeapMinFree = 4,
    };

    const CommandService* cmdSvc = nullptr;
    const ConfigStoreService* cfgSvc = nullptr;
    const LogHubService* logHub = nullptr;
    SystemConfig cfgData_{};

    ConfigVariable<char,0> languageVar_{
        NVS_KEY(NvsKeys::System::Language), "lang", "system", ConfigType::CharArray,
        (char*)cfgData_.lang, ConfigPersistence::Persistent, sizeof(cfgData_.lang)
    };

    static bool cmdPing(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdReboot(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdFactoryReset(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
};
