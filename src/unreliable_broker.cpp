
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
#include "include/reliable_msg.hpp"
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
int32_t delay_multiplier = 500;
geometric_distribution<> delay_distribution;
bernoulli_distribution lost_distribution{0.90};
} // namespace anonymous

int write_int(broker* self, connection_handle hdl, uint64_t value) {
  int res = 0;
  res += write_int(self, hdl, static_cast<uint32_t>(value));
  res += write_int(self, hdl, static_cast<uint32_t>(value >> sizeof(uint32_t)));
  return res;
}

int read_int(const void* data, uint64_t& storage) {
  uint32_t first;
  uint32_t second;
  int res = 0;
  res += read_int(data, first);
  res += read_int(reinterpret_cast<const char*>(data) + sizeof(uint32_t), second);
  storage = first | (static_cast<uint64_t>(second) << sizeof(uint32_t));
  return res;
}

behavior broker_impl(broker* self, connection_handle hdl, const actor& buddy) {
  // assumption: we manage exactly one connection`
  assert(self->num_connections() == 1);
  self->monitor(buddy);
  self->set_down_handler([=](down_msg& dm) {
    if (dm.source == buddy) {
      aout(self) << "[B] Buddy is down." << endl;
      self->quit(dm.reason);
    }
  });
  self->send(buddy, register_atom::value, self);
  // Each exchanged message is a reliable_msg to make thing easier here
  self->configure_read(hdl, receive_policy::exactly(sizeof(reliable_msg)));
  return {
    [=](const connection_closed_msg& msg) {
      if (msg.handle == hdl) {
        aout(self) << "[B] Connection closed." << endl;
        self->send_exit(buddy, exit_reason::remote_link_unreachable);
        self->quit(exit_reason::remote_link_unreachable);
      }
    },
    [=](const new_data_msg& incoming) {
      int offset = 0;
      reliable_msg msg;
      uint64_t atm_val;
      offset += read_int(incoming.buf.data(), atm_val);
      msg.atm = static_cast<atom_value>(atm_val);
      offset += read_int(incoming.buf.data() + offset, msg.content);
      offset += read_int(incoming.buf.data() + offset, msg.seq);
      offset += read_int(incoming.buf.data() + offset, msg.num_nacks);
      offset += read_int(incoming.buf.data() + offset, msg.nacks[0]);
      offset += read_int(incoming.buf.data() + offset, msg.nacks[1]);
      offset += read_int(incoming.buf.data() + offset, msg.nacks[2]);
      // loose some messages
      if (lost_distribution(gen)) {
        // "network" delay for the rest
        auto delay = milliseconds{delay_distribution(gen) * delay_multiplier};
        // aout(self) << "[B][" << msg.seq << "][>>] " << to_string(msg)
                   // << " with " << delay.count() << "ms delay" << endl;
        self->delayed_send(buddy, delay, recv_atom::value, msg);
      } else {
        // aout(self) << "[B][" << msg.seq << "][X] " << to_string(msg) << endl;
      }
    },
    [=] (send_atom, const reliable_msg& msg) {
      write_int(self, hdl, static_cast<uint64_t>(msg.atm));
      write_int(self, hdl, msg.content);
      write_int(self, hdl, msg.seq);
      write_int(self, hdl, msg.num_nacks);
      for (auto& nack : msg.nacks) {
        write_int(self, hdl, nack);
      }
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
