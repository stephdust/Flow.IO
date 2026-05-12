/**
 * @file WebInterfaceServer.cpp
 * @brief HTTP server wiring and network-facing endpoints for WebInterfaceModule.
 */

#include "WebInterfaceModule.h"

#include "Board/BoardSpec.h"
#include "App/BuildFlags.h"
#include "Core/FirmwareVersion.h"
#include "Core/Generated/RuntimeUiManifest_Generated.h"
#include "Core/I2cCfgProtocol.h"
#include "Core/SystemLimits.h"
#include "Core/SystemStats.h"
#if !defined(FLOW_PROFILE_MICRONOVA)
#include "Modules/Network/I2CCfgClientModule/I2CCfgClientRuntime.h"
#else
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerModuleDataModel.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#endif

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::WebInterfaceModule)
#include "Core/ModuleLog.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"

#ifndef FLOW_WEB_HIDE_MENU_SVG
#define FLOW_WEB_HIDE_MENU_SVG 0
#endif

#ifndef FLOW_WEB_UNIFY_STATUS_CARD_ICONS
#define FLOW_WEB_UNIFY_STATUS_CARD_ICONS 0
#endif

#ifndef FLOW_WEB_DISABLE_ICONS
#define FLOW_WEB_DISABLE_ICONS 0
#endif

#ifndef FLOW_WEB_HEAP_FORENSICS
#define FLOW_WEB_HEAP_FORENSICS 0
#endif

static void sanitizeJsonString_(char* s)
{
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') {
            s[i] = ' ';
        }
    }
}

static void printJsonEscaped_(Print& out, const char* s)
{
    out.print('\"');
    if (s) {
        for (const char* p = s; *p != '\0'; ++p) {
            switch (*p) {
            case '\"': out.print("\\\""); break;
            case '\\': out.print("\\\\"); break;
            case '\b': out.print("\\b"); break;
            case '\f': out.print("\\f"); break;
            case '\n': out.print("\\n"); break;
            case '\r': out.print("\\r"); break;
            case '\t': out.print("\\t"); break;
            default:
                if ((uint8_t)*p < 0x20U) {
                    out.print('?');
                } else {
                    out.print(*p);
                }
                break;
            }
        }
    }
    out.print('\"');
}

static bool parseBoolParam_(const char* in, bool fallback)
{
    if (!in || in[0] == '\0') return fallback;
    if (strcasecmp(in, "1") == 0 || strcasecmp(in, "true") == 0 || strcasecmp(in, "yes") == 0 ||
        strcasecmp(in, "on") == 0) {
        return true;
    }
    if (strcasecmp(in, "0") == 0 || strcasecmp(in, "false") == 0 || strcasecmp(in, "no") == 0 ||
        strcasecmp(in, "off") == 0) {
        return false;
    }
    return fallback;
}

static bool copyRequestParamValue_(AsyncWebServerRequest* request,
                                   const char* name,
                                   bool post,
                                   char* out,
                                   size_t outLen,
                                   const char* fallback = "")
{
    if (!out || outLen == 0U) return false;
    out[0] = '\0';
    if (!request || !name) {
        snprintf(out, outLen, "%s", fallback ? fallback : "");
        return false;
    }
    if (!request->hasParam(name, post)) {
        snprintf(out, outLen, "%s", fallback ? fallback : "");
        return false;
    }
    const AsyncWebParameter* param = request->getParam(name, post);
    if (!param) {
        snprintf(out, outLen, "%s", fallback ? fallback : "");
        return false;
    }
    const String& value = param->value();
    snprintf(out, outLen, "%s", value.c_str());
    return true;
}

template <size_t N>
static inline void sendProgmemLiteral_(AsyncWebServerRequest* request, const char* contentType, const char (&content)[N])
{
    if (!request || !contentType || N == 0U) return;
    request->send(200, contentType, reinterpret_cast<const uint8_t*>(content), N - 1U);
}

namespace {
constexpr uint32_t kHttpLatencyInfoMs = 40U;
constexpr uint32_t kHttpLatencyWarnMs = 120U;
constexpr uint32_t kHttpLatencyFlowCfgInfoMs = 200U;
constexpr uint32_t kHttpLatencyFlowCfgWarnMs = 900U;
constexpr uint32_t kHeapGuardAssetFreeBytesLight = 8192U;
constexpr uint32_t kHeapGuardAssetFreeBytesMinor = 12288U;
constexpr uint32_t kHeapGuardAssetFreeBytesMajor = 15360U;
void (*gHttpActivityHook)(void*) = nullptr;
void* gHttpActivityHookCtx = nullptr;

const char* httpMethodName_(uint32_t method);
void addNoCacheHeaders_(AsyncWebServerResponse* response);

size_t tokenLenToSlash_(const char* s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n] != '\0' && s[n] != '/') ++n;
    return n;
}

bool childTokenForPrefix_(const char* module,
                          const char* prefix,
                          size_t prefixLen,
                          const char*& childStart,
                          size_t& childLen,
                          bool& isExact)
{
    childStart = nullptr;
    childLen = 0;
    isExact = false;
    if (!module || module[0] == '\0') return false;

    if (prefixLen == 0) {
        childStart = module;
        childLen = tokenLenToSlash_(module);
        return childLen > 0;
    }

    if (strncmp(module, prefix, prefixLen) != 0) return false;
    const char sep = module[prefixLen];
    if (sep == '\0') {
        isExact = true;
        return false;
    }
    if (sep != '/') return false;

    childStart = module + prefixLen + 1;
    childLen = tokenLenToSlash_(childStart);
    return childLen > 0;
}

bool tokensEqual_(const char* a, size_t aLen, const char* b, size_t bLen)
{
    if (!a || !b) return false;
    if (aLen != bLen) return false;
    if (aLen == 0) return true;
    return strncmp(a, b, aLen) == 0;
}

struct HeapForensicSnapshot {
    uint32_t freeBytes = 0;
    uint32_t minFreeBytes = 0;
    uint32_t largestFreeBlock = 0;
};

struct SpiffsAssetForensicMeta {
    char assetName[24] = {0};
    uint32_t sizeBytes = 0;
    bool gzip = false;
};

HeapForensicSnapshot captureHeapForensicSnapshot_()
{
    HeapForensicSnapshot snap{};
    snap.freeBytes = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    snap.minFreeBytes = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    snap.largestFreeBlock = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    return snap;
}

const char* pathBaseName_(const char* path)
{
    if (!path || path[0] == '\0') return "-";
    const char* slash = strrchr(path, '/');
    return (slash && slash[1] != '\0') ? (slash + 1) : path;
}

bool hasPathSuffix_(const char* path, const char* suffix)
{
    if (!path || !suffix) return false;
    const size_t pathLen = strlen(path);
    const size_t suffixLen = strlen(suffix);
    return pathLen >= suffixLen && strcmp(path + (pathLen - suffixLen), suffix) == 0;
}

bool isMinorWebAssetPath_(const char* path)
{
    return hasPathSuffix_(path, ".svg") || hasPathSuffix_(path, ".png") || hasPathSuffix_(path, ".ico");
}

bool isLightWebAssetPath_(const char* path)
{
    return path && strstr(path, "/webinterface/light.") != nullptr;
}

bool shouldRejectAssetByFreeHeap_(const char* assetPath, uint32_t* freeBytesOut = nullptr)
{
    const uint32_t freeBytes = (uint32_t)ESP.getFreeHeap();
    if (freeBytesOut) *freeBytesOut = freeBytes;
    const uint32_t minFreeBytes = isLightWebAssetPath_(assetPath)
        ? kHeapGuardAssetFreeBytesLight
        : isMinorWebAssetPath_(assetPath)
        ? kHeapGuardAssetFreeBytesMinor
        : kHeapGuardAssetFreeBytesMajor;
    return freeBytes < minFreeBytes;
}

void buildProvisioningApSsid_(char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;
    const uint64_t chipId = ESP.getEfuseMac();
    const uint8_t b0 = (uint8_t)(chipId >> 16);
    const uint8_t b1 = (uint8_t)(chipId >> 8);
    const uint8_t b2 = (uint8_t)(chipId >> 0);
    snprintf(out, outLen, "FlowIO-%s-%02X%02X%02X", FLOW_BUILD_PROFILE_NAME, b0, b1, b2);
}

bool isModuleFlagAndStringConfigured_(ConfigStore* cfgStore,
                                      const char* moduleName,
                                      const char* enabledKey,
                                      bool enabledDefault,
                                      const char* valueKey)
{
    if (!cfgStore || !moduleName || !enabledKey || !valueKey) return false;
    char moduleJson[512] = {0};
    if (!cfgStore->toJsonModule(moduleName, moduleJson, sizeof(moduleJson), nullptr, false)) return false;

    StaticJsonDocument<512> doc;
    const DeserializationError err = deserializeJson(doc, moduleJson);
    if (err || !doc.is<JsonObjectConst>()) return false;

    JsonObjectConst root = doc.as<JsonObjectConst>();
    const bool enabled = root[enabledKey] | enabledDefault;
    const char* value = root[valueKey] | "";
    return enabled && value && value[0] != '\0';
}

bool isProvisioningConfigured_(ConfigStore* cfgStore, bool requireMqtt)
{
    const bool wifiConfigured =
        isModuleFlagAndStringConfigured_(cfgStore, "wifi", "enabled", true, "ssid");
    if (!wifiConfigured) return false;
    if (!requireMqtt) return true;
    return isModuleFlagAndStringConfigured_(cfgStore, "mqtt", "enabled", false, "host");
}

void appendJsonFieldName_(Print& out, const char* key)
{
    out.print(",\"");
    out.print(key ? key : "");
    out.print("\":");
}

void appendJsonFieldValue_(Print& out, const char* key, JsonVariantConst value)
{
    appendJsonFieldName_(out, key);
    serializeJson(value, out);
}

bool sendFlowStatusCompactResponse_(AsyncWebServerRequest* request, const FlowCfgRemoteService* flowCfgSvc)
{
    if (!request || !flowCfgSvc || !flowCfgSvc->runtimeStatusDomainJson) return false;

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    addNoCacheHeaders_(response);
    response->print("{\"ok\":true");

    char domainBuf[640] = {0};
    StaticJsonDocument<768> domainDoc;
    bool anyDomainOk = false;

    auto loadDomain = [&](FlowStatusDomain domain) -> JsonObjectConst {
        memset(domainBuf, 0, sizeof(domainBuf));
        domainDoc.clear();
        if (!flowCfgSvc->runtimeStatusDomainJson(flowCfgSvc->ctx, domain, domainBuf, sizeof(domainBuf))) {
            return JsonObjectConst();
        }
        const DeserializationError err = deserializeJson(domainDoc, domainBuf);
        if (err || !domainDoc.is<JsonObjectConst>()) {
            domainDoc.clear();
            return JsonObjectConst();
        }
        JsonObjectConst root = domainDoc.as<JsonObjectConst>();
        if (!(root["ok"] | false)) {
            domainDoc.clear();
            return JsonObjectConst();
        }
        anyDomainOk = true;
        return root;
    };

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::System);
        if (!root.isNull()) {
            appendJsonFieldName_(*response, "fw");
            printJsonEscaped_(*response, root["fw"] | "");
            appendJsonFieldValue_(*response, "upms", root["upms"]);
            response->print(",\"heap\":{");
            JsonObjectConst heapIn = root["heap"];
            response->print("\"free\":");
            serializeJson(heapIn["free"], *response);
            appendJsonFieldValue_(*response, "min_free", heapIn["min_free"]);
            response->print('}');
        }
    }

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::Wifi);
        if (!root.isNull()) {
            JsonObjectConst wifiIn = root["wifi"];
            response->print(",\"wifi\":{");
            response->print("\"rdy\":");
            serializeJson(wifiIn["rdy"], *response);
            appendJsonFieldName_(*response, "ip");
            printJsonEscaped_(*response, wifiIn["ip"] | "");
            appendJsonFieldValue_(*response, "hrss", wifiIn["hrss"]);
            appendJsonFieldValue_(*response, "rssi", wifiIn["rssi"]);
            response->print('}');
        }
    }

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::Mqtt);
        if (!root.isNull()) {
            JsonObjectConst mqttIn = root["mqtt"];
            response->print(",\"mqtt\":{");
            response->print("\"rdy\":");
            serializeJson(mqttIn["rdy"], *response);
            appendJsonFieldName_(*response, "srv");
            printJsonEscaped_(*response, mqttIn["srv"] | "");
            appendJsonFieldValue_(*response, "rxdrp", mqttIn["rxdrp"]);
            appendJsonFieldValue_(*response, "prsf", mqttIn["prsf"]);
            appendJsonFieldValue_(*response, "hndf", mqttIn["hndf"]);
            appendJsonFieldValue_(*response, "ovr", mqttIn["ovr"]);
            response->print('}');
        }
    }

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::Pool);
        if (!root.isNull()) {
            JsonObjectConst poolIn = root["pool"];
            response->print(",\"pool\":{");
            response->print("\"has\":");
            serializeJson(poolIn["has"], *response);
            appendJsonFieldValue_(*response, "auto", poolIn["auto"]);
            appendJsonFieldValue_(*response, "wint", poolIn["wint"]);
            appendJsonFieldValue_(*response, "wat", poolIn["wat"]);
            appendJsonFieldValue_(*response, "air", poolIn["air"]);
            appendJsonFieldValue_(*response, "ph", poolIn["ph"]);
            appendJsonFieldValue_(*response, "orp", poolIn["orp"]);
            appendJsonFieldValue_(*response, "fil", poolIn["fil"]);
            appendJsonFieldValue_(*response, "php", poolIn["php"]);
            appendJsonFieldValue_(*response, "clp", poolIn["clp"]);
            appendJsonFieldValue_(*response, "rbt", poolIn["rbt"]);
            response->print('}');
        }
    }

    {
        JsonObjectConst root = loadDomain(FlowStatusDomain::I2c);
        if (!root.isNull()) {
            JsonObjectConst i2cIn = root["i2c"];
            response->print(",\"i2c\":{");
            response->print("\"lnk\":");
            serializeJson(i2cIn["lnk"], *response);
            appendJsonFieldValue_(*response, "seen", i2cIn["seen"]);
            appendJsonFieldValue_(*response, "req", i2cIn["req"]);
            appendJsonFieldValue_(*response, "breq", i2cIn["breq"]);
            appendJsonFieldValue_(*response, "ago", i2cIn["ago"]);
            response->print('}');
        }
    }

    if (!anyDomainOk) {
        delete response;
        request->send(500, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.status\"}}");
        return false;
    }

    response->print('}');
    request->send(response);
    return true;
}

