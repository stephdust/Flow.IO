#pragma once
/**
 * @file ConfigMigrations.h
 * @brief Config migration steps for ConfigStore.
 */
#include <Preferences.h>
#include "Core/ConfigStore.h"
#include "Core/NvsKeys.h"

/** @brief Current configuration schema version. */
constexpr uint32_t CURRENT_CFG_VERSION = 2;

/** @brief Migration step from version 0 to 1. */
static bool mig_0_to_1(Preferences& prefs, bool clearOnFail)
{
    (void)prefs;
    (void)clearOnFail;
    // TODO migration logic
    return true; // true = OK, false = failed
}

/** @brief Migration step from version 1 to 2. */
static bool mig_1_to_2(Preferences& prefs, bool clearOnFail)
{
    (void)clearOnFail;
    if (prefs.isKey(NvsKeys::Hmi::RemoteUdpEnabledLegacy) &&
        !prefs.isKey(NvsKeys::Hmi::FlowConnectUdpEnabled)) {
        const bool enabled = prefs.getBool(NvsKeys::Hmi::RemoteUdpEnabledLegacy, false);
        (void)prefs.putBool(NvsKeys::Hmi::FlowConnectUdpEnabled, enabled);
    }
    if (prefs.isKey(NvsKeys::Hmi::RemoteUdpTokenLegacy) &&
        !prefs.isKey(NvsKeys::Hmi::FlowConnectUdpToken)) {
        char token[33]{};
        if (prefs.getString(NvsKeys::Hmi::RemoteUdpTokenLegacy, token, sizeof(token)) > 0U) {
            (void)prefs.putString(NvsKeys::Hmi::FlowConnectUdpToken, token);
        }
    }
    return true;
}

/** @brief Ordered list of migrations. */
static const MigrationStep steps[] = {
    {0, 1, mig_0_to_1},
    {1, 2, mig_1_to_2}
};

/** @brief Number of migration steps. */
static constexpr size_t MIGRATION_COUNT = sizeof(steps) / sizeof(steps[0]);
