#ifndef MIR2_SERVER_APPS_DEAD_LETTER_REPLAY_COMPAT_H_
#define MIR2_SERVER_APPS_DEAD_LETTER_REPLAY_COMPAT_H_

#include <iosfwd>

namespace mir2::apps {

// Compatibility entrypoint for the legacy mir2_dead_letter_replay binary.
int RunDeadLetterReplayCompat(int argc,
                              char** argv,
                              std::ostream* out,
                              std::ostream* err);

}  // namespace mir2::apps

#endif  // MIR2_SERVER_APPS_DEAD_LETTER_REPLAY_COMPAT_H_
