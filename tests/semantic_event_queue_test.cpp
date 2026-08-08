#include "cec_injector/mate/semantic_event_queue.h"

#include <stdexcept>

#include "test_support.h"

using namespace cec_injector::mate;

int main() {
  bool invalid_rejected = false;
  try {
    SemanticEventQueue invalid(0);
  } catch (const std::invalid_argument&) {
    invalid_rejected = true;
  }
  CHECK(invalid_rejected);

  SemanticEventQueue queue(2);
  CHECK(queue.submit(LifecycleEvent::BootRequested) ==
        EventSubmitResult::Accepted);
  CHECK(queue.submit(LifecycleEvent::HostResumed) ==
        EventSubmitResult::Accepted);
  CHECK(queue.submit(LifecycleEvent::ExternalTvStandby) ==
        EventSubmitResult::Full);
  CHECK(queue.submit(LifecycleEvent::BootRequested) ==
        EventSubmitResult::Duplicate);

  CHECK(queue.begin_next() == LifecycleEvent::BootRequested);
  CHECK(queue.submit(LifecycleEvent::BootRequested) ==
        EventSubmitResult::Duplicate);
  CHECK(!queue.begin_next());
  queue.complete_active();
  CHECK(queue.begin_next() == LifecycleEvent::HostResumed);
  queue.complete_active();
  CHECK(!queue.begin_next());

  CHECK(queue.submit(LifecycleEvent::SessionLost) ==
        EventSubmitResult::Accepted);
  queue.clear();
  CHECK(queue.pending() == 0);
  CHECK(!queue.active());
  CHECK(!queue.begin_next());
  return EXIT_SUCCESS;
}
