
#include <caf/all.hpp>

namespace relm {

using seq_num_t      = int32_t;
using retransmit_cnt = int32_t;
using clk            = std::chrono::high_resolution_clock;
using tp             = clk::time_point;

using send_atom      = caf::atom_constant<caf::atom("send")>;
using recv_atom      = caf::atom_constant<caf::atom("receive")>;
using register_atom  = caf::atom_constant<caf::atom("register")>;

struct reliable_msg {
  caf::atom_value atm;
  seq_num_t seq;
  int32_t content;
};

std::string to_string(const reliable_msg& msg);

template <class Inspector>
typename Inspector::result_type inspect(Inspector& f, reliable_msg& x) {
  return f(caf::meta::type_name("reliable_msg"), x.atm, x.seq, x.content);
}

struct reliability_state {
  seq_num_t next = 0;
  std::vector<reliable_msg> pending;
  std::vector<reliable_msg> early;
  std::string name = "reliability_actor";
};

caf::behavior boostrap_reliability_actor(caf::stateful_actor<reliability_state>* self,
                                         const caf::actor& buddy);
caf::behavior reliability_actor(caf::stateful_actor<reliability_state>* self,
                                const caf::actor& buddy,
                                const caf::actor& broker);

} // namespace relm
