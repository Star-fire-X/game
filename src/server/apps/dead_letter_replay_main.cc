#include <iostream>

#include "apps/dead_letter_replay_compat.h"

int main(int argc, char** argv) {
  return mir2::apps::RunDeadLetterReplayCompat(
      argc, argv, &std::cout, &std::cerr);
}