void fillSpiffsAssetForensicMeta_(SpiffsAssetForensicMeta* out,
                                  const char* servedPath,
                                  uint32_t sizeBytes,
                                  bool gzip)
{
    if (!out) return;
    snprintf(out->assetName, sizeof(out->assetName), "%s", pathBaseName_(servedPath));
    out->sizeBytes = sizeBytes;
    out->gzip = gzip;
}

#if FLOW_WEB_HEAP_FORENSICS
void logHttpHeapForensic_(AsyncWebServerRequest* req,
                          const char* route,
                          uint32_t startUs,
                          const HeapForensicSnapshot& startHeap)
{
    const HeapForensicSnapshot endHeap = captureHeapForensicSnapshot_();
    const uint32_t elapsedUs = micros() - startUs;
    const long deltaFree = (long)endHeap.freeBytes - (long)startHeap.freeBytes;
    const uint32_t lowWaterDrop =
        (startHeap.minFreeBytes > endHeap.minFreeBytes) ? (startHeap.minFreeBytes - endHeap.minFreeBytes) : 0U;
    const char* method = req ? httpMethodName_(req->method()) : "?";
    LOGW("HTTPfx %s %s us=%lu f0=%lu f1=%lu df=%ld m1=%lu lo=%lu",
         method,
         route ? route : "?",
         (unsigned long)elapsedUs,
         (unsigned long)startHeap.freeBytes,
         (unsigned long)endHeap.freeBytes,
         deltaFree,
         (unsigned long)endHeap.minFreeBytes,
         (unsigned long)lowWaterDrop);
}

void logSpiffsAssetHeapForensic_(const char* stage,
                                 const SpiffsAssetForensicMeta& meta,
                                 uint32_t startUs,
                                 const HeapForensicSnapshot& startHeap)
{
    const HeapForensicSnapshot endHeap = captureHeapForensicSnapshot_();
    const uint32_t elapsedUs = micros() - startUs;
    const long deltaFree = (long)endHeap.freeBytes - (long)startHeap.freeBytes;
    const uint32_t lowWaterDrop =
        (startHeap.minFreeBytes > endHeap.minFreeBytes) ? (startHeap.minFreeBytes - endHeap.minFreeBytes) : 0U;
    LOGW("ASfx %s %s u=%lu f0=%lu f1=%lu d=%ld m=%lu l=%lu s=%lu z=%u",
         stage ? stage : "?",
         meta.assetName[0] ? meta.assetName : "-",
         (unsigned long)elapsedUs,
         (unsigned long)startHeap.freeBytes,
         (unsigned long)endHeap.freeBytes,
         deltaFree,
         (unsigned long)endHeap.minFreeBytes,
         (unsigned long)lowWaterDrop,
         (unsigned long)meta.sizeBytes,
         meta.gzip ? 1U : 0U);
}
#endif

const char* webAssetVersion_()
{
    return FirmwareVersion::BuildRef;
}

void addNoCacheHeaders_(AsyncWebServerResponse* response)
{
    if (!response) return;
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
}

void addVersionedAssetCacheHeaders_(AsyncWebServerResponse* response)
{
    if (!response) return;
    response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
}

bool isCurrentWebAssetVersionRequest_(AsyncWebServerRequest* request)
{
    if (!request || !request->hasParam("v")) return false;
    const AsyncWebParameter* versionParam = request->getParam("v");
    if (!versionParam) return false;
    const char* currentVersion = webAssetVersion_();
    if (!currentVersion || currentVersion[0] == '\0') return false;
    const String& requestedVersion = versionParam->value();
    return requestedVersion.length() == strlen(currentVersion) &&
           strcmp(requestedVersion.c_str(), currentVersion) == 0;
}

void addCacheAwareAssetHeaders_(AsyncWebServerRequest* request, AsyncWebServerResponse* response)
{
    if (!request || !response) return;
    if (isCurrentWebAssetVersionRequest_(request)) {
        addVersionedAssetCacheHeaders_(response);
    } else {
        addNoCacheHeaders_(response);
    }
}

int flowCfgApplyHttpStatus_(const char* ackJson)
{
    if (!ackJson || ackJson[0] == '\0') return 500;
    if (strstr(ackJson, "\"code\":\"BadCfgJson\"")) return 400;
    if (strstr(ackJson, "\"code\":\"ArgsTooLarge\"") || strstr(ackJson, "\"code\":\"CfgTruncated\"")) return 413;
    if (strstr(ackJson, "\"code\":\"NotReady\"")) return 503;
    if (strstr(ackJson, "\"code\":\"CfgApplyFailed\"")) return 409;
    if (strstr(ackJson, "\"code\":\"IoError\"")) return 502;
    if (strstr(ackJson, "\"code\":\"Failed\"")) return 502;
    return 500;
}

bool parseFlowStatusDomainParam_(const char* raw, FlowStatusDomain& domainOut)
{
    if (!raw || raw[0] == '\0') return false;
    if (strcasecmp(raw, "system") == 0) {
        domainOut = FlowStatusDomain::System;
        return true;
    }
    if (strcasecmp(raw, "wifi") == 0) {
        domainOut = FlowStatusDomain::Wifi;
        return true;
    }
    if (strcasecmp(raw, "mqtt") == 0) {
        domainOut = FlowStatusDomain::Mqtt;
        return true;
    }
    if (strcasecmp(raw, "i2c") == 0) {
        domainOut = FlowStatusDomain::I2c;
        return true;
    }
    if (strcasecmp(raw, "pool") == 0) {
        domainOut = FlowStatusDomain::Pool;
        return true;
    }
    return false;
}

const char* httpMethodName_(uint32_t method)
{
    switch (method) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "OTHER";
    }
}

const char* runtimeUiWireTypeName_(RuntimeUiWireType type)
{
    switch (type) {
    case RuntimeUiWireType::NotFound: return "not_found";
    case RuntimeUiWireType::Unavailable: return "unavailable";
    case RuntimeUiWireType::Bool: return "bool";
    case RuntimeUiWireType::Int32: return "int32";
    case RuntimeUiWireType::UInt32: return "uint32";
    case RuntimeUiWireType::Float32: return "float";
    case RuntimeUiWireType::Enum: return "enum";
    case RuntimeUiWireType::String: return "string";
    default: return "unknown";
    }
}

size_t runtimeUiWireEstimate_(const RuntimeUiManifestItem* item)
{
    if (!item || !item->type) return 20U;
    if (strcmp(item->type, "bool") == 0) return 4U;
    if (strcmp(item->type, "enum") == 0) return 4U;
    if (strcmp(item->type, "int32") == 0) return 7U;
    if (strcmp(item->type, "uint32") == 0) return 7U;
    if (strcmp(item->type, "float") == 0) return 7U;
    if (strcmp(item->type, "string") == 0) {
        if (strcmp(item->key, "mqtt.server") == 0) return 72U;
        return 24U;
    }
    return 20U;
}

uint32_t readLe32_(const uint8_t* in)
{
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

bool appendRuntimeUiJsonValues_(JsonArray values, const uint8_t* payload, size_t payloadLen)
{
    if (!payload || payloadLen == 0U) return true;

    size_t offset = 0U;
    const uint8_t count = payload[offset++];
    for (uint8_t i = 0; i < count; ++i) {
        if ((offset + 3U) > payloadLen) return false;
        const RuntimeUiId runtimeId = (RuntimeUiId)((RuntimeUiId)payload[offset] |
                                                    ((RuntimeUiId)payload[offset + 1U] << 8));
        offset += 2U;
        const RuntimeUiWireType wireType = (RuntimeUiWireType)payload[offset++];
        const RuntimeUiManifestItem* manifestItem = findRuntimeUiManifestItem(runtimeId);

        JsonObject value = values.createNestedObject();
        value["id"] = runtimeId;
        if (manifestItem) {
            value["key"] = manifestItem->key;
            value["type"] = manifestItem->type;
            if (manifestItem->unit && manifestItem->unit[0] != '\0') {
                value["unit"] = manifestItem->unit;
            }
        } else {
            value["type"] = runtimeUiWireTypeName_(wireType);
        }

        switch (wireType) {
        case RuntimeUiWireType::NotFound:
            value["status"] = "not_found";
            break;

        case RuntimeUiWireType::Unavailable:
            value["status"] = "unavailable";
            break;

        case RuntimeUiWireType::Bool:
            if ((offset + 1U) > payloadLen) return false;
            value["value"] = (payload[offset++] != 0U);
            break;

        case RuntimeUiWireType::Int32: {
            if ((offset + 4U) > payloadLen) return false;
            int32_t raw = 0;
            const uint32_t bits = readLe32_(payload + offset);
            memcpy(&raw, &bits, sizeof(raw));
            value["value"] = raw;
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::UInt32:
            if ((offset + 4U) > payloadLen) return false;
            value["value"] = readLe32_(payload + offset);
            offset += 4U;
            break;

        case RuntimeUiWireType::Float32: {
            if ((offset + 4U) > payloadLen) return false;
            const uint32_t bits = readLe32_(payload + offset);
            float raw = 0.0f;
            memcpy(&raw, &bits, sizeof(raw));
            value["value"] = raw;
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::Enum:
            if ((offset + 1U) > payloadLen) return false;
            value["value"] = payload[offset++];
            break;

        case RuntimeUiWireType::String: {
            if ((offset + 1U) > payloadLen) return false;
            const uint8_t len = payload[offset++];
            if ((offset + len) > payloadLen) return false;
            char text[I2cCfgProtocol::MaxPayload + 1U] = {0};
            memcpy(text, payload + offset, len);
            text[len] = '\0';
            value["value"] = text;
            offset += len;
            break;
        }

        default:
            return false;
        }
    }

    return offset == payloadLen;
}

bool appendRuntimeUiJsonValuesToStream_(Print& out, const uint8_t* payload, size_t payloadLen, bool& firstValue)
{
    if (!payload || payloadLen == 0U) return true;

    size_t offset = 0U;
    const uint8_t count = payload[offset++];
    for (uint8_t i = 0; i < count; ++i) {
        if ((offset + 3U) > payloadLen) return false;
        const RuntimeUiId runtimeId = (RuntimeUiId)((RuntimeUiId)payload[offset] |
                                                    ((RuntimeUiId)payload[offset + 1U] << 8));
        offset += 2U;
        const RuntimeUiWireType wireType = (RuntimeUiWireType)payload[offset++];
        const RuntimeUiManifestItem* manifestItem = findRuntimeUiManifestItem(runtimeId);

        if (!firstValue) out.print(',');
        firstValue = false;

        out.print('{');
        out.print("\"id\":");
        out.print((unsigned)runtimeId);
        out.print(",\"key\":");
        printJsonEscaped_(out, manifestItem ? manifestItem->key : "");
        out.print(",\"type\":");
        printJsonEscaped_(out, manifestItem ? manifestItem->type : runtimeUiWireTypeName_(wireType));
        if (manifestItem && manifestItem->unit && manifestItem->unit[0] != '\0') {
            out.print(",\"unit\":");
            printJsonEscaped_(out, manifestItem->unit);
        }

        switch (wireType) {
        case RuntimeUiWireType::NotFound:
            out.print(",\"status\":\"not_found\"}");
            break;

        case RuntimeUiWireType::Unavailable:
            out.print(",\"status\":\"unavailable\"}");
            break;

        case RuntimeUiWireType::Bool:
            if ((offset + 1U) > payloadLen) return false;
            out.print(",\"value\":");
            out.print((payload[offset++] != 0U) ? "true" : "false");
            out.print('}');
            break;

        case RuntimeUiWireType::Int32: {
            if ((offset + 4U) > payloadLen) return false;
            int32_t raw = 0;
            const uint32_t bits = readLe32_(payload + offset);
            memcpy(&raw, &bits, sizeof(raw));
            out.print(",\"value\":");
            out.print((int32_t)raw);
            out.print('}');
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::UInt32:
            if ((offset + 4U) > payloadLen) return false;
            out.print(",\"value\":");
            out.print((unsigned long)readLe32_(payload + offset));
            out.print('}');
            offset += 4U;
            break;

        case RuntimeUiWireType::Float32: {
            if ((offset + 4U) > payloadLen) return false;
            const uint32_t bits = readLe32_(payload + offset);
            float raw = 0.0f;
            memcpy(&raw, &bits, sizeof(raw));
            out.print(",\"value\":");
            out.print(raw, 3);
            out.print('}');
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::Enum:
            if ((offset + 1U) > payloadLen) return false;
            out.print(",\"value\":");
            out.print((unsigned)payload[offset++]);
            out.print('}');
            break;

        case RuntimeUiWireType::String: {
            if ((offset + 1U) > payloadLen) return false;
            const uint8_t len = payload[offset++];
            if ((offset + len) > payloadLen) return false;
            char text[I2cCfgProtocol::MaxPayload + 1U] = {0};
            memcpy(text, payload + offset, len);
            text[len] = '\0';
            out.print(",\"value\":");
            printJsonEscaped_(out, text);
            out.print('}');
            offset += len;
            break;
        }

        default:
            return false;
        }
    }

    return offset == payloadLen;
}

constexpr size_t kMaxRuntimeHttpIds = 48U;

#if defined(FLOW_PROFILE_MICRONOVA)
constexpr RuntimeUiId kMicronovaRuntimeIdOnline = makeRuntimeUiId(ModuleId::MicronovaBoiler, 1);
constexpr RuntimeUiId kMicronovaRuntimeIdStateCode = makeRuntimeUiId(ModuleId::MicronovaBoiler, 2);
constexpr RuntimeUiId kMicronovaRuntimeIdStateText = makeRuntimeUiId(ModuleId::MicronovaBoiler, 3);
constexpr RuntimeUiId kMicronovaRuntimeIdPowerState = makeRuntimeUiId(ModuleId::MicronovaBoiler, 4);
constexpr RuntimeUiId kMicronovaRuntimeIdPowerLevel = makeRuntimeUiId(ModuleId::MicronovaBoiler, 5);
constexpr RuntimeUiId kMicronovaRuntimeIdFanSpeed = makeRuntimeUiId(ModuleId::MicronovaBoiler, 6);
constexpr RuntimeUiId kMicronovaRuntimeIdTargetTemp = makeRuntimeUiId(ModuleId::MicronovaBoiler, 7);
constexpr RuntimeUiId kMicronovaRuntimeIdRoomTemp = makeRuntimeUiId(ModuleId::MicronovaBoiler, 8);
constexpr RuntimeUiId kMicronovaRuntimeIdFumesTemp = makeRuntimeUiId(ModuleId::MicronovaBoiler, 9);
constexpr RuntimeUiId kMicronovaRuntimeIdWaterTemp = makeRuntimeUiId(ModuleId::MicronovaBoiler, 10);
constexpr RuntimeUiId kMicronovaRuntimeIdWaterPressure = makeRuntimeUiId(ModuleId::MicronovaBoiler, 11);
constexpr RuntimeUiId kMicronovaRuntimeIdAlarmCode = makeRuntimeUiId(ModuleId::MicronovaBoiler, 12);
constexpr RuntimeUiId kMicronovaRuntimeIdLastUpdateMs = makeRuntimeUiId(ModuleId::MicronovaBoiler, 13);
constexpr RuntimeUiId kMicronovaRuntimeIdLastCommand = makeRuntimeUiId(ModuleId::MicronovaBoiler, 14);

const char kMicronovaRuntimeManifestJson[] PROGMEM = R"json({
  "ok": true,
  "values": [
    {"id":2901,"runtimeId":2901,"moduleId":29,"module":"micronova.boiler","valueId":1,"key":"micronova.online","label":"Connexion chaudière","type":"bool","domain":"micronova","group":"Chaudière","unit":null,"decimals":null,"order":10,"display":"boolean"},
    {"id":2902,"runtimeId":2902,"moduleId":29,"module":"micronova.boiler","valueId":2,"key":"micronova.stove_state_code","label":"Code état","type":"int32","domain":"micronova","group":"Chaudière","unit":null,"decimals":0,"order":20},
    {"id":2903,"runtimeId":2903,"moduleId":29,"module":"micronova.boiler","valueId":3,"key":"micronova.stove_state_text","label":"État","type":"string","domain":"micronova","group":"Chaudière","unit":null,"decimals":null,"order":30,"display":"badge"},
    {"id":2904,"runtimeId":2904,"moduleId":29,"module":"micronova.boiler","valueId":4,"key":"micronova.power_state","label":"Marche","type":"string","domain":"micronova","group":"Chaudière","unit":null,"decimals":null,"order":40,"display":"badge"},
    {"id":2905,"runtimeId":2905,"moduleId":29,"module":"micronova.boiler","valueId":5,"key":"micronova.power_level","label":"Puissance","type":"int32","domain":"micronova","group":"Commandes","unit":null,"decimals":0,"order":50},
    {"id":2906,"runtimeId":2906,"moduleId":29,"module":"micronova.boiler","valueId":6,"key":"micronova.fan_speed","label":"Ventilation","type":"int32","domain":"micronova","group":"Commandes","unit":null,"decimals":0,"order":60},
    {"id":2907,"runtimeId":2907,"moduleId":29,"module":"micronova.boiler","valueId":7,"key":"micronova.target_temperature","label":"Consigne","type":"int32","domain":"micronova","group":"Températures","unit":"°C","decimals":0,"order":70},
    {"id":2908,"runtimeId":2908,"moduleId":29,"module":"micronova.boiler","valueId":8,"key":"micronova.room_temperature","label":"Température ambiante","type":"float","domain":"micronova","group":"Températures","unit":"°C","decimals":1,"order":80},
    {"id":2909,"runtimeId":2909,"moduleId":29,"module":"micronova.boiler","valueId":9,"key":"micronova.fumes_temperature","label":"Température fumées","type":"float","domain":"micronova","group":"Températures","unit":"°C","decimals":1,"order":90},
    {"id":2910,"runtimeId":2910,"moduleId":29,"module":"micronova.boiler","valueId":10,"key":"micronova.water_temperature","label":"Température eau","type":"float","domain":"micronova","group":"Hydraulique","unit":"°C","decimals":1,"order":100},
    {"id":2911,"runtimeId":2911,"moduleId":29,"module":"micronova.boiler","valueId":11,"key":"micronova.water_pressure","label":"Pression eau","type":"float","domain":"micronova","group":"Hydraulique","unit":"bar","decimals":2,"order":110},
    {"id":2912,"runtimeId":2912,"moduleId":29,"module":"micronova.boiler","valueId":12,"key":"micronova.alarm_code","label":"Code alarme","type":"int32","domain":"micronova","group":"Alarmes","unit":null,"decimals":0,"order":120},
    {"id":2913,"runtimeId":2913,"moduleId":29,"module":"micronova.boiler","valueId":13,"key":"micronova.last_update_ms","label":"Dernière lecture","type":"uint32","domain":"micronova","group":"Chaudière","unit":"ms","decimals":0,"order":130},
    {"id":2914,"runtimeId":2914,"moduleId":29,"module":"micronova.boiler","valueId":14,"key":"micronova.last_command","label":"Dernière commande","type":"string","domain":"micronova","group":"Commandes","unit":null,"decimals":null,"order":140,"display":"badge"},
    {"id":2001,"runtimeId":2001,"moduleId":20,"module":"mqtt","valueId":1,"key":"mqtt.ready","label":"MQTT connecté","type":"bool","domain":"mqtt","group":"MQTT","unit":null,"decimals":null,"order":10,"display":"boolean"},
    {"id":2003,"runtimeId":2003,"moduleId":20,"module":"mqtt","valueId":3,"key":"mqtt.rx_drop","label":"Messages ignorés","type":"uint32","domain":"mqtt","group":"MQTT","unit":null,"decimals":0,"order":30},
    {"id":2004,"runtimeId":2004,"moduleId":20,"module":"mqtt","valueId":4,"key":"mqtt.parse_fail","label":"Payloads invalides","type":"uint32","domain":"mqtt","group":"MQTT","unit":null,"decimals":0,"order":40},
    {"id":2005,"runtimeId":2005,"moduleId":20,"module":"mqtt","valueId":5,"key":"mqtt.handler_fail","label":"Commandes refusées","type":"uint32","domain":"mqtt","group":"MQTT","unit":null,"decimals":0,"order":50},
    {"id":2006,"runtimeId":2006,"moduleId":20,"module":"mqtt","valueId":6,"key":"mqtt.oversize_drop","label":"Messages trop grands","type":"uint32","domain":"mqtt","group":"MQTT","unit":null,"decimals":0,"order":60},
    {"id":1701,"runtimeId":1701,"moduleId":17,"module":"system","valueId":1,"key":"system.firmware","label":"Firmware","type":"string","domain":"system","group":"Système","unit":null,"decimals":null,"order":10},
    {"id":1702,"runtimeId":1702,"moduleId":17,"module":"system","valueId":2,"key":"system.uptime_ms","label":"Uptime","type":"uint32","domain":"system","group":"Système","unit":"ms","decimals":0,"order":20},
    {"id":1703,"runtimeId":1703,"moduleId":17,"module":"system","valueId":3,"key":"system.heap_free","label":"Heap libre","type":"uint32","domain":"system","group":"Mémoire","unit":"B","decimals":0,"order":30},
    {"id":1704,"runtimeId":1704,"moduleId":17,"module":"system","valueId":4,"key":"system.heap_min_free","label":"Heap minimum","type":"uint32","domain":"system","group":"Mémoire","unit":"B","decimals":0,"order":40},
    {"id":1001,"runtimeId":1001,"moduleId":10,"module":"wifi","valueId":1,"key":"wifi.ready","label":"WiFi connecté","type":"bool","domain":"wifi","group":"WiFi","unit":null,"decimals":null,"order":10,"display":"boolean"},
    {"id":1002,"runtimeId":1002,"moduleId":10,"module":"wifi","valueId":2,"key":"wifi.ip","label":"Adresse IP","type":"string","domain":"wifi","group":"WiFi","unit":null,"decimals":null,"order":20},
    {"id":1003,"runtimeId":1003,"moduleId":10,"module":"wifi","valueId":3,"key":"wifi.rssi","label":"RSSI","type":"int32","domain":"wifi","group":"WiFi","unit":"dBm","decimals":0,"order":30}
  ]
})json";

void printRuntimeValuePrefix_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, const char* type, const char* unit)
{
    if (!firstValue) out.print(',');
    firstValue = false;
    out.print("{\"id\":");
    out.print((unsigned)id);
    out.print(",\"key\":");
    printJsonEscaped_(out, key);
    out.print(",\"type\":");
    printJsonEscaped_(out, type);
    if (unit && unit[0] != '\0') {
        out.print(",\"unit\":");
        printJsonEscaped_(out, unit);
    }
}

void printRuntimeBool_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, bool value)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "bool", nullptr);
    out.print(",\"value\":");
    out.print(value ? "true" : "false");
    out.print('}');
}

