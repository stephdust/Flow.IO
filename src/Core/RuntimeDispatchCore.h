#pragma once
/**
 * @file RuntimeDispatchCore.h
 * @brief Generic runtime route dispatcher independent from concrete transports.
 */

#include <stdint.h>
#include <stddef.h>

#include "Core/EventBus/EventPayloads.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/SystemLimits.h"

#include "freertos/FreeRTOS.h"

class IRuntimeDispatchSink {
public:
    virtual ~IRuntimeDispatchSink() = default;

    /** Resolve a provider runtime suffix to sink-specific route target. */
    virtual bool resolveRouteTarget(const char* suffix, char* out, size_t outLen) = 0;
    /** Whether publish attempts can run now (transport connected/ready). */
    virtual bool canPublish() const = 0;
    /** Publish an already built payload to a sink route target. */
    virtual bool publish(const char* routeTarget, const char* payload) = 0;
    /** Optional hook to react to runtime data changes. Return true to force full refresh. */
    virtual bool onDataChanged(const DataChangedPayload&) { return false; }
};

class RuntimeDispatchCore {
public:
    bool registerProvider(const IRuntimeSnapshotProvider* provider);
    void setSink(IRuntimeDispatchSink* sink) { sink_ = sink; }

    void onConfigLoaded();
    void onDataChanged(const DataChangedPayload& change);
    void tick(uint32_t nowMs, char* sharedBuf, size_t sharedBufLen);

    uint8_t routeCount() const { return routeCount_; }

private:
    static constexpr uint8_t kMaxProviders = 8;
    static constexpr uint32_t kNumericThrottleMs = 10000U;
    static constexpr uint8_t kMaxPublishesPerTick = 2;
    static constexpr uint32_t kRetryMinMs = 250U;
    static constexpr uint32_t kRetryMaxMs = 5000U;

    struct RouteEntry {
        const IRuntimeSnapshotProvider* provider = nullptr;
        uint8_t snapshotIdx = 0;
        RuntimeRouteClass routeClass = RuntimeRouteClass::NumericThrottled;
        char routeTarget[Limits::TopicBuf] = {0};
        uint32_t lastPublishedTs = 0;
        uint32_t lastPublishMs = 0;
        uint32_t retryBackoffMs = 0;
        uint32_t retryNextMs = 0;
        uint32_t dirtySeq = 1;
        bool pending = true;
        bool force = true;
    };

    struct RouteSnapshot {
        const IRuntimeSnapshotProvider* provider = nullptr;
        uint8_t snapshotIdx = 0;
        RuntimeRouteClass routeClass = RuntimeRouteClass::NumericThrottled;
        char routeTarget[Limits::TopicBuf] = {0};
        uint32_t lastPublishedTs = 0;
        uint32_t lastPublishMs = 0;
        uint32_t retryBackoffMs = 0;
        uint32_t retryNextMs = 0;
        uint32_t dirtySeq = 0;
        bool pending = false;
        bool force = false;
    };

    void rebuildRoutes_();
    void markRouteDirty_(uint8_t routeIdx, bool force);
    void markAllRoutesDirty_(bool force);
    bool copyRouteSnapshot_(uint8_t routeIdx, RouteSnapshot& out);
    void onPublishSuccess_(uint8_t routeIdx, uint32_t observedDirtySeq, uint32_t nowMs, uint32_t publishedTs);
    void onPublishNoChange_(uint8_t routeIdx, uint32_t observedDirtySeq);
    void scheduleRetry_(uint8_t routeIdx, uint32_t nowMs);

    IRuntimeDispatchSink* sink_ = nullptr;

    const IRuntimeSnapshotProvider* providers_[kMaxProviders] = {};
    uint8_t providerCount_ = 0;

    RouteEntry routes_[Limits::MaxRuntimeRoutes] = {};
    uint8_t routeCount_ = 0;
    portMUX_TYPE routeMux_ = portMUX_INITIALIZER_UNLOCKED;
};
