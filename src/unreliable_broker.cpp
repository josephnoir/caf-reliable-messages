
#include <vector>
#include <string>
#include <limits>
#include <memory>
#include <random>
#include <cstdint>
#include <cassert>
#include <iostream>

#include <caf/config.hpp>

#include "include/utility.hpp"
#include "include/reliability_actor.hpp"
#include "include/unreliable_broker.hpp"

namespace relm {

using namespace std;
using namespace caf;
using namespace caf::io;
using namespace std::chrono;

namespace {
random_device rng;
std::mt19937 gen{rng()};
// same as negative_binomial_distribution<> d(1, 0.5):
geometric_distribution<> delay_distribution;
bernoulli_distribution lost_distribution{0.90};
} // namespace anonymous

void write_int(broker* self, connection_handle hdl, uint64_t value) {
  write_int(self, hdl, static_cast<uint32_t>(value));
  write_int(self, hdl, static_cast<uint32_t>(value >> sizeof(uint32_t)));
}

void read_int(const void* data, uint64_t& storage) {
  uint32_t first;
  uint32_t second;
  read_int(data, first);
  read_int(reinterpret_cast<const char*>(data) + sizeof(uint32_t), second);
  storage = first | (static_cast<uint64_t>(second) << sizeof(uint32_t));
}

behavior broker_impl(broker* self, connection_handle hdl, const actor& buddy) {
  // assumption: we manage exactly one connection`
  assert(self->num_connections() == 1);
  self->monitor(buddy);
  self->set_down_handler([=](down_msg& dm) {
    if (dm.source == buddy) {
      aout(self) << "Broker's buddy is down." << endl;
      self->quit(dm.reason);
    }
  });
  self->send(buddy, register_atom::value, self);
  // we are exchanging messages consisting of
  // - an atom                (as uint64_t),
  // - a sequence number      (as seq_num_t),
  // - an integer value       (as int32_t)
  self->configure_read(hdl, receive_policy::exactly(sizeof(uint64_t) +
                                                    sizeof(seq_num_t) +
                                                    sizeof(int32_t)));
  return {
    [=](const connection_closed_msg& msg) {
      if (msg.handle == hdl) {
        aout(self) << "Broker connection closed." << endl;
        self->send_exit(buddy, exit_reason::remote_link_unreachable);
        self->quit(exit_reason::remote_link_unreachable);
      }
    },
    [=](const new_data_msg& msg) {
      uint64_t atm_val;
      read_int(msg.buf.data(), atm_val);
      auto atm = static_cast<atom_value>(atm_val);
      seq_num_t seq;
      read_int(msg.buf.data() + sizeof(uint64_t), seq);
      int32_t ival;
      read_int(msg.buf.data() + sizeof(uint64_t) + sizeof(seq_num_t), ival);
      reliable_msg rmsg{atm, seq, ival};
      // loose some messages
      if (lost_distribution(gen)) {
        // "network" delay for the rest
        auto delay = milliseconds{delay_distribution(gen) * 100};
        aout(self) << "[" << seq << "] Incoming " << to_string(rmsg)
                   << " with delay " << delay.count() << "ms." << endl;
        self->delayed_send(buddy, delay, recv_atom::value, rmsg);
      } else {
        aout(self) << "[" << seq << "] Incoming message " << to_string(rmsg) << " lost."
                   << endl;
      }
    }, 
    [=] (send_atom, const reliable_msg& msg) {
      write_int(self, hdl, static_cast<uint64_t>(msg.atm));
      write_int(self, hdl, msg.seq);
      write_int(self, hdl, msg.content);
    }
  };
}

behavior server(broker* self, const actor& buddy) {
  aout(self) << "Server is running." << endl;
  return {
    [=](const new_connection_msg& msg) {
      aout(self) << "Server accepted new connection." << endl;
      // by forking into a new broker, we are no longer
      // responsible for the connection
      auto impl = self->fork(broker_impl, msg.handle, buddy);
      print_on_exit(impl, "broker_impl");
      aout(self) << "Quit server (only accept 1 connection)." << endl;
      self->quit();
    }
  };
}

} // namespace relm
