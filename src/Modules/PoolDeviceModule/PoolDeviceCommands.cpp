/**
 * @file PoolDeviceCommands.cpp
 * @brief Command handlers for manual pool device actions.
 */

#include "PoolDeviceModule.h"
#include "Core/ErrorCodes.h"
#include "Domain/Pool/PoolBindings.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolDeviceModule)
#include "Core/ModuleLog.h"
#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

namespace {
// Commands accept either a plain args object or a wrapped root payload.
bool parseCmdArgsObject_(const CommandRequest& req, JsonObjectConst& outObj)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::JsonCmdPoolDeviceBuf;
    static StaticJsonDocument<CMD_DOC_CAPACITY> doc;

    doc.clear();
    const char* json = req.args ? req.args : req.json;
    if (!json || json[0] == '\0') return false;

    const DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonObject>()) {
        outObj = doc.as<JsonObjectConst>();
        return true;
    }

    if (req.json && req.json[0] != '\0' && req.args != req.json) {
        doc.clear();
        const DeserializationError rootErr = deserializeJson(doc, req.json);
        if (rootErr || !doc.is<JsonObjectConst>()) return false;
        JsonVariantConst argsVar = doc["args"];
        if (argsVar.is<JsonObjectConst>()) {
            outObj = argsVar.as<JsonObjectConst>();
            return true;
        }
    }

    return false;
}

void writeCmdError_(char* reply, size_t replyLen, const char* where, ErrorCode code)
{
    if (!writeErrorJson(reply, replyLen, code, where)) {
        snprintf(reply, replyLen, "{\"ok\":false}");
    }
}

void writeCmdErrorSlot_(char* reply, size_t replyLen, const char* where, ErrorCode code, uint8_t slot)
{
    if (!writeErrorJsonWithSlot(reply, replyLen, code, where, slot)) {
        snprintf(reply, replyLen, "{\"ok\":false}");
    }
}
} // namespace

bool PoolDeviceModule::cmdPoolWrite_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(userCtx);
    if (!self) return false;
    return self->handlePoolWrite_(req, reply, replyLen);
}

bool PoolDeviceModule::cmdPoolRefill_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(userCtx);
    if (!self) return false;
    return self->handlePoolRefill_(req, reply, replyLen);
}