void printRuntimeI32_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, int32_t value, const char* unit = nullptr)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "int32", unit);
    out.print(",\"value\":");
    out.print((int32_t)value);
    out.print('}');
}

void printRuntimeU32_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, uint32_t value, const char* unit = nullptr)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "uint32", unit);
    out.print(",\"value\":");
    out.print((unsigned long)value);
    out.print('}');
}

void printRuntimeF32_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, float value, const char* unit = nullptr)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "float", unit);
    out.print(",\"value\":");
    out.print(value, 3);
    out.print('}');
}

void printRuntimeString_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, const char* value)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, "string", nullptr);
    out.print(",\"value\":");
    printJsonEscaped_(out, value ? value : "");
    out.print('}');
}

void printRuntimeUnavailable_(Print& out, bool& firstValue, RuntimeUiId id, const char* key, const char* type)
{
    printRuntimeValuePrefix_(out, firstValue, id, key, type, nullptr);
    out.print(",\"status\":\"unavailable\"}");
}

bool appendMicronovaLocalRuntimeValue_(Print& out, DataStore* dataStore, RuntimeUiId id, bool& firstValue)
{
    if (!dataStore) return false;
    const RuntimeData& rt = dataStore->data();
    const MicronovaRuntimeData& mn = rt.micronova;
    switch (id) {
        case kMicronovaRuntimeIdOnline: printRuntimeBool_(out, firstValue, id, "micronova.online", mn.online); return true;
        case kMicronovaRuntimeIdStateCode: printRuntimeI32_(out, firstValue, id, "micronova.stove_state_code", mn.stoveStateCode); return true;
        case kMicronovaRuntimeIdStateText: printRuntimeString_(out, firstValue, id, "micronova.stove_state_text", mn.stoveStateText); return true;
        case kMicronovaRuntimeIdPowerState: printRuntimeString_(out, firstValue, id, "micronova.power_state", mn.powerState); return true;
        case kMicronovaRuntimeIdPowerLevel: printRuntimeI32_(out, firstValue, id, "micronova.power_level", mn.powerLevel); return true;
        case kMicronovaRuntimeIdFanSpeed: printRuntimeI32_(out, firstValue, id, "micronova.fan_speed", mn.fanSpeed); return true;
        case kMicronovaRuntimeIdTargetTemp: printRuntimeI32_(out, firstValue, id, "micronova.target_temperature", mn.targetTemperature, "\xC2\xB0""C"); return true;
        case kMicronovaRuntimeIdRoomTemp: printRuntimeF32_(out, firstValue, id, "micronova.room_temperature", mn.roomTemperature, "\xC2\xB0""C"); return true;
        case kMicronovaRuntimeIdFumesTemp: printRuntimeF32_(out, firstValue, id, "micronova.fumes_temperature", mn.fumesTemperature, "\xC2\xB0""C"); return true;
        case kMicronovaRuntimeIdWaterTemp: printRuntimeF32_(out, firstValue, id, "micronova.water_temperature", mn.waterTemperature, "\xC2\xB0""C"); return true;
        case kMicronovaRuntimeIdWaterPressure: printRuntimeF32_(out, firstValue, id, "micronova.water_pressure", mn.waterPressure, "bar"); return true;
        case kMicronovaRuntimeIdAlarmCode: printRuntimeI32_(out, firstValue, id, "micronova.alarm_code", mn.alarmCode); return true;
        case kMicronovaRuntimeIdLastUpdateMs: printRuntimeU32_(out, firstValue, id, "micronova.last_update_ms", mn.lastUpdateMs, "ms"); return true;
        case kMicronovaRuntimeIdLastCommand: printRuntimeString_(out, firstValue, id, "micronova.last_command", mn.lastCommand); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 1): printRuntimeBool_(out, firstValue, id, "mqtt.ready", mqttReady(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 3): printRuntimeU32_(out, firstValue, id, "mqtt.rx_drop", mqttRxDrop(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 4): printRuntimeU32_(out, firstValue, id, "mqtt.parse_fail", mqttParseFail(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 5): printRuntimeU32_(out, firstValue, id, "mqtt.handler_fail", mqttHandlerFail(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Mqtt, 6): printRuntimeU32_(out, firstValue, id, "mqtt.oversize_drop", mqttOversizeDrop(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::System, 1): printRuntimeString_(out, firstValue, id, "system.firmware", FirmwareVersion::Full); return true;
        case makeRuntimeUiId(ModuleId::System, 2): {
            SystemStatsSnapshot snap{};
            SystemStats::collect(snap);
            printRuntimeU32_(out, firstValue, id, "system.uptime_ms", snap.uptimeMs, "ms");
            return true;
        }
        case makeRuntimeUiId(ModuleId::System, 3): {
            SystemStatsSnapshot snap{};
            SystemStats::collect(snap);
            printRuntimeU32_(out, firstValue, id, "system.heap_free", snap.heap.freeBytes, "B");
            return true;
        }
        case makeRuntimeUiId(ModuleId::System, 4): {
            SystemStatsSnapshot snap{};
            SystemStats::collect(snap);
            printRuntimeU32_(out, firstValue, id, "system.heap_min_free", snap.heap.minFreeBytes, "B");
            return true;
        }
        case makeRuntimeUiId(ModuleId::Wifi, 1): printRuntimeBool_(out, firstValue, id, "wifi.ready", wifiReady(*dataStore)); return true;
        case makeRuntimeUiId(ModuleId::Wifi, 2): {
            const IpV4 ip = wifiIp(*dataStore);
            char ipText[16] = {0};
            snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", (unsigned)ip.b[0], (unsigned)ip.b[1], (unsigned)ip.b[2], (unsigned)ip.b[3]);
            printRuntimeString_(out, firstValue, id, "wifi.ip", ipText);
            return true;
        }
        case makeRuntimeUiId(ModuleId::Wifi, 3): printRuntimeI32_(out, firstValue, id, "wifi.rssi", WiFi.isConnected() ? (int32_t)WiFi.RSSI() : 0, "dBm"); return true;
        default:
            printRuntimeUnavailable_(out, firstValue, id, "", "unknown");
            return true;
    }
}

void sendMicronovaLocalRuntimeValuesResponse_(AsyncWebServerRequest* request,
                                              DataStore* dataStore,
                                              const RuntimeUiId* ids,
                                              size_t idCount)
{
    if (!request || !dataStore) {
        if (request) request->send(503, "application/json", "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.values.local\"}}");
        return;
    }
    if (!ids || idCount == 0U) {
        request->send(400, "application/json", "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
        return;
    }
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    addNoCacheHeaders_(response);
    response->print("{\"ok\":true,\"values\":[");
    bool firstValue = true;
    for (size_t i = 0U; i < idCount; ++i) {
        (void)appendMicronovaLocalRuntimeValue_(*response, dataStore, ids[i], firstValue);
    }
    response->print("]}");
    request->send(response);
}
#endif

bool parseRuntimeUiIdsCsv_(const char* raw, RuntimeUiId* idsOut, size_t capacity, size_t& countOut)
{
    countOut = 0U;
    if (!raw || !idsOut || capacity == 0U) return false;

    uint32_t current = 0U;
    bool hasDigit = false;

    auto flushCurrent = [&]() -> bool {
        if (!hasDigit) return true;
        if (countOut >= capacity || current == 0U || current > 65535U) return false;
        idsOut[countOut++] = (RuntimeUiId)current;
        current = 0U;
        hasDigit = false;
        return true;
    };

    for (const char* p = raw; *p != '\0'; ++p) {
        const char ch = *p;
        if (ch >= '0' && ch <= '9') {
            hasDigit = true;
            current = (current * 10U) + (uint32_t)(ch - '0');
            if (current > 65535U) return false;
            continue;
        }
        if (ch == ',' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (ch == ',') {
                if (!flushCurrent()) return false;
            }
            continue;
        }
        return false;
    }

    return flushCurrent() && countOut > 0U;
}

void sendRuntimeUiValuesResponse_(AsyncWebServerRequest* request,
                                  const FlowCfgRemoteService* flowCfgSvc,
                                  const RuntimeUiId* ids,
                                  size_t idCount)
{
    if (!request || !flowCfgSvc || !flowCfgSvc->runtimeUiValues) {
        if (request) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.values\"}}");
        }
        return;
    }
    if (flowCfgSvc->isReady && !flowCfgSvc->isReady(flowCfgSvc->ctx)) {
        request->send(503, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.values.link\"}}");
        return;
    }
    if (!ids || idCount == 0U) {
        request->send(400, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
        return;
    }

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    addNoCacheHeaders_(response);
    response->print("{\"ok\":true,\"values\":[");
    bool firstValue = true;

    size_t start = 0U;
    while (start < idCount) {
        size_t batchCount = 0U;
        size_t batchBudget = 1U;  // record count byte
        while ((start + batchCount) < idCount) {
            const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(ids[start + batchCount]);
            const bool isString = item && item->type && strcmp(item->type, "string") == 0;
            const size_t estimate = runtimeUiWireEstimate_(item);

            if (batchCount > 0U && (isString || (batchBudget + estimate) > I2cCfgProtocol::MaxPayload)) {
                break;
            }
            batchBudget += estimate;
            ++batchCount;
            if (isString) break;
        }
        if (batchCount == 0U) batchCount = 1U;

        uint8_t payload[I2cCfgProtocol::MaxPayload] = {0};
        size_t written = 0U;
        if (!flowCfgSvc->runtimeUiValues(flowCfgSvc->ctx,
                                         ids + start,
                                         (uint8_t)batchCount,
                                         payload,
                                         sizeof(payload),
                                         &written)) {
            delete response;
            request->send(502, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"runtime.values.fetch\"}}");
            return;
        }
        if (!appendRuntimeUiJsonValuesToStream_(*response, payload, written, firstValue)) {
            delete response;
            request->send(502, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"runtime.values.decode\"}}");
            return;
        }
        start += batchCount;
    }

    response->print("]}");
    request->send(response);
}

bool dashboardSlotDegreeCUnit_(const char* unit)
{
    if (!unit || unit[0] == '\0') return false;
    if ((uint8_t)unit[0] == 0xB0 && unit[1] == 'C' && unit[2] == '\0') return true;
    return (uint8_t)unit[0] == 0xC2 && (uint8_t)unit[1] == 0xB0 && unit[2] == 'C' && unit[3] == '\0';
}

#if !defined(FLOW_PROFILE_MICRONOVA)
void dashboardSlotBgColorHex_(uint16_t color565, char* out, size_t outLen)
{
    if (!out || outLen < 8U) return;
    const uint8_t r5 = (uint8_t)((color565 >> 11) & 0x1FU);
    const uint8_t g6 = (uint8_t)((color565 >> 5) & 0x3FU);
    const uint8_t b5 = (uint8_t)(color565 & 0x1FU);
    const uint8_t r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
    const uint8_t g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
    const uint8_t b8 = (uint8_t)((b5 << 3) | (b5 >> 2));
    snprintf(out, outLen, "#%02X%02X%02X", (unsigned)r8, (unsigned)g8, (unsigned)b8);
}

uint8_t dashboardSlotDecimals_(const FlowRemoteDashboardSlotRuntime& slot)
{
    if ((RuntimeUiWireType)slot.wireType != RuntimeUiWireType::Float32) return 0U;
    if (slot.runtimeUiId == makeRuntimeUiId(ModuleId::Io, 3)) return 2U;
    if (slot.runtimeUiId == makeRuntimeUiId(ModuleId::Io, 6)) return 2U;
    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(slot.runtimeUiId);
    const char* unit = (item && item->unit) ? item->unit : "";
    if (unit && strcmp(unit, "mV") == 0) return 0U;
    return 1U;
}

void trimDashboardSlotFloat_(char* text)
{
    if (!text) return;
    char* dot = strchr(text, '.');
    if (!dot) return;
    char* end = text + strlen(text);
    while (end > dot && end[-1] == '0') --end;
    if (end > dot && end[-1] == '.') --end;
    *end = '\0';
    if (strcmp(text, "-0") == 0) snprintf(text, 4, "0");
}

void formatDashboardSlotValueText_(const FlowRemoteDashboardSlotRuntime& slot, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return;
    if (!slot.enabled || !slot.available) {
        snprintf(out, outLen, "Indisponible");
        return;
    }

    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(slot.runtimeUiId);
    const char* unit = (item && item->unit) ? item->unit : "";

    switch ((RuntimeUiWireType)slot.wireType) {
        case RuntimeUiWireType::Bool:
            snprintf(out, outLen, "%s", slot.boolValue ? "Actif" : "Arret");
            return;
        case RuntimeUiWireType::Enum:
            snprintf(out, outLen, "%u", (unsigned)slot.enumValue);
            return;
        case RuntimeUiWireType::Int32:
            if (unit && unit[0] != '\0') {
                snprintf(out, outLen, "%ld %s", (long)slot.i32Value, unit);
            } else {
                snprintf(out, outLen, "%ld", (long)slot.i32Value);
            }
            return;
        case RuntimeUiWireType::UInt32:
            if (unit && unit[0] != '\0') {
                snprintf(out, outLen, "%lu %s", (unsigned long)slot.u32Value, unit);
            } else {
                snprintf(out, outLen, "%lu", (unsigned long)slot.u32Value);
            }
            return;
        case RuntimeUiWireType::Float32: {
            char numberBuf[24] = {0};
            const uint8_t decimals = dashboardSlotDecimals_(slot);
            if (decimals > 0U) {
                snprintf(numberBuf, sizeof(numberBuf), "%.*f", (int)decimals, (double)slot.f32Value);
                trimDashboardSlotFloat_(numberBuf);
            } else {
                snprintf(numberBuf, sizeof(numberBuf), "%ld", lroundf(slot.f32Value));
            }
            if (unit && unit[0] != '\0') {
                if (dashboardSlotDegreeCUnit_(unit)) {
                    snprintf(out, outLen, "%s %s", numberBuf, "\xC2\xB0""C");
                } else {
                    snprintf(out, outLen, "%s %s", numberBuf, unit);
                }
            } else {
                snprintf(out, outLen, "%s", numberBuf);
            }
            return;
        }
        case RuntimeUiWireType::NotFound:
        case RuntimeUiWireType::Unavailable:
        case RuntimeUiWireType::String:
        default:
            snprintf(out, outLen, "Indisponible");
            return;
    }
}
#endif

struct HttpLatencyScope {
    AsyncWebServerRequest* req;
    const char* route;
    uint32_t startUs;
    uint32_t infoMs;
    uint32_t warnMs;
#if FLOW_WEB_HEAP_FORENSICS
    HeapForensicSnapshot startHeap;
#endif

    HttpLatencyScope(AsyncWebServerRequest* request,
                     const char* routePath,
                     uint32_t infoThresholdMs = kHttpLatencyInfoMs,
                     uint32_t warnThresholdMs = kHttpLatencyWarnMs)
        : req(request),
          route(routePath),
          startUs(micros()),
          infoMs(infoThresholdMs),
          warnMs((warnThresholdMs > infoThresholdMs) ? warnThresholdMs : (infoThresholdMs + 1U))
    {
        if (gHttpActivityHook) {
            gHttpActivityHook(gHttpActivityHookCtx);
        }
#if FLOW_WEB_HEAP_FORENSICS
        startHeap = captureHeapForensicSnapshot_();
#endif
    }

    ~HttpLatencyScope()
    {
#if FLOW_WEB_HEAP_FORENSICS
        logHttpHeapForensic_(req, route, startUs, startHeap);
#endif
        const uint32_t elapsedUs = micros() - startUs;
        const uint32_t elapsedMs = elapsedUs / 1000U;
        if (elapsedMs < infoMs) return;

        const char* method = req ? httpMethodName_(req->method()) : "?";
        const uint32_t heapFree = (uint32_t)ESP.getFreeHeap();
        const uint32_t heapLargest = heapFree;
        if (elapsedMs >= warnMs) {
            LOGW("HTTP slow %s %s latency=%lums heap=%lu largest=%lu",
                 method,
                 route ? route : "?",
                 (unsigned long)elapsedMs,
                 (unsigned long)heapFree,
                 (unsigned long)heapLargest);
        } else {
            LOGI("HTTP %s %s latency=%lums heap=%lu largest=%lu",
                 method,
                 route ? route : "?",
                 (unsigned long)elapsedMs,
                 (unsigned long)heapFree,
                 (unsigned long)heapLargest);
        }
    }
};

const UartSpec* webBridgeUartSpec_(const BoardSpec& board)
{
    return boardFindUart(board, "bridge");
}
} // namespace

static const char kWebInterfaceFallbackPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head><meta charset="utf-8" /><meta name="viewport" content="width=device-width, initial-scale=1" /><title>Superviseur Flow.io</title></head>
<body style="font-family:Arial,sans-serif;background:#0B1F3A;color:#FFFFFF;padding:16px;">
<h1>Superviseur Flow.io</h1>
<p>Interface web indisponible (fichiers SPIFFS manquants).</p>
<p>Veuillez charger SPIFFS puis recharger cette page.</p>
</body></html>
)HTML";

static const char kWebSerialLogPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Micronova - Logs Locaux</title>
<style>
  :root { color-scheme: dark; }
  html, body { margin: 0; padding: 0; background: #091220; color: #dbeafe; font-family: "SFMono-Regular", Menlo, Monaco, Consolas, monospace; }
  .top { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 10px 12px; background: #0f1b2f; border-bottom: 1px solid #1d304f; }
  .status { font-size: 12px; opacity: 0.9; }
  .actions { display: flex; gap: 8px; }
  button, a { border: 1px solid #2b4b78; background: #142742; color: #dbeafe; text-decoration: none; border-radius: 6px; padding: 6px 10px; font-size: 12px; }
  button:hover, a:hover { background: #1d3963; cursor: pointer; }
  #out { white-space: pre-wrap; word-break: break-word; margin: 0; padding: 12px; height: calc(100vh - 56px); overflow: auto; font-size: 12px; line-height: 1.35; }
  .line { display: block; }
  .log-d { color: #93c5fd; }
  .log-i { color: #86efac; }
  .log-w { color: #fde68a; }
  .log-e { color: #fca5a5; font-weight: 600; }
  .log-t { color: #d8b4fe; }
  .ansi-red { color: #f87171; }
  .ansi-green { color: #4ade80; }
  .ansi-yellow { color: #facc15; }
  .ansi-blue { color: #60a5fa; }
  .ansi-magenta { color: #c084fc; }
  .ansi-cyan { color: #22d3ee; }
  .ansi-white { color: #e5e7eb; }
  .ansi-gray { color: #9ca3af; }
</style>
</head>
<body>
  <div class="top">
    <div class="status" id="status">Connexion...</div>
    <div class="actions">
      <button id="pause" type="button">Pause</button>
      <button id="clear" type="button">Clear</button>
      <a href="/webinterface">Retour UI</a>
    </div>
  </div>
  <pre id="out"></pre>
<script>
(() => {
  const out = document.getElementById('out');
  const status = document.getElementById('status');
  const pauseBtn = document.getElementById('pause');
  const clearBtn = document.getElementById('clear');
  let paused = false;
  let ws = null;

  const MAX_LINES = 1200;

  const levelClassFor = (line) => {
    if (line.includes("][E][")) return "log-e";
    if (line.includes("][W][")) return "log-w";
    if (line.includes("][I][")) return "log-i";
    if (line.includes("][D][")) return "log-d";
    if (line.includes("][T][")) return "log-t";
    return "";
  };

  const ansiClassForCode = (code) => {
    if (code === 31) return "ansi-red";
    if (code === 32) return "ansi-green";
    if (code === 33) return "ansi-yellow";
    if (code === 34) return "ansi-blue";
    if (code === 35) return "ansi-magenta";
    if (code === 36) return "ansi-cyan";
    if (code === 37) return "ansi-white";
    if (code === 90) return "ansi-gray";
    return "";
  };

  const appendChunk = (parent, text, cls) => {
    if (!text) return;
    const span = document.createElement("span");
    if (cls) span.className = cls;
    span.textContent = text;
    parent.appendChild(span);
  };

  const appendAnsiAware = (parent, line, baseClass) => {
    const re = /\x1b\[([0-9;]*)m/g;
    let cursor = 0;
    let activeAnsi = "";
    let found = false;
    let m;
    while ((m = re.exec(line)) !== null) {
      found = true;
      const start = m.index;
      appendChunk(parent, line.slice(cursor, start), activeAnsi || baseClass);
      const codes = (m[1] || "0").split(";");
      for (const codeText of codes) {
        const code = Number(codeText || "0");
        if (code === 0) {
          activeAnsi = "";
          continue;
        }
        const mapped = ansiClassForCode(code);
        if (mapped) activeAnsi = mapped;
      }
      cursor = re.lastIndex;
    }
    if (!found) {
      appendChunk(parent, line, baseClass);
      return;
    }
    appendChunk(parent, line.slice(cursor), activeAnsi || baseClass);
  };

  const trimLines = () => {
    while (out.childNodes.length > MAX_LINES) {
      out.removeChild(out.firstChild);
    }
  };

  const append = (line) => {
    if (paused) return;
    const row = document.createElement("span");
    row.className = "line";
    appendAnsiAware(row, line, levelClassFor(line));
    out.appendChild(row);
    trimLines();
    out.scrollTop = out.scrollHeight;
  };

  const open = () => {
    const scheme = location.protocol === "https:" ? "wss" : "ws";
    ws = new WebSocket(`${scheme}://${location.host}/wslog`);
    ws.onopen = () => { status.textContent = "Connecté à /wslog"; };
    ws.onmessage = (ev) => { append(String(ev.data || "")); };
    ws.onclose = () => {
      status.textContent = "Déconnecté, reconnexion...";
      setTimeout(open, 1200);
    };
    ws.onerror = () => {
      status.textContent = "Erreur WebSocket";
    };
  };

  pauseBtn.addEventListener('click', () => {
    paused = !paused;
    pauseBtn.textContent = paused ? "Reprendre" : "Pause";
  });
  clearBtn.addEventListener('click', () => { out.textContent = ""; });

  open();
})();
</script>
</body>
</html>
)HTML";

WebInterfaceModule::WebInterfaceModule(const BoardSpec& board)
{
    const UartSpec* uart = webBridgeUartSpec_(board);
    if (uart) {
        bridgeUartConfigured_ = true;
        uartBaud_ = uart->baud;
        uartRxPin_ = uart->rxPin;
        uartTxPin_ = uart->txPin;
    } else {
        bridgeUartConfigured_ = false;
    }
    provisioningDisableAfterConfigured_ = board.provisioning.disableAfterConfigured;
    provisioningRequireMqttForConfigured_ = board.provisioning.requireMqttForConfigured;
}

void WebInterfaceModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;

    services_ = &services;
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    logSinkReg_ = services.get<LogSinkRegistryService>(ServiceId::LogSinks);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    flowCfgSvc_ = services.get<FlowCfgRemoteService>(ServiceId::FlowCfg);
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    auto* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    fwUpdateSvc_ = services.get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &WebInterfaceModule::onEventStatic_, this);
    }

    if (!services.add(ServiceId::WebInterface, &webInterfaceSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::WebInterface));
    }

    netReady_ = dataStore_ ? wifiReady(*dataStore_) : false;

    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&healthMux_);
    health_.snapshotMs = nowMs;
    health_.lastLoopMs = nowMs;
    health_.lastHttpActivityMs = 0U;
    health_.lastWsActivityMs = 0U;
    health_.wsSerialClients = 0U;
    health_.wsLogClients = 0U;
    health_.started = started_;
    health_.paused = uartPaused_;
    portEXIT_CRITICAL(&healthMux_);

#if defined(FLOW_PROFILE_MICRONOVA)
    LOGI("WebInterface local runtime deferred (server deferred)");
#else
    startLocalRuntime_();
#endif
}

void WebInterfaceModule::onStart(ConfigStore&, ServiceRegistry&)
{
#if defined(FLOW_PROFILE_MICRONOVA)
    startLocalRuntime_();
#endif
}

void WebInterfaceModule::startLocalRuntime_()
{
    if (!localLogQueue_) {
        localLogQueue_ = xQueueCreate(kLocalLogQueueLen, kLocalLogLineMax);
        if (!localLogQueue_) {
            LOGW("WebInterface local log queue alloc failed");
        }
    }
    if (!localLogSinkRegistered_ && localLogQueue_ && logSinkReg_ && logSinkReg_->add) {
        const LogSinkService sink{&WebInterfaceModule::onLocalLogSinkWrite_, this};
        if (logSinkReg_->add(logSinkReg_->ctx, sink)) {
            localLogSinkRegistered_ = true;
        } else {
            LOGW("WebInterface local log sink registration failed");
        }
    }

    if (provisioningOnly_) {
        bridgeUartEnabled_ = false;
        LOGI("WebInterface local runtime disabled in provisioning-only mode (wslog only)");
        return;
    }

    if (!bridgeUartConfigured_) {
        bridgeUartEnabled_ = false;
        LOGI("WebInterface bridge UART disabled: no 'bridge' UART in BoardSpec (wslog only)");
        return;
    }

    uart_.setRxBufferSize(kUartRxBufferSize);
    uart_.begin(uartBaud_, SERIAL_8N1, uartRxPin_, uartTxPin_);
    bridgeUartEnabled_ = true;

    LOGI("WebInterface local runtime uart=Serial2 baud=%lu rx=%d tx=%d line_buf=%u rx_buf=%u",
         (unsigned long)uartBaud_,
         uartRxPin_,
         uartTxPin_,
         (unsigned)kLineBufferSize,
         (unsigned)kUartRxBufferSize);
}

void WebInterfaceModule::startServer_()
{
    if (started_) return;
    gHttpActivityHook = &WebInterfaceModule::onHttpActivityHook_;
    gHttpActivityHookCtx = this;

    spiffsReady_ = SPIFFS.begin(false);
    if (!spiffsReady_) {
        LOGW("SPIFFS mount failed; web assets unavailable");
    } else {
        LOGI("SPIFFS mounted for web assets");
    }

    auto spiffsAssetExists = [this](const char* assetPath, const char* gzipOverridePath = nullptr) -> bool {
        if (!spiffsReady_ || !assetPath || assetPath[0] == '\0') return false;
        if (SPIFFS.exists(assetPath)) return true;
        if (gzipOverridePath && gzipOverridePath[0] != '\0') {
            return SPIFFS.exists(gzipOverridePath);
        }
        char gzipPath[128] = {0};
        const int gzipPathLen = snprintf(gzipPath, sizeof(gzipPath), "%s.gz", assetPath);
        return (gzipPathLen > 0) && ((size_t)gzipPathLen < sizeof(gzipPath)) && SPIFFS.exists(gzipPath);
    };

    auto beginSpiffsAssetResponse =
        [this](AsyncWebServerRequest* request,
               const char* assetPath,
               const char* contentType,
               bool cacheAware,
               const char* gzipOverridePath = nullptr,
               SpiffsAssetForensicMeta* forensicMeta = nullptr,
               bool* heapRejected = nullptr) -> AsyncWebServerResponse* {
        if (!request || !assetPath || !contentType || !spiffsReady_) return nullptr;
        if (heapRejected) *heapRejected = false;

        const size_t assetPathLen = strlen(assetPath);
        if (assetPathLen == 0U || assetPathLen >= 112U) return nullptr;

        uint32_t freeBytes = 0U;
        if (shouldRejectAssetByFreeHeap_(assetPath, &freeBytes)) {
            if (heapRejected) *heapRejected = true;
            LOGW("Web asset busy path=%s free=%lu",
                 assetPath,
                 (unsigned long)freeBytes);
            return nullptr;
        }

#if FLOW_WEB_HEAP_FORENSICS
        const uint32_t forensicStartUs = micros();
        const HeapForensicSnapshot forensicStartHeap = captureHeapForensicSnapshot_();
        SpiffsAssetForensicMeta localMeta{};
#endif

        char gzipPath[128] = {0};
        const char* servedPath = assetPath;
        bool hasGzip = false;
        if (gzipOverridePath && gzipOverridePath[0] != '\0') {
            if (SPIFFS.exists(gzipOverridePath)) {
                servedPath = gzipOverridePath;
                hasGzip = true;
            }
        } else {
            const int gzipPathLen = snprintf(gzipPath, sizeof(gzipPath), "%s.gz", assetPath);
            if ((gzipPathLen > 0) && ((size_t)gzipPathLen < sizeof(gzipPath)) && SPIFFS.exists(gzipPath)) {
                servedPath = gzipPath;
                hasGzip = true;
            }
        }
        if (!SPIFFS.exists(servedPath)) return nullptr;

#if FLOW_WEB_HEAP_FORENSICS
        uint32_t servedSize = 0U;
        File servedFile = SPIFFS.open(servedPath, FILE_READ);
        if (servedFile) {
            servedSize = (uint32_t)servedFile.size();
            servedFile.close();
        }
        fillSpiffsAssetForensicMeta_(&localMeta, servedPath, servedSize, hasGzip);
        if (forensicMeta) {
            *forensicMeta = localMeta;
        }
#endif

        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, servedPath, contentType);
        if (!response) {
#if FLOW_WEB_HEAP_FORENSICS
            logSpiffsAssetHeapForensic_("null", localMeta, forensicStartUs, forensicStartHeap);
#endif
            return nullptr;
        }
        response->addHeader("Vary", "Accept-Encoding");
        if (hasGzip) {
            response->addHeader("Content-Encoding", "gzip");
        }
        if (cacheAware) {
            addCacheAwareAssetHeaders_(request, response);
        } else {
            addNoCacheHeaders_(response);
        }
#if FLOW_WEB_HEAP_FORENSICS
        logSpiffsAssetHeapForensic_("prep", localMeta, forensicStartUs, forensicStartHeap);
#endif
        return response;
    };

    auto sendPreparedAssetResponse =
        [](AsyncWebServerRequest* request,
           AsyncWebServerResponse* response,
           const SpiffsAssetForensicMeta* forensicMeta = nullptr) {
        if (!request || !response) return;
#if FLOW_WEB_HEAP_FORENSICS
        const uint32_t forensicStartUs = micros();
        const HeapForensicSnapshot forensicStartHeap = captureHeapForensicSnapshot_();
#endif
        request->send(response);
#if FLOW_WEB_HEAP_FORENSICS
        if (forensicMeta) {
            logSpiffsAssetHeapForensic_("send", *forensicMeta, forensicStartUs, forensicStartHeap);
        }
#endif
    };

    server_.on("/assets/favicon.png", HTTP_GET, [this](AsyncWebServerRequest* request) {
#if FLOW_WEB_DISABLE_ICONS
        request->send(204);
        return;
#else
        char redirectPath[96] = {0};
        const int n = snprintf(redirectPath,
                               sizeof(redirectPath),
                               "/webinterface/i/f.svg?v=%s",
                               webAssetVersion_());
        if (n <= 0 || (size_t)n >= sizeof(redirectPath)) {
            request->send(500, "text/plain", "Failed");
            return;
        }
        request->redirect(redirectPath);
#endif
    });
    server_.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest* request) {
#if FLOW_WEB_DISABLE_ICONS
        request->send(204);
        return;
#else
        char redirectPath[96] = {0};
        const int n = snprintf(redirectPath,
                               sizeof(redirectPath),
                               "/webinterface/i/f.svg?v=%s",
                               webAssetVersion_());
        if (n <= 0 || (size_t)n >= sizeof(redirectPath)) {
            request->send(500, "text/plain", "Failed");
            return;
        }
        request->redirect(redirectPath);
#endif
    });
    auto webInterfaceLandingUrl = []() -> const char* {
        return "/webinterface";
    };

    server_.on("/", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });

    server_.on("/webinterface/app.css", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/app.css", "text/css", true, nullptr, &forensicMeta, &heapRejected);
        if (!response) {
            request->send(heapRejected ? 503 : 404, "text/plain", heapRejected ? "Busy" : "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/sh.html", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/sh.html", "text/html", true, nullptr, &forensicMeta, &heapRejected);
        if (!response) {
            request->send(heapRejected ? 503 : 404, "text/plain", heapRejected ? "Busy" : "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/app.js", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/app.js", "application/javascript", true, nullptr, &forensicMeta, &heapRejected);
        if (!response) {
            request->send(heapRejected ? 503 : 404, "text/plain", heapRejected ? "Busy" : "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/light.css", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/light.css", "text/css", true, nullptr, &forensicMeta, &heapRejected);
        if (!response) {
            request->send(heapRejected ? 503 : 404, "text/plain", heapRejected ? "Busy" : "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/light.js", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/light.js", "application/javascript", true, nullptr, &forensicMeta, &heapRejected);
        if (!response) {
            request->send(heapRejected ? 503 : 404, "text/plain", heapRejected ? "Busy" : "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    server_.on("/webinterface/runtimeui.json", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/runtimeui.json", "application/json", true, nullptr, &forensicMeta, &heapRejected);
        if (!response) {
            request->send(heapRejected ? 503 : 404, "text/plain", heapRejected ? "Busy" : "Not found");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
    });
    auto registerWebSvgRoute = [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](const char* assetPath) {
        server_.on(assetPath, HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse, assetPath](AsyncWebServerRequest* request) {
#if FLOW_WEB_DISABLE_ICONS
            request->send(204);
            return;
#else
            SpiffsAssetForensicMeta forensicMeta{};
            bool heapRejected = false;
            AsyncWebServerResponse* response =
                beginSpiffsAssetResponse(request, assetPath, "image/svg+xml", true, nullptr, &forensicMeta, &heapRejected);
            if (!response) {
                request->send(heapRejected ? 503 : 404, "text/plain", heapRejected ? "Busy" : "Not found");
                return;
            }
            sendPreparedAssetResponse(request, response, &forensicMeta);
#endif
        });
    };
    registerWebSvgRoute("/webinterface/i/m.svg");
    registerWebSvgRoute("/webinterface/i/c.svg");
    registerWebSvgRoute("/webinterface/i/t.svg");
    registerWebSvgRoute("/webinterface/i/s.svg");
    registerWebSvgRoute("/webinterface/i/d.svg");
    registerWebSvgRoute("/webinterface/i/e.svg");
    registerWebSvgRoute("/webinterface/i/r.svg");
    registerWebSvgRoute("/webinterface/i/u.svg");
    registerWebSvgRoute("/webinterface/i/f.svg");
    server_.on("/webinterface/cfgdocs.fr.json", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/cfgdocs.fr.json", "application/json", true, "/webinterface/cfgdocs.jz", &forensicMeta, &heapRejected);
        if (response) {
            sendPreparedAssetResponse(request, response, &forensicMeta);
            return;
        }
        if (heapRejected) {
            request->send(503, "application/json", "{\"ok\":false,\"err\":{\"code\":\"Busy\",\"where\":\"cfgdocs\"}}");
            return;
        }
        AsyncWebServerResponse* fallbackResponse =
            request->beginResponse(200, "application/json", "{\"_meta\":{\"generated\":false},\"docs\":{}}");
        addNoCacheHeaders_(fallbackResponse);
        request->send(fallbackResponse);
    });
    server_.on("/webinterface/cfgmods.fr.json", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/cfgmods.fr.json", "application/json", true, "/webinterface/cfgmods.jz", &forensicMeta, &heapRejected);
        if (response) {
            sendPreparedAssetResponse(request, response, &forensicMeta);
            return;
        }
        if (heapRejected) {
            request->send(503, "application/json", "{\"ok\":false,\"err\":{\"code\":\"Busy\",\"where\":\"cfgmods\"}}");
            return;
        }
        AsyncWebServerResponse* fallbackResponse =
            request->beginResponse(200, "application/json", "{\"_meta\":{\"generated\":false},\"docs\":{}}");
        addNoCacheHeaders_(fallbackResponse);
        request->send(fallbackResponse);
    });
    server_.on("/api/web/meta", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/web/meta");
        StaticJsonDocument<576> doc;
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        } else if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
            mode = NetworkAccessMode::Station;
        }
        const char* modeTxt = "none";
        if (mode == NetworkAccessMode::Station) modeTxt = "station";
        else if (mode == NetworkAccessMode::AccessPoint) modeTxt = "ap";

        doc["ok"] = true;
        doc["web_asset_version"] = webAssetVersion_();
        doc["firmware_version"] = FirmwareVersion::Full;
        doc["profile"] = FLOW_BUILD_PROFILE_NAME;
        doc["profile_name"] = FLOW_BUILD_PROFILE_NAME;
        doc["network_mode"] = modeTxt;
        doc["is_ap_portal"] = (mode == NetworkAccessMode::AccessPoint);
#if defined(FLOW_PROFILE_MICRONOVA)
        doc["local_runtime"] = true;
        doc["local_config_label"] = "Config Store Micronova";
        doc["remote_config_enabled"] = false;
#else
        doc["local_runtime"] = false;
        doc["local_config_label"] = "Config Store Supervisor";
        doc["remote_config_enabled"] = true;
#endif
        doc["hide_menu_svg"] = (FLOW_WEB_HIDE_MENU_SVG != 0);
        doc["unify_status_card_icons"] = (FLOW_WEB_UNIFY_STATUS_CARD_ICONS != 0);
        doc["disable_icons"] = (FLOW_WEB_DISABLE_ICONS != 0);
        SystemStatsSnapshot snap{};
        SystemStats::collect(snap);

        doc["upms"] = snap.uptimeMs;
        JsonObject heap = doc.createNestedObject("heap");
        heap["free"] = snap.heap.freeBytes;
        heap["min_free"] = snap.heap.minFreeBytes;
        heap["largest"] = snap.heap.largestFreeBlock;
        heap["frag"] = snap.heap.fragPercent;

        char out[640] = {0};
        const size_t n = serializeJson(doc, out, sizeof(out));
        if (n == 0 || n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"web.meta\"}}");
            return;
        }

        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", out);
        addNoCacheHeaders_(response);
        request->send(response);
    });
    server_.on("/webinterface", HTTP_GET, [this, spiffsAssetExists, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/webinterface");
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        } else if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
            mode = NetworkAccessMode::Station;
        }
        const bool useLightUi =
            !request->hasParam("full") &&
            (mode == NetworkAccessMode::AccessPoint
#if defined(FLOW_PROFILE_MICRONOVA)
             || true
#endif
            );
        if (useLightUi) {
            if (spiffsAssetExists("/webinterface/light.html")) {
                SpiffsAssetForensicMeta forensicMeta{};
                bool heapRejected = false;
                AsyncWebServerResponse* response =
                    beginSpiffsAssetResponse(request, "/webinterface/light.html", "text/html", true, nullptr, &forensicMeta, &heapRejected);
                if (!response) {
                    request->send(heapRejected ? 503 : 500,
                                  "text/plain",
                                  heapRejected ? "Busy" : "Failed to load light web interface");
                    return;
                }
                sendPreparedAssetResponse(request, response, &forensicMeta);
                return;
            }
            request->send(503, "text/plain", "Light web interface missing");
            return;
        }
        if (!request->hasParam("page")) {
            if (mode == NetworkAccessMode::AccessPoint) {
                if (request->hasParam("full")) {
                    request->redirect("/webinterface?full=1&page=page-wifi");
                    return;
                }
                request->redirect("/webinterface?page=page-wifi");
                return;
            }
        }
        if (spiffsAssetExists("/webinterface/index.html")) {
            SpiffsAssetForensicMeta forensicMeta{};
            bool heapRejected = false;
            AsyncWebServerResponse* response =
                beginSpiffsAssetResponse(request, "/webinterface/index.html", "text/html", false, nullptr, &forensicMeta, &heapRejected);
            if (!response) {
                request->send(heapRejected ? 503 : 500,
                              "text/plain",
                              heapRejected ? "Busy" : "Failed to load web interface");
                return;
            }
            sendPreparedAssetResponse(request, response, &forensicMeta);
            return;
        }
        AsyncWebServerResponse* response = request->beginResponse(200, "text/html", kWebInterfaceFallbackPage);
        addNoCacheHeaders_(response);
        request->send(response);
    });
    server_.on("/webinterface/", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/webserial", HTTP_GET, [this](AsyncWebServerRequest* request) {
        noteHttpActivity_();
        AsyncWebServerResponse* response = request->beginResponse(200, "text/html", kWebSerialLogPage);
        addNoCacheHeaders_(response);
        request->send(response);
    });

    server_.on("/webinterface/health", HTTP_GET, [this](AsyncWebServerRequest* request) {
        noteHttpActivity_();
        request->send(200, "text/plain", "ok");
    });
    server_.on("/webserial/health", HTTP_GET, [this](AsyncWebServerRequest* request) {
        noteHttpActivity_();
        request->redirect("/webinterface/health");
    });
    server_.on("/api/network/mode", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/network/mode");
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        } else if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
            mode = NetworkAccessMode::Station;
        }

        const char* modeTxt = "none";
        if (mode == NetworkAccessMode::Station) modeTxt = "station";
        else if (mode == NetworkAccessMode::AccessPoint) modeTxt = "ap";

        char ip[16] = {0};
        (void)getNetworkIp_(ip, sizeof(ip), nullptr);

        char out[96] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"mode\":\"%s\",\"ip\":\"%s\"}",
                               modeTxt,
                               ip);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"network.mode\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });
    server_.on("/generate_204", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/gen_204", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/hotspot-detect.html", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/connecttest.txt", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/ncsi.txt", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });

    auto fwStatusHandler = [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/status");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->statusJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.status\"}}");
            return;
        }

        char out[320] = {0};
        if (!fwUpdateSvc_->statusJson(fwUpdateSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.status\"}}");
            return;
        }
        request->send(200, "application/json", out);
    };
    server_.on("/fwupdate/status", HTTP_GET, fwStatusHandler);
    server_.on("/api/fwupdate/status", HTTP_GET, fwStatusHandler);

    server_.on("/api/fwupdate/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/config");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->configJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.config\"}}");
            return;
        }

        char out[512] = {0};
        if (!fwUpdateSvc_->configJson(fwUpdateSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.config\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/fwupdate/check", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/check");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->checkManifestJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.check\"}}");
            return;
        }

        char out[4096] = {0};
        char err[128] = {0};
        if (!fwUpdateSvc_->checkManifestJson(fwUpdateSvc_->ctx, out, sizeof(out), err, sizeof(err))) {
            sanitizeJsonString_(err);
            char msg[320] = {0};
            const int n = snprintf(msg,
                                   sizeof(msg),
                                   "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.check\",\"msg\":\"%s\"}}",
                                   err[0] ? err : "failed");
            request->send(409,
                          "application/json",
                          (n > 0 && (size_t)n < sizeof(msg))
                              ? msg
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.check\"}}");
            return;
        }

        request->send(200, "application/json", out);
    });

    server_.on("/api/fwupdate/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/config");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->setConfig) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.set_config\"}}");
            return;
        }

        char hostStr[192] = {0};
        char updatePathStr[192] = {0};
        char flowStr[192] = {0};
        char supStr[192] = {0};
        char nxStr[192] = {0};
        char spiffsStr[192] = {0};
        const bool hasHost = copyRequestParamValue_(request, "update_host", true, hostStr, sizeof(hostStr), "");
        const bool hasUpdatePath =
            copyRequestParamValue_(request, "update_path", true, updatePathStr, sizeof(updatePathStr), "");
        const bool hasFlow = copyRequestParamValue_(request, "flowio_path", true, flowStr, sizeof(flowStr), "");
        const bool hasSupervisor =
            copyRequestParamValue_(request, "supervisor_path", true, supStr, sizeof(supStr), "");
        const bool hasNextion = copyRequestParamValue_(request, "nextion_path", true, nxStr, sizeof(nxStr), "");
        bool hasSpiffs = false;
        if (request->hasParam("spiffs_path", true)) {
            hasSpiffs = copyRequestParamValue_(request, "spiffs_path", true, spiffsStr, sizeof(spiffsStr), "");
        } else if (request->hasParam("cfgdocs_path", true)) {
            hasSpiffs = copyRequestParamValue_(request, "cfgdocs_path", true, spiffsStr, sizeof(spiffsStr), "");
        }

        char err[96] = {0};
        if (!fwUpdateSvc_->setConfig(fwUpdateSvc_->ctx,
                                     hasHost ? hostStr : nullptr,
                                     hasUpdatePath ? updatePathStr : nullptr,
                                     hasFlow ? flowStr : nullptr,
                                     hasSupervisor ? supStr : nullptr,
                                     hasNextion ? nxStr : nullptr,
                                     hasSpiffs ? spiffsStr : nullptr,
                                     err,
                                     sizeof(err))) {
            sanitizeJsonString_(err);
            char out[288] = {0};
            const int n = snprintf(out,
                                   sizeof(out),
                                   "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.set_config\",\"msg\":\"%s\"}}",
                                   err[0] ? err : "failed");
            request->send(409,
                          "application/json",
                          (n > 0 && (size_t)n < sizeof(out))
                              ? out
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.set_config\"}}");
            return;
        }

        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/wifi/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.config.get\"}}");
            return;
        }

        char wifiJson[320] = {0};
        if (!cfgStore_->toJsonModule("wifi", wifiJson, sizeof(wifiJson), nullptr, false)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.get\"}}");
            return;
        }

        StaticJsonDocument<320> doc;
        const DeserializationError err = deserializeJson(doc, wifiJson);
        if (err || !doc.is<JsonObjectConst>()) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidData\",\"where\":\"wifi.config.get\"}}");
            return;
        }

        JsonObjectConst root = doc.as<JsonObjectConst>();
        bool enabled = root["enabled"] | true;
        const char* ssid = root["ssid"] | "";
        const char* pass = root["pass"] | "";

        char ssidSafe[96] = {0};
        char passSafe[96] = {0};
        snprintf(ssidSafe, sizeof(ssidSafe), "%s", ssid ? ssid : "");
        snprintf(passSafe, sizeof(passSafe), "%s", pass ? pass : "");
        sanitizeJsonString_(ssidSafe);
        sanitizeJsonString_(passSafe);

        char out[360] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"enabled\":%s,\"ssid\":\"%s\",\"pass\":\"%s\"}",
                               enabled ? "true" : "false",
                               ssidSafe,
                               passSafe);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.get\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/wifi/ap", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/ap");

        NetworkAccessMode mode = NetworkAccessMode::None;
        char ip[24] = {0};
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        }
        if (netAccessSvc_ && netAccessSvc_->getIP) {
            (void)netAccessSvc_->getIP(netAccessSvc_->ctx, ip, sizeof(ip));
        }
        if (ip[0] == '\0' && mode == NetworkAccessMode::AccessPoint) {
            const IPAddress apIp = WiFi.softAPIP();
            snprintf(ip, sizeof(ip), "%u.%u.%u.%u", apIp[0], apIp[1], apIp[2], apIp[3]);
        }

        char ssid[48] = {0};
        buildProvisioningApSsid_(ssid, sizeof(ssid));
        sanitizeJsonString_(ssid);
        sanitizeJsonString_(ip);

        char out[256] = {0};
        const char* modeTxt = "none";
        if (mode == NetworkAccessMode::Station) modeTxt = "sta";
        else if (mode == NetworkAccessMode::AccessPoint) modeTxt = "ap";
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"active\":%s,\"mode\":\"%s\",\"ssid\":\"%s\",\"pass\":\"flowio1234\",\"ip\":\"%s\",\"clients\":%u}",
                               mode == NetworkAccessMode::AccessPoint ? "true" : "false",
                               modeTxt,
                               ssid,
                               ip,
                               (unsigned)WiFi.softAPgetStationNum());
        request->send(200,
                      "application/json",
                      (n > 0 && (size_t)n < sizeof(out))
                          ? out
                          : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.ap\"}}");
    });

    server_.on("/api/mqtt/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/mqtt/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"mqtt.config.get\"}}");
            return;
        }

        bool enabled = false;
        int32_t port = Limits::Mqtt::Defaults::Port;
        char host[96] = {0};
        char user[64] = {0};
        char pass[64] = {0};
        char baseTopic[48] = "flowio";
        char topicDeviceId[48] = {0};

        char mqttJson[512] = {0};
        if (cfgStore_->toJsonModule("mqtt", mqttJson, sizeof(mqttJson), nullptr, false)) {
            StaticJsonDocument<512> doc;
            const DeserializationError err = deserializeJson(doc, mqttJson);
            if (err || !doc.is<JsonObjectConst>()) {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"InvalidData\",\"where\":\"mqtt.config.get\"}}");
                return;
            }

            JsonObjectConst root = doc.as<JsonObjectConst>();
            enabled = root["enabled"] | false;
            port = root["port"] | Limits::Mqtt::Defaults::Port;
            snprintf(host, sizeof(host), "%s", root["host"] | "");
            snprintf(user, sizeof(user), "%s", root["user"] | "");
            snprintf(pass, sizeof(pass), "%s", root["pass"] | "");
            snprintf(baseTopic, sizeof(baseTopic), "%s", root["baseTopic"] | "flowio");
            snprintf(topicDeviceId, sizeof(topicDeviceId), "%s", root["topicDeviceId"] | "");
        } else {
            LOGW("mqtt.config.get: module unavailable, returning defaults");
        }

        sanitizeJsonString_(host);
        sanitizeJsonString_(user);
        sanitizeJsonString_(pass);
        sanitizeJsonString_(baseTopic);
        sanitizeJsonString_(topicDeviceId);

        char out[512] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"enabled\":%s,\"host\":\"%s\",\"port\":%ld,"
                               "\"user\":\"%s\",\"pass\":\"%s\",\"baseTopic\":\"%s\","
                               "\"topicDeviceId\":\"%s\"}",
                               enabled ? "true" : "false",
                               host,
                               (long)port,
                               user,
                               pass,
                               baseTopic,
                               topicDeviceId);
        request->send(200,
                      "application/json",
                      (n > 0 && (size_t)n < sizeof(out))
                          ? out
                          : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"mqtt.config.get\"}}");
    });

    server_.on("/api/wifi/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.config.set\"}}");
            return;
        }

        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        const bool wasApProvisioning = netAccessSvc_ &&
                                       netAccessSvc_->mode &&
                                       (netAccessSvc_->mode(netAccessSvc_->ctx) == NetworkAccessMode::AccessPoint);

        char enabledStr[8] = {0};
        char ssid[96] = {0};
        char pass[96] = {0};
        copyRequestParamValue_(request, "enabled", true, enabledStr, sizeof(enabledStr), "1");
        const bool enabled = parseBoolParam_(enabledStr, true);
        copyRequestParamValue_(request, "ssid", true, ssid, sizeof(ssid), "");
        copyRequestParamValue_(request, "pass", true, pass, sizeof(pass), "");

        StaticJsonDocument<320> patch;
        JsonObject root = patch.to<JsonObject>();
        JsonObject wifi = root.createNestedObject("wifi");
        wifi["enabled"] = enabled;
        wifi["ssid"] = ssid;
        wifi["pass"] = pass;

        char patchJson[320] = {0};
        if (serializeJson(patch, patchJson, sizeof(patchJson)) == 0) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.set\"}}");
            return;
        }

        if (!cfgStore_->applyJson(patchJson)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.set\"}}");
            return;
        }

        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->notifyWifiConfigChanged) {
            netAccessSvc_->notifyWifiConfigChanged(netAccessSvc_->ctx);
        }

        bool flowSyncAttempted = false;
        bool flowSyncOk = false;
        char flowSyncErr[96] = {0};
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (flowCfgSvc_ && flowCfgSvc_->applyPatchJson) {
            flowSyncAttempted = true;

            StaticJsonDocument<320> flowPatchDoc;
            JsonObject flowRoot = flowPatchDoc.to<JsonObject>();
            JsonObject flowWifi = flowRoot.createNestedObject("wifi");
            flowWifi["enabled"] = enabled;
            flowWifi["ssid"] = ssid;
            flowWifi["pass"] = pass;

            char flowPatchJson[320] = {0};
            const size_t flowPatchLen = serializeJson(flowPatchDoc, flowPatchJson, sizeof(flowPatchJson));
            if (flowPatchLen > 0 && flowPatchLen < sizeof(flowPatchJson)) {
                char flowAck[Limits::Mqtt::Buffers::Ack] = {0};
                flowSyncOk = flowCfgSvc_->applyPatchJson(flowCfgSvc_->ctx, flowPatchJson, flowAck, sizeof(flowAck));
                if (!flowSyncOk) {
                    snprintf(flowSyncErr, sizeof(flowSyncErr), "flowcfg.apply failed");
                }
            } else {
                snprintf(flowSyncErr, sizeof(flowSyncErr), "flowcfg.patch serialize failed");
            }
        }

        if (flowSyncAttempted && flowSyncOk) {
            LOGI("WiFi config synced to Flow.io");
        } else if (flowSyncAttempted) {
            LOGW("WiFi config sync to Flow.io skipped/failed attempted=%d err=%s",
                 (int)flowSyncAttempted,
                 flowSyncErr[0] ? flowSyncErr : "none");
        }

        bool flowRebootAttempted = false;
        bool flowRebootOk = false;
        char flowRebootErr[96] = {0};
        if (wasApProvisioning && flowSyncAttempted && flowSyncOk) {
            flowRebootAttempted = true;
            if (!cmdSvc_ && services_) {
                cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
            }
            if (cmdSvc_ && cmdSvc_->execute) {
                char rebootReply[220] = {0};
                flowRebootOk = cmdSvc_->execute(cmdSvc_->ctx,
                                                "flow.system.reboot",
                                                "{}",
                                                nullptr,
                                                rebootReply,
                                                sizeof(rebootReply));
                if (!flowRebootOk) {
                    snprintf(flowRebootErr, sizeof(flowRebootErr), "flow.system.reboot failed");
                }
            } else {
                snprintf(flowRebootErr, sizeof(flowRebootErr), "command service unavailable");
            }
        }

        if (flowRebootAttempted && flowRebootOk) {
            LOGI("Flow.io reboot requested after AP WiFi provisioning");
        } else if (flowRebootAttempted) {
            LOGW("Flow.io reboot request failed err=%s", flowRebootErr[0] ? flowRebootErr : "unknown");
        }

        char out[384] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,"
                               "\"flowio_sync\":{\"attempted\":%s,\"ok\":%s,\"err\":\"%s\"},"
                               "\"flowio_reboot\":{\"attempted\":%s,\"ok\":%s,\"err\":\"%s\"}}",
                               flowSyncAttempted ? "true" : "false",
                               flowSyncOk ? "true" : "false",
                               flowSyncErr,
                               flowRebootAttempted ? "true" : "false",
                               flowRebootOk ? "true" : "false",
                               flowRebootErr);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(200, "application/json", "{\"ok\":true}");
            return;
        }
        const bool provisioningConfigured =
            provisioningOnly_ &&
            provisioningDisableAfterConfigured_ &&
            isProvisioningConfigured_(cfgStore_, provisioningRequireMqttForConfigured_);
        if (provisioningConfigured) {
            scheduleReboot_(1200U, "prov.done.wifi");
            request->send(200, "application/json", "{\"ok\":true,\"reboot_scheduled\":true}");
            return;
        }

        request->send(200, "application/json", out);
    });

    server_.on("/api/mqtt/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/mqtt/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"mqtt.config.set\"}}");
            return;
        }

        char enabledStr[8] = {0};
        char host[96] = {0};
        char portStr[12] = {0};
        char user[64] = {0};
        char pass[64] = {0};
        char baseTopic[48] = {0};
        char topicDeviceId[48] = {0};
        copyRequestParamValue_(request, "enabled", true, enabledStr, sizeof(enabledStr), "0");
        copyRequestParamValue_(request, "host", true, host, sizeof(host), "");
        copyRequestParamValue_(request, "port", true, portStr, sizeof(portStr), "1883");
        copyRequestParamValue_(request, "user", true, user, sizeof(user), "");
        copyRequestParamValue_(request, "pass", true, pass, sizeof(pass), "");
        copyRequestParamValue_(request, "baseTopic", true, baseTopic, sizeof(baseTopic), "flowio");
        copyRequestParamValue_(request, "topicDeviceId", true, topicDeviceId, sizeof(topicDeviceId), "");

        const bool enabled = parseBoolParam_(enabledStr, false);
        int32_t port = (int32_t)atoi(portStr);
        if (port <= 0 || port > 65535) port = Limits::Mqtt::Defaults::Port;
        sanitizeJsonString_(host);
        sanitizeJsonString_(user);
        sanitizeJsonString_(pass);
        sanitizeJsonString_(baseTopic);
        sanitizeJsonString_(topicDeviceId);

        char patchJson[512] = {0};
        const int n = snprintf(patchJson,
                               sizeof(patchJson),
                               "{\"mqtt\":{\"enabled\":%s,\"host\":\"%s\",\"port\":%ld,"
                               "\"user\":\"%s\",\"pass\":\"%s\",\"baseTopic\":\"%s\","
                               "\"topicDeviceId\":\"%s\"}}",
                               enabled ? "true" : "false",
                               host,
                               (long)port,
                               user,
                               pass,
                               baseTopic,
                               topicDeviceId);
        if (n <= 0 || (size_t)n >= sizeof(patchJson)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"ArgsTooLarge\",\"where\":\"mqtt.config.set\"}}");
            return;
        }

        if (!cfgStore_->applyJson(patchJson)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"mqtt.config.set\"}}");
            return;
        }
        const bool provisioningConfigured =
            provisioningOnly_ &&
            provisioningDisableAfterConfigured_ &&
            isProvisioningConfigured_(cfgStore_, provisioningRequireMqttForConfigured_);
        if (provisioningConfigured) {
            scheduleReboot_(1200U, "prov.done.mqtt");
            request->send(200, "application/json", "{\"ok\":true,\"reboot_scheduled\":true}");
            return;
        }

        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/scan");
        if (!wifiSvc_ && services_) {
            wifiSvc_ = services_->get<WifiService>(ServiceId::Wifi);
        }
        if (!wifiSvc_ || !wifiSvc_->scanStatusJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.scan.get\"}}");
            return;
        }

        char out[Limits::Wifi::Buffers::ScanStatusJson] = {0};
        if (!wifiSvc_->scanStatusJson(wifiSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.scan.get\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/wifi/scan", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/scan");
        if (!wifiSvc_ && services_) {
            wifiSvc_ = services_->get<WifiService>(ServiceId::Wifi);
        }
        if (!wifiSvc_ || !wifiSvc_->requestScan) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.scan.start\"}}");
            return;
        }

        char forceStr[8] = {0};
        copyRequestParamValue_(request, "force", true, forceStr, sizeof(forceStr), "1");
        const bool force = parseBoolParam_(forceStr, true);
        if (!wifiSvc_->requestScan(wifiSvc_->ctx, force)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.scan.start\"}}");
            return;
        }

        request->send(202, "application/json", "{\"ok\":true,\"accepted\":true}");
    });

    server_.on("/api/flow/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flow/status",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->runtimeStatusDomainJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status.link\"}}");
            return;
        }

        (void)sendFlowStatusCompactResponse_(request, flowCfgSvc_);
    });

    server_.on("/api/flow/status/domain", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flow/status/domain",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->runtimeStatusDomainJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status.domain\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status.domain.link\"}}");
            return;
        }

        if (!request->hasParam("d")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"flow.status.domain\"}}");
            return;
        }
        FlowStatusDomain domain = FlowStatusDomain::System;
        char domainStr[16] = {0};
        copyRequestParamValue_(request, "d", false, domainStr, sizeof(domainStr), "");
        if (!parseFlowStatusDomainParam_(domainStr, domain)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadDomain\",\"where\":\"flow.status.domain\"}}");
            return;
        }

        char domainBuf[640] = {0};
        if (!flowCfgSvc_->runtimeStatusDomainJson(flowCfgSvc_->ctx, domain, domainBuf, sizeof(domainBuf))) {
            if (domainBuf[0] != '\0') {
                request->send(500, "application/json", domainBuf);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.status.domain.fetch\"}}");
            }
            return;
        }

        request->send(200, "application/json", domainBuf);
    });

    server_.on("/api/runtime/manifest", HTTP_GET, [this, beginSpiffsAssetResponse, sendPreparedAssetResponse](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/runtime/manifest");
#if defined(FLOW_PROFILE_MICRONOVA)
        AsyncWebServerResponse* response =
            request->beginResponse(200,
                                   "application/json",
                                   reinterpret_cast<const uint8_t*>(kMicronovaRuntimeManifestJson),
                                   sizeof(kMicronovaRuntimeManifestJson) - 1U);
        addNoCacheHeaders_(response);
        request->send(response);
#else
        SpiffsAssetForensicMeta forensicMeta{};
        bool heapRejected = false;
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/runtimeui.json", "application/json", true, nullptr, &forensicMeta, &heapRejected);
        if (!response) {
            request->send(503,
                          "application/json",
                          heapRejected
                              ? "{\"ok\":false,\"err\":{\"code\":\"Busy\",\"where\":\"runtime.manifest\"}}"
                              : "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.manifest\"}}");
            return;
        }
        sendPreparedAssetResponse(request, response, &forensicMeta);
#endif
    });

    server_.on("/api/runtime/alarms", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/runtime/alarms",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        request->send(503, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"Disabled\",\"where\":\"runtime.alarms.disabled\"}}");
    });

    server_.on("/api/runtime/dashboard_slots", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/runtime/dashboard_slots");
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"slots\":[");
        bool first = true;
