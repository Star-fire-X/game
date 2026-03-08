#include <iostream>

#include "apps/storage_engine_phase6_fault_driver.h"

int main(int argc, char** argv) {
  return mir2::apps::RunStorageEnginePhase6FaultDriver(
      argc, argv, &std::cout, &std::cerr);
}
