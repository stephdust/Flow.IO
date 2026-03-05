/**
 * @file RuntimeDispatchCore.cpp
 * @brief Generic runtime route dispatcher independent from concrete transports.
 */

#include "Core/RuntimeDispatchCore.h"

#include <stdio.h>
#include <string.h>

bool RuntimeDispatchCore::registerProvider(const IRuntimeSnapshotProvider* provider)
{
    if (!provider) return false;
    if (providerCount_ >= kMaxProviders) return false;

    for (uint8_t i = 0; i < providerCount_; ++i) {
        if (providers_[i] == provider) return true;
    }

    providers_[providerCount_++] = provider;
    return true;
}

void RuntimeDispatchCore::onConfigLoaded()
{
    rebuildRoutes_();
    markAllRoutesDirty_(true);
}

void RuntimeDispatchCore::rebuildRoutes_()
{
    portENTER_CRITICAL(&routeMux_);
    routeCount_ = 0;

    for (uint8_t p = 0; p < providerCount_; ++p) {
        const IRuntimeSnapshotProvider* provider = providers_[p];
        if (!provider) continue;

        const uint8_t snapCount = provider->runtimeSnapshotCount();
        for (uint8_t idx = 0; idx < snapCount; ++idx) {
            if (routeCount_ >= Limits::MaxRuntimeRoutes) {
                portEXIT_CRITICAL(&routeMux_);
                return;
            }

            const char* suffix = provider->runtimeSnapshotSuffix(idx);
            if (!suffix || suffix[0] == '\0') continue;

            RouteEntry& route = routes_[routeCount_++];
            route = RouteEntry{};
            route.provider = provider;
            route.snapshotIdx = idx;
            route.routeClass = provider->runtimeSnapshotClass(idx);
            route.dirtySeq = 1;
            route.pending = true;
            route.force = true;

            bool mapped = false;
            if (sink_) {
                mapped = sink_->resolveRouteTarget(suffix, route.routeTarget, sizeof(route.routeTarget));
            }
            if (!mapped) {
                snprintf(route.routeTarget, sizeof(route.routeTarget), "%s", suffix);
            }
        }
    }

    portEXIT_CRITICAL(&routeMux_);
}

void RuntimeDispatchCore::markRouteDirty_(uint8_t routeIdx, bool force)
{
    if (routeIdx >= routeCount_) return;

    portENTER_CRITICAL(&routeMux_);
    RouteEntry& route = routes_[routeIdx];
    route.pending = true;
    route.force = route.force || force;
    route.dirtySeq += 1U;
    if (route.dirtySeq == 0U) route.dirtySeq = 1U;
    portEXIT_CRITICAL(&routeMux_);
}

void RuntimeDispatchCore::markAllRoutesDirty_(bool force)
{
    portENTER_CRITICAL(&routeMux_);
    for (uint8_t i = 0; i < routeCount_; ++i) {
        RouteEntry& route = routes_[i];
        route.pending = true;
        route.force = route.force || force;
        route.dirtySeq += 1U;
        if (route.dirtySeq == 0U) route.dirtySeq = 1U;
    }
    portEXIT_CRITICAL(&routeMux_);
}

void RuntimeDispatchCore::onDataChanged(const DataChangedPayload& change)
{
    if (sink_ && sink_->onDataChanged(change)) {
        markAllRoutesDirty_(true);
    }

    for (uint8_t i = 0; i < routeCount_; ++i) {
        const RouteEntry& route = routes_[i];
        if (!route.provider) continue;
        if (!route.provider->runtimeSnapshotAffectsKey(route.snapshotIdx, change.id)) continue;
        markRouteDirty_(i, false);
    }
}

