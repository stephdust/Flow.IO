#pragma once

class AppContext;

namespace Profiles {
namespace FlowIOS3 {

struct ModuleInstances;

void configureIoModule(const AppContext& ctx, ModuleInstances& modules);
void registerIoHomeAssistant(AppContext& ctx, ModuleInstances& modules);
void releaseIoHomeAssistantDiscoveryHeapIfDone(ModuleInstances& modules);
void refreshIoHomeAssistantIfNeeded(ModuleInstances& modules);

}  // namespace FlowIOS3
}  // namespace Profiles