#if !defined(FLOW_PROFILE_MICRONOVA)
        if (dataStore_) {
            const FlowRemoteRuntimeData& flow = flowRemoteRuntime(*dataStore_);
            for (uint8_t i = 0U; i < kFlowRemoteDashboardSlotCount; ++i) {
                const FlowRemoteDashboardSlotRuntime& slot = flow.dashboardSlots[i];
                if (!slot.enabled) continue;

                char valueBuf[40] = {0};
                char bgColorBuf[8] = {0};
                formatDashboardSlotValueText_(slot, valueBuf, sizeof(valueBuf));
                dashboardSlotBgColorHex_(slot.bgColor565, bgColorBuf, sizeof(bgColorBuf));
                if (!first) response->print(',');
                response->print("{\"slot\":");
                response->print((unsigned)i);
                response->print(",\"runtime_ui_id\":");
                response->print((unsigned long)slot.runtimeUiId);
                const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(slot.runtimeUiId);
                const char* unit = (item && item->unit) ? item->unit : "";
                response->print(",\"label\":");
                printJsonEscaped_(*response, slot.label[0] != '\0' ? slot.label : "Mesure");
                response->print(",\"value\":");
                printJsonEscaped_(*response, valueBuf);
                response->print(",\"unit\":");
                if (unit && unit[0] != '\0') {
                    printJsonEscaped_(*response, dashboardSlotDegreeCUnit_(unit) ? "\xC2\xB0""C" : unit);
                } else {
                    printJsonEscaped_(*response, "");
                }
                response->print(",\"bg_color\":");
                printJsonEscaped_(*response, bgColorBuf);
                response->print(",\"available\":");
                response->print(slot.available ? "true" : "false");
                response->print("}");
                first = false;
            }
        }
