#pragma once

#include <tuple>
#include <vector>

#include <caf/all.hpp>

#include "include/reliable_msg.hpp"

namespace relm {

using retransmit_cnt = int32_t;
using clk            = std::chrono::high_resolution_clock;
using tp             = clk::time_point;

using send_atom      = caf::atom_constant<caf::atom("send")>;
using recv_atom      = caf::atom_constant<caf::atom("receive")>;
using register_atom  = caf::atom_constant<caf::atom("register")>;
using send_acks_atom = caf::atom_constant<caf::atom("send_acks")>;

struct reliability_state {
  int16_t unacked = 0;
  int32_t seq_send = 0; // next sequence number to send
  int32_t seq_recv = 0; // next sequence number to receive
  std::vector<reliable_msg> inbox;   // missing previous seq
  std::vector<reliable_msg> outbox;  // requires acks from dest
  std::string name = "reliability_actor";
};

reliable_msg create_ack_msg(reliability_state& state);

std::vector<reliable_msg>::iterator find_seq(std::vector<reliable_msg>& vec,
                                             const int32_t seq);

/// Actor doesn't know the broker yet, waiting to be initialized
caf::behavior init_reliability_actor(caf::stateful_actor<reliability_state>* self,
                                     const caf::actor& app);

/// Actor know the application and the broker, working state
/// Functionality:
/// - retransmit / ack
/// - ordering
/// - missing: duplicate packet detection
caf::behavior reliability_actor(caf::stateful_actor<reliability_state>* self,
                                const caf::actor& app,
                                const caf::actor& broker);

} // namespace relm
