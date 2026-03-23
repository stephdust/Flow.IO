/**
 * @file MQTTModule.cpp
 * @brief Facade translation unit for MQTTModule.
 *
 * Architecture: MQTTModule keeps a single public facade and splits its
 * implementation across Lifecycle / Transport / Queue / Rx / Producers
 * translation units.
 */

#include "MQTTModule.h"