#else
        (void)first;
#endif
        response->print("]}");
        request->send(response);
    });

    server_.on("/api/runtime/values", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/runtime/values",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!request->hasParam("ids")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
            return;
        }

        RuntimeUiId ids[kMaxRuntimeHttpIds] = {};
        size_t idCount = 0U;
        char idsCsv[768] = {0};
        const bool hasIds = copyRequestParamValue_(request, "ids", false, idsCsv, sizeof(idsCsv), "");
        if (!hasIds || !parseRuntimeUiIdsCsv_(idsCsv, ids, kMaxRuntimeHttpIds, idCount)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
            return;
        }

#if defined(FLOW_PROFILE_MICRONOVA)
        sendMicronovaLocalRuntimeValuesResponse_(request, dataStore_, ids, idCount);
#else
        sendRuntimeUiValuesResponse_(request, flowCfgSvc_, ids, idCount);
#endif
    });

    server_.on(
        "/api/runtime/values",
        HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            HttpLatencyScope latency(request,
                                     "/api/runtime/values",
                                     kHttpLatencyFlowCfgInfoMs,
                                     kHttpLatencyFlowCfgWarnMs);
            if (request->_tempObject == reinterpret_cast<void*>(1)) {
                request->_tempObject = nullptr;
                return;
            }
            if (!flowCfgSvc_ && services_) {
                flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
            }
            if (!request->_tempObject || request->_tempObject != runtimeValuesBodyScratch_) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.body\"}}");
                return;
            }

            char* body = static_cast<char*>(request->_tempObject);
            request->_tempObject = nullptr;

            StaticJsonDocument<2048> reqDoc;
            const DeserializationError reqErr = deserializeJson(reqDoc, body);
            releaseRuntimeValuesBodyScratch_();
            if (reqErr) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.json\"}}");
                return;
            }

            JsonArrayConst idsIn = reqDoc["ids"].as<JsonArrayConst>();
            if (idsIn.isNull()) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
                return;
            }

            RuntimeUiId ids[kMaxRuntimeHttpIds] = {};
            size_t idCount = 0U;
            for (JsonVariantConst item : idsIn) {
                if (!item.is<uint32_t>()) continue;
                if (idCount >= kMaxRuntimeHttpIds) break;
                const uint32_t raw = item.as<uint32_t>();
                if (raw == 0U || raw > 65535U) continue;
                ids[idCount++] = (RuntimeUiId)raw;
            }
            if (idCount == 0U) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
                return;
            }

