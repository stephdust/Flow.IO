/**
 * @file ConfigStoreModule.cpp
 * @brief Implementation file.
 */
#include "ConfigStoreModule.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::ConfigStoreModule)
#include "Core/ModuleLog.h"


bool ConfigStoreModule::applyJson_(const char* json) {
    return registry ? registry->applyJson(json) : false;
}

void ConfigStoreModule::toJson_(char* out, size_t outLen) {
    if (!registry) return;
    registry->toJson(out, outLen);
}

bool ConfigStoreModule::toJsonModule_(const char* module, char* out, size_t outLen, bool* truncated) {
    return registry ? registry->toJsonModule(module, out, outLen, truncated) : false;
}

uint8_t ConfigStoreModule::listModules_(const char** out, uint8_t max) {
    return registry ? registry->listModules(out, max) : 0;
}

bool ConfigStoreModule::erase_() {
    return registry ? registry->erasePersistent() : false;
}

void ConfigStoreModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    registry = &cfg;

    /// récupérer service loghub (log async)
    logHub = services.get<LogHubService>(ServiceId::LogHub);

    if (!services.add(ServiceId::ConfigStore, &svc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::ConfigStore));
    }
    LOGI("ConfigStoreService registered");
}
