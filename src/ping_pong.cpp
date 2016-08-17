
#include "include/ping_pong.hpp"

using namespace caf;

namespace relm {

behavior ping(event_based_actor* self, size_t num_pings) {
  auto count = std::make_shared<size_t>(0);
  return {
    [=](kickoff_atom, const actor& pong) {
      self->send(pong, ping_atom::value, int32_t(1));
      self->become (
        [=](pong_atom, int32_t value) -> result<ping_atom, int32_t> {
          if (++*count >= num_pings) self->quit();
          return {ping_atom::value, value + 1};
        }
      );
    }
  };
}

behavior pong() {
  return {
    [](ping_atom, int32_t value) -> result<pong_atom, int32_t> {
      return {pong_atom::value, value};
    }
  };
}

} // namespace relm
