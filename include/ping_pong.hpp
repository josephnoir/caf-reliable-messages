#pragma once

#include <caf/all.hpp>

namespace relm {

using ping_atom = caf::atom_constant<caf::atom("ping")>;
using pong_atom = caf::atom_constant<caf::atom("pong")>;
using kickoff_atom = caf::atom_constant<caf::atom("kickoff")>;

caf::behavior ping(caf::event_based_actor* self, size_t num_pings);
caf::behavior pong();

} // namespace relm