bool PoolDeviceModule::handlePoolWrite_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingArgs);
        return false;
    }

    if (!args.containsKey("slot")) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingSlot);
        return false;
    }
    if (!args["slot"].is<uint8_t>()) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::BadSlot);
        return false;
    }
    const uint8_t slot = args["slot"].as<uint8_t>();
    if (slot >= POOL_DEVICE_MAX) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::BadSlot);
        return false;
    }

    if (!args.containsKey("value")) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingValue);
        return false;
    }

    JsonVariantConst value = args["value"];
    bool requested = false;
    if (value.is<bool>()) {
        requested = value.as<bool>();
    } else if (value.is<int32_t>() || value.is<uint32_t>() || value.is<float>()) {
        requested = (value.as<float>() != 0.0f);
    } else if (value.is<const char*>()) {
        const char* s = value.as<const char*>();
        if (!s) s = "0";
        if (strcmp(s, "true") == 0) requested = true;
        else if (strcmp(s, "false") == 0) requested = false;
        else requested = (atoi(s) != 0);
    } else {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingValue);
        return false;
    }

    const PoolDeviceSvcStatus st = svcWriteDesiredImpl_(slot, requested ? 1U : 0U);
    if (st != POOLDEV_SVC_OK) {
        ErrorCode code = ErrorCode::Failed;
        if (st == POOLDEV_SVC_ERR_UNKNOWN_SLOT) code = ErrorCode::UnknownSlot;
        else if (st == POOLDEV_SVC_ERR_NOT_READY) code = ErrorCode::NotReady;
        else if (st == POOLDEV_SVC_ERR_DISABLED) code = ErrorCode::Disabled;
        else if (st == POOLDEV_SVC_ERR_INTERLOCK) code = ErrorCode::InterlockBlocked;
        else if (st == POOLDEV_SVC_ERR_IO) code = ErrorCode::IoError;
        writeCmdErrorSlot_(reply, replyLen, "pooldevice.write", code, slot);
        return false;
    }

    // Global business rule for manual starts:
    // if a dosing pump is manually forced ON, disable the corresponding auto mode
    // regardless of the command source (HMI, MQTT, Web, ...).
    if (requested && cfgStore_) {
        uint8_t phPumpSlot = PoolBinding::kDeviceSlotPhPump;
        uint8_t orpPumpSlot = PoolBinding::kDeviceSlotChlorinePump;

        char modeJson[160]{};
        bool truncated = false;
        if (cfgStore_->toJsonModule("poollogic/device", modeJson, sizeof(modeJson), &truncated) && !truncated) {
            StaticJsonDocument<192> modeDoc;
            const DeserializationError modeErr = deserializeJson(modeDoc, modeJson);
            if (!modeErr && modeDoc.is<JsonObjectConst>()) {
                const JsonObjectConst obj = modeDoc.as<JsonObjectConst>();
                if (obj["ph_pump_slot"].is<uint16_t>()) {
                    const uint16_t v = obj["ph_pump_slot"].as<uint16_t>();
                    if (v < POOL_DEVICE_MAX) phPumpSlot = (uint8_t)v;
                }
                if (obj["orp_pump_slot"].is<uint16_t>()) {
                    const uint16_t v = obj["orp_pump_slot"].as<uint16_t>();
                    if (v < POOL_DEVICE_MAX) orpPumpSlot = (uint8_t)v;
                }
            }
        }

        const char* modeKey = nullptr;
        if (slot == phPumpSlot) modeKey = "ph_auto_mode";
        else if (slot == orpPumpSlot) modeKey = "orp_auto_mode";

        if (modeKey) {
            char patch[96]{};
            snprintf(patch, sizeof(patch), "{\"poollogic/mode\":{\"%s\":false}}", modeKey);
            if (!cfgStore_->applyJson(patch)) {
                LOGW("Manual pump start slot=%u failed to clear %s",
                     (unsigned)slot,
                     modeKey);
            } else {
                LOGI("Manual pump start slot=%u -> %s=false",
                     (unsigned)slot,
                     modeKey);
            }
        }
    }

    const char* label = deviceLabel(slot);
    LOGI("Manual %s %s (slot=%u)",
         requested ? "Start" : "Stop",
         (label && label[0] != '\0') ? label : "Pool Device",
         (unsigned)slot);
    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u}", (unsigned)slot);
    return true;
}

bool PoolDeviceModule::handlePoolRefill_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::MissingArgs);
        return false;
    }

    if (!args.containsKey("slot")) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::MissingSlot);
        return false;
    }
    if (!args["slot"].is<uint8_t>()) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::BadSlot);
        return false;
    }
    const uint8_t slot = args["slot"].as<uint8_t>();
    if (slot >= POOL_DEVICE_MAX) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::BadSlot);
        return false;
    }

    float remaining = slots_[slot].def.tankCapacityMl;
    if (args.containsKey("remaining_ml")) {
        JsonVariantConst rem = args["remaining_ml"];
        if (rem.is<float>() || rem.is<double>() || rem.is<int32_t>() || rem.is<uint32_t>()) {
            remaining = rem.as<float>();
        } else if (rem.is<const char*>()) {
            const char* s = rem.as<const char*>();
            remaining = s ? (float)atof(s) : remaining;
        }
    }

    const PoolDeviceSvcStatus st = svcRefillTankImpl_(slot, remaining);
    if (st != POOLDEV_SVC_OK) {
        const ErrorCode code = (st == POOLDEV_SVC_ERR_UNKNOWN_SLOT) ? ErrorCode::UnknownSlot : ErrorCode::Failed;
        writeCmdErrorSlot_(reply, replyLen, "pool.refill", code, slot);
        return false;
    }

    const float applied = slots_[slot].tankRemainingMl;
    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u,\"remaining_ml\":%.1f}", (unsigned)slot, (double)applied);
    return true;
}