bool RuntimeDispatchCore::copyRouteSnapshot_(uint8_t routeIdx, RouteSnapshot& out)
{
    if (routeIdx >= routeCount_) return false;

    portENTER_CRITICAL(&routeMux_);
    const RouteEntry& route = routes_[routeIdx];
    out.provider = route.provider;
    out.snapshotIdx = route.snapshotIdx;
    out.routeClass = route.routeClass;
    memcpy(out.routeTarget, route.routeTarget, sizeof(out.routeTarget));
    out.lastPublishedTs = route.lastPublishedTs;
    out.lastPublishMs = route.lastPublishMs;
    out.retryBackoffMs = route.retryBackoffMs;
    out.retryNextMs = route.retryNextMs;
    out.dirtySeq = route.dirtySeq;
    out.pending = route.pending;
    out.force = route.force;
    portEXIT_CRITICAL(&routeMux_);

    return true;
}

void RuntimeDispatchCore::onPublishSuccess_(uint8_t routeIdx,
                                            uint32_t observedDirtySeq,
                                            uint32_t nowMs,
                                            uint32_t publishedTs)
{
    if (routeIdx >= routeCount_) return;

    portENTER_CRITICAL(&routeMux_);
    RouteEntry& route = routes_[routeIdx];
    route.lastPublishedTs = publishedTs;
    route.lastPublishMs = nowMs;
    route.retryBackoffMs = 0;
    route.retryNextMs = 0;
    if (route.dirtySeq == observedDirtySeq) {
        route.pending = false;
        route.force = false;
    }
    portEXIT_CRITICAL(&routeMux_);
}

void RuntimeDispatchCore::onPublishNoChange_(uint8_t routeIdx, uint32_t observedDirtySeq)
{
    if (routeIdx >= routeCount_) return;

    portENTER_CRITICAL(&routeMux_);
    RouteEntry& route = routes_[routeIdx];
    if (route.dirtySeq == observedDirtySeq && !route.force) {
        route.pending = false;
    }
    route.retryBackoffMs = 0;
    route.retryNextMs = 0;
    portEXIT_CRITICAL(&routeMux_);
}

void RuntimeDispatchCore::scheduleRetry_(uint8_t routeIdx, uint32_t nowMs)
{
    if (routeIdx >= routeCount_) return;

    portENTER_CRITICAL(&routeMux_);
    RouteEntry& route = routes_[routeIdx];
    uint32_t backoff = route.retryBackoffMs;
    if (backoff == 0U) backoff = kRetryMinMs;
    else if (backoff >= (kRetryMaxMs / 2U)) backoff = kRetryMaxMs;
    else backoff *= 2U;

    route.retryBackoffMs = backoff;
    route.retryNextMs = nowMs + backoff;
    route.pending = true;
    portEXIT_CRITICAL(&routeMux_);
}

void RuntimeDispatchCore::tick(uint32_t nowMs, char* sharedBuf, size_t sharedBufLen)
{
    if (!sink_ || !sharedBuf || sharedBufLen == 0) return;
    if (!sink_->canPublish()) return;

    uint8_t publishBudget = kMaxPublishesPerTick;
    for (uint8_t i = 0; i < routeCount_; ++i) {
        if (publishBudget == 0) break;
        RouteSnapshot route{};
        if (!copyRouteSnapshot_(i, route)) continue;
        if (!route.provider || route.routeTarget[0] == '\0') continue;
        if (!route.pending && !route.force) continue;

        if (route.retryNextMs != 0U && (int32_t)(nowMs - route.retryNextMs) < 0) continue;
        if (!route.force && route.routeClass == RuntimeRouteClass::NumericThrottled) {
            if ((uint32_t)(nowMs - route.lastPublishMs) < kNumericThrottleMs) continue;
        }

        uint32_t ts = 0;
        if (!route.provider->buildRuntimeSnapshot(route.snapshotIdx, sharedBuf, sharedBufLen, ts)) {
            scheduleRetry_(i, nowMs);
            continue;
        }

        if (!route.force && route.lastPublishedTs != 0U && ts <= route.lastPublishedTs) {
            onPublishNoChange_(i, route.dirtySeq);
            continue;
        }

        --publishBudget;
        if (sink_->publish(route.routeTarget, sharedBuf)) {
            onPublishSuccess_(i, route.dirtySeq, nowMs, ts);
        } else {
            scheduleRetry_(i, nowMs);
        }
    }
}