#if defined(FLOW_PROFILE_MICRONOVA)
            sendMicronovaLocalRuntimeValuesResponse_(request, dataStore_, ids, idCount);
#else
            sendRuntimeUiValuesResponse_(request, flowCfgSvc_, ids, idCount);
#endif
        },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0U) {
                if (total == 0U || total > kRuntimeValuesBodyMax) {
                    request->_tempObject = reinterpret_cast<void*>(1);
                    request->send(413, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"ArgsTooLarge\",\"where\":\"runtime.values.body\"}}");
                    return;
                }
                if (!acquireRuntimeValuesBodyScratch_()) {
                    request->_tempObject = reinterpret_cast<void*>(1);
                    request->send(503, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"Busy\",\"where\":\"runtime.values.body\"}}");
                    return;
                }
                request->_tempObject = runtimeValuesBodyScratch_;
            }

            if (request->_tempObject == reinterpret_cast<void*>(1)) return;
            char* body = static_cast<char*>(request->_tempObject);
            if (!body) return;
            memcpy(body + index, data, len);
            if ((index + len) < total) return;
            body[total] = '\0';
        });

    server_.on("/api/flowcfg/modules", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/modules",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->listModulesJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.modules\"}}");
            return;
        }
        char out[Limits::Mqtt::Buffers::Ack] = {0};
        if (!flowCfgSvc_->listModulesJson(flowCfgSvc_->ctx, out, sizeof(out))) {
            if (out[0] != '\0') {
                LOGW("flowcfg.modules failed details=%s", out);
                request->send(500, "application/json", out);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.modules\"}}");
            }
            return;
        }

        request->send(200, "application/json", out);
    });

    server_.on("/api/flowcfg/children", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/children",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->listChildrenJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.children\"}}");
            return;
        }
        char prefix[128] = {0};
        copyRequestParamValue_(request, "prefix", false, prefix, sizeof(prefix), "");
        char out[Limits::Mqtt::Buffers::Ack] = {0};
        if (!flowCfgSvc_->listChildrenJson(flowCfgSvc_->ctx, prefix, out, sizeof(out))) {
            if (out[0] != '\0') {
                LOGW("flowcfg.children failed prefix=%s details=%s", prefix, out);
                request->send(500, "application/json", out);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.children\"}}");
            }
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/flowcfg/module", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/module",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->getModuleJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.module\"}}");
            return;
        }
        if (!request->hasParam("name")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.module.name\"}}");
            return;
        }

        char moduleStr[64] = {0};
        copyRequestParamValue_(request, "name", false, moduleStr, sizeof(moduleStr), "");
        if (moduleStr[0] == '\0') {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.module.name\"}}");
            return;
        }

        char moduleName[64] = {0};
        snprintf(moduleName, sizeof(moduleName), "%s", moduleStr);
        sanitizeJsonString_(moduleName);

        bool truncated = false;
        char moduleJson[Limits::Mqtt::Buffers::StateCfg] = {0};
        if (!flowCfgSvc_->getModuleJson(flowCfgSvc_->ctx, moduleStr, moduleJson, sizeof(moduleJson), &truncated)) {
            if (moduleJson[0] != '\0') {
                LOGW("flowcfg.module failed module=%s details=%s", moduleStr, moduleJson);
                request->send(500, "application/json", moduleJson);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.module.get\"}}");
            }
            return;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"module\":");
        printJsonEscaped_(*response, moduleName);
        response->print(",\"truncated\":");
        response->print(truncated ? "true" : "false");
        response->print(",\"data\":");
        response->print(moduleJson);
        response->print('}');
        request->send(response);
    });

    server_.on("/api/flowcfg/apply", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/apply",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->applyPatchJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.apply\"}}");
            return;
        }
        if (!request->hasParam("patch", true)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.apply.patch\"}}");
            return;
        }

        char patchStr[Limits::Mqtt::Buffers::StateCfg] = {0};
        copyRequestParamValue_(request, "patch", true, patchStr, sizeof(patchStr), "");
        char ack[Limits::Mqtt::Buffers::Ack] = {0};
        if (!flowCfgSvc_->applyPatchJson(flowCfgSvc_->ctx, patchStr, ack, sizeof(ack))) {
            if (ack[0] != '\0') {
                request->send(flowCfgApplyHttpStatus_(ack), "application/json", ack);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.apply.exec\"}}");
            }
            return;
        }
        request->send(200, "application/json", ack);
    });

    server_.on("/api/supervisorcfg/modules", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/modules");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.modules\"}}");
            return;
        }

        constexpr uint8_t kMaxModules = Limits::Config::Capacity::ModuleListMax;
        const char* modules[kMaxModules] = {0};
        const uint8_t moduleCount = cfgStore_->listModules(modules, kMaxModules);

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"modules\":[");
        bool first = true;
        for (uint8_t i = 0; i < moduleCount; ++i) {
            if (!modules[i] || modules[i][0] == '\0') continue;
            if (!first) response->print(',');
            printJsonEscaped_(*response, modules[i]);
            first = false;
        }
        response->print("]}");
        request->send(response);
    });

    server_.on("/api/supervisorcfg/children", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/children");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.children\"}}");
            return;
        }

        char prefix[128] = {0};
        copyRequestParamValue_(request, "prefix", false, prefix, sizeof(prefix), "");

        size_t prefixLen = strnlen(prefix, sizeof(prefix));
        while (prefixLen > 0 && prefix[0] == '/') {
            memmove(prefix, prefix + 1, prefixLen);
            --prefixLen;
        }
        while (prefixLen > 0 && prefix[prefixLen - 1] == '/') {
            prefix[prefixLen - 1] = '\0';
            --prefixLen;
        }

        constexpr uint8_t kMaxModules = Limits::Config::Capacity::ModuleListMax;
        const char* modules[kMaxModules] = {0};
        const uint8_t moduleCount = cfgStore_->listModules(modules, kMaxModules);

        const char* childStarts[kMaxModules] = {0};
        size_t childLens[kMaxModules] = {0};
        uint8_t childCount = 0;
        bool hasExact = false;

        for (uint8_t i = 0; i < moduleCount; ++i) {
            const char* childStart = nullptr;
            size_t childLen = 0;
            bool exact = false;
            if (!childTokenForPrefix_(modules[i], prefix, prefixLen, childStart, childLen, exact)) {
                if (exact) hasExact = true;
                continue;
            }

            bool duplicate = false;
            for (uint8_t j = 0; j < childCount; ++j) {
                if (tokensEqual_(childStart, childLen, childStarts[j], childLens[j])) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            childStarts[childCount] = childStart;
            childLens[childCount] = childLen;
            ++childCount;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"has_exact\":");
        response->print(hasExact ? "true" : "false");
        response->print(",\"children\":[");
        for (uint8_t i = 0; i < childCount; ++i) {
            if (i > 0) response->print(',');
            response->print('\"');
            for (size_t j = 0; j < childLens[i]; ++j) {
                response->print(childStarts[i][j]);
            }
            response->print('\"');
        }
        response->print("]}");
        request->send(response);
    });

    server_.on("/api/supervisorcfg/module", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/module");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.module\"}}");
            return;
        }
        if (!request->hasParam("name")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"supervisorcfg.module.name\"}}");
            return;
        }

        char moduleStr[64] = {0};
        copyRequestParamValue_(request, "name", false, moduleStr, sizeof(moduleStr), "");
        if (moduleStr[0] == '\0') {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"supervisorcfg.module.name\"}}");
            return;
        }

        char moduleName[64] = {0};
        snprintf(moduleName, sizeof(moduleName), "%s", moduleStr);
        sanitizeJsonString_(moduleName);

        bool truncated = false;
        char moduleJson[Limits::Mqtt::Buffers::StateCfg] = {0};
        if (!cfgStore_->toJsonModule(moduleStr, moduleJson, sizeof(moduleJson), &truncated)) {
            request->send(404, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotFound\",\"where\":\"supervisorcfg.module.get\"}}");
            return;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        addNoCacheHeaders_(response);
        response->print("{\"ok\":true,\"module\":");
        printJsonEscaped_(*response, moduleName);
        response->print(",\"truncated\":");
        response->print(truncated ? "true" : "false");
        response->print(",\"data\":");
        response->print(moduleJson);
        response->print('}');
        request->send(response);
    });

    server_.on("/api/supervisorcfg/apply", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/apply");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.apply\"}}");
            return;
        }
        if (!request->hasParam("patch", true)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"supervisorcfg.apply.patch\"}}");
            return;
        }

        char patchStr[Limits::Mqtt::Buffers::StateCfg] = {0};
        copyRequestParamValue_(request, "patch", true, patchStr, sizeof(patchStr), "");
        if (!cfgStore_->applyJson(patchStr)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"supervisorcfg.apply.exec\"}}");
            return;
        }
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/system/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/system/reboot");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"system.reboot\"}}");
            return;
        }

        char reply[196] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "system.reboot", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"system.reboot\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/api/system/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/system/factory-reset");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"system.factory_reset\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "system.factory_reset", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"system.factory_reset\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/api/system/nextion/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/system/nextion/reboot");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fw.nextion.reboot\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "fw.nextion.reboot", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fw.nextion.reboot\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/api/flow/system/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/flow/system/reboot");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.system.reboot\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "flow.system.reboot", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.system.reboot\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/api/flow/system/hardware-reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/flow/system/hardware-reboot");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fw.flowio.hw_reboot\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "fw.flowio.hw_reboot", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fw.flowio.hw_reboot\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/api/flow/system/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/flow/system/factory-reset");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.system.factory_reset\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "flow.system.factory_reset", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            request->send(500,
                          "application/json",
                          (reply[0] != '\0')
                              ? reply
                              : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.system.factory_reset\"}}");
            return;
        }
        request->send(200, "application/json", (reply[0] != '\0') ? reply : "{\"ok\":true}");
    });

    server_.on("/fwupdate/flowio", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/flowio");
        handleUpdateRequest_(request, FirmwareUpdateTarget::FlowIO);
    });

    server_.on("/fwupdate/supervisor", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/supervisor");
        handleUpdateRequest_(request, FirmwareUpdateTarget::Supervisor);
    });

    server_.on("/fwupdate/nextion", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/nextion");
        handleUpdateRequest_(request, FirmwareUpdateTarget::Nextion);
    });
    server_.on("/fwupdate/spiffs", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/spiffs");
        handleUpdateRequest_(request, FirmwareUpdateTarget::Spiffs);
    });
    server_.on("/fwupdate/cfgdocs", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/cfgdocs");
        handleUpdateRequest_(request, FirmwareUpdateTarget::Spiffs);
    });

    server_.onNotFound([this, webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        noteHttpActivity_();
        request->redirect(webInterfaceLandingUrl());
    });

    if (!provisioningOnly_ && bridgeUartEnabled_) {
        ws_.onEvent([this](AsyncWebSocket* server,
                           AsyncWebSocketClient* client,
                           AwsEventType type,
                           void* arg,
                           uint8_t* data,
                           size_t len) {
            this->onWsEvent_(server, client, type, arg, data, len);
        });
        server_.addHandler(&ws_);
    } else if (!provisioningOnly_) {
        LOGI("WebInterface wsserial disabled (bridge UART unavailable)");
    }

    if (!provisioningOnly_) {
        wsLog_.onEvent([this](AsyncWebSocket* server,
                              AsyncWebSocketClient* client,
                              AwsEventType type,
                              void* arg,
                              uint8_t* data,
                              size_t len) {
            this->onWsLogEvent_(server, client, type, arg, data, len);
        });

        server_.addHandler(&wsLog_);
    } else {
        LOGI("WebInterface WS handlers disabled in provisioning-only mode");
    }
    server_.begin();
    started_ = true;
    noteServerStarted_();
    LOGI("WebInterface server started, listening on 0.0.0.0:%d", kServerPort);

    char ip[16] = {0};
    NetworkAccessMode mode = NetworkAccessMode::None;
    if (getNetworkIp_(ip, sizeof(ip), &mode) && ip[0] != '\0') {
        if (mode == NetworkAccessMode::AccessPoint) {
            LOGI("WebInterface URL (AP): http://%s/webinterface", ip);
        } else {
            LOGI("WebInterface URL: http://%s/webinterface", ip);
        }
    } else {
        LOGI("WebInterface URL: waiting for network IP");
    }
}

