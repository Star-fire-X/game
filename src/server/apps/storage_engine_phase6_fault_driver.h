#ifndef MIR2_SERVER_APPS_STORAGE_ENGINE_PHASE6_FAULT_DRIVER_H_
#define MIR2_SERVER_APPS_STORAGE_ENGINE_PHASE6_FAULT_DRIVER_H_

#include <iosfwd>

namespace mir2::apps {

int RunStorageEnginePhase6FaultDriver(int argc,
                                      char** argv,
                                      std::ostream* out,
                                      std::ostream* err);

}  // namespace mir2::apps

#endif  // MIR2_SERVER_APPS_STORAGE_ENGINE_PHASE6_FAULT_DRIVER_H_
