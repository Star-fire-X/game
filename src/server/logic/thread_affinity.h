/**
 * @file thread_affinity.h
 * @brief Logic thread affinity helpers.
 */

#ifndef MIR2_LOGIC_THREAD_AFFINITY_H_
#define MIR2_LOGIC_THREAD_AFFINITY_H_

#include <thread>

namespace mir2::logic {

void BindLogicThread(std::thread::id thread_id);
void ClearLogicThread();
bool IsLogicThreadBound();
bool IsOnLogicThread();
bool AssertOnLogicThread(const char* tag);
bool AssertNotOnLogicThread(const char* tag);

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_THREAD_AFFINITY_H_