void WebInterfaceModule::handleUpdateRequest_(AsyncWebServerRequest* request, FirmwareUpdateTarget target)
{
    if (!request) return;
    if (!fwUpdateSvc_ && services_) {
        fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    }
    if (!fwUpdateSvc_ || !fwUpdateSvc_->start) {
        request->send(503, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.start\"}}");
        return;
    }

    char urlBuf[224] = {0};
    copyRequestParamValue_(request, "url", true, urlBuf, sizeof(urlBuf), "");
    const char* url = (urlBuf[0] != '\0') ? urlBuf : nullptr;

    char err[144] = {0};
    if (!fwUpdateSvc_->start(fwUpdateSvc_->ctx, target, url, err, sizeof(err))) {
        sanitizeJsonString_(err);
        char out[336] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.start\",\"msg\":\"%s\"}}",
                               err[0] ? err : "failed");
        request->send(409,
                      "application/json",
                      (n > 0 && (size_t)n < sizeof(out))
                          ? out
                          : "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.start\"}}");
        return;
    }

    request->send(202, "application/json", "{\"ok\":true,\"accepted\":true}");
}

bool WebInterfaceModule::isWebReachable_() const
{
    if (netAccessSvc_ && netAccessSvc_->isWebReachable) {
        return netAccessSvc_->isWebReachable(netAccessSvc_->ctx);
    }
    if (wifiSvc_ && wifiSvc_->isConnected) {
        return wifiSvc_->isConnected(wifiSvc_->ctx);
    }
    return netReady_;
}

bool WebInterfaceModule::getNetworkIp_(char* out, size_t len, NetworkAccessMode* modeOut) const
{
    if (out && len > 0) out[0] = '\0';
    if (modeOut) *modeOut = NetworkAccessMode::None;
    if (!out || len == 0) return false;

    if (netAccessSvc_ && netAccessSvc_->getIP) {
        if (netAccessSvc_->getIP(netAccessSvc_->ctx, out, len)) {
            if (modeOut && netAccessSvc_->mode) {
                *modeOut = netAccessSvc_->mode(netAccessSvc_->ctx);
            }
            return out[0] != '\0';
        }
    }

    if (wifiSvc_ && wifiSvc_->getIP) {
        if (wifiSvc_->getIP(wifiSvc_->ctx, out, len)) {
            if (modeOut) *modeOut = NetworkAccessMode::Station;
            return out[0] != '\0';
        }
    }

    return false;
}
