#pragma once

#if defined(FLOW_PROFILE_FLOWIO) && defined(FLOW_PROFILE_SUPERVISOR)
#error "Only one firmware profile can be compiled at a time."
#endif

#if !defined(FLOW_PROFILE_FLOWIO) && !defined(FLOW_PROFILE_SUPERVISOR)
#error "A firmware profile macro must be defined."
#endif

#if defined(FLOW_PROFILE_FLOWIO)
#define FLOW_BUILD_IS_FLOWIO 1
#define FLOW_BUILD_IS_SUPERVISOR 0
#define FLOW_BUILD_PROFILE_NAME "FlowIO"
#endif

#if defined(FLOW_PROFILE_SUPERVISOR)
#define FLOW_BUILD_IS_FLOWIO 0
#define FLOW_BUILD_IS_SUPERVISOR 1
#define FLOW_BUILD_PROFILE_NAME "Supervisor"
#endif

