#pragma once

#include <tuple>

#include <caf/all.hpp>

#include "include/reliable_msg.hpp"

namespace relm {

using retransmit_cnt = int32_t;
using clk            = std::chrono::high_resolution_clock;
using tp             = clk::time_point;

using send_atom      = caf::atom_constant<caf::atom("send")>;
using recv_atom      = caf::atom_constant<caf::atom("receive")>;
using register_atom  = caf::atom_constant<caf::atom("register")>;

struct reliability_state {
  int32_t next_send = 0;
  int32_t next_recv = 0;
  std::vector<reliable_msg> pending;
  std::vector<reliable_msg> early;
  std::string name = "reliability_actor";
};

std::vector<reliable_msg>::iterator find_seq(int32_t seq,
                                             std::vector<reliable_msg>& container);
std::tuple<int32_t, int32_t, std::array<int32_t,3>> find_acks(reliability_state& state);

caf::behavior init_reliability_actor(caf::stateful_actor<reliability_state>* self,
                                         const caf::actor& buddy);
caf::behavior reliability_actor(caf::stateful_actor<reliability_state>* self,
                                const caf::actor& buddy,
                                const caf::actor& broker);

} // namespace relm
