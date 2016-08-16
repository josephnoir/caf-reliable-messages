
#include <vector>
#include <string>
#include <limits>
#include <memory>
#include <random>
#include <cstdint>
#include <cassert>
#include <iostream>

#include <caf/config.hpp>

#include "include/reliable_broker.hpp"

namespace relm {

using namespace std;
using namespace caf;
using namespace caf::io;
using namespace std::chrono;

using ack_atom = atom_constant<atom("ack")>;
using retransmit_atom = atom_constant<atom("retransmit")>;

namespace {
auto default_timeout = milliseconds(300);
random_device rng;
std::mt19937 gen{rng()};
// same as negative_binomial_distribution<> d(1, 0.5):
geometric_distribution<> delay_distribution;
bernoulli_distribution lost_distribution{0.90};
} // namespace anonymous

void print_on_exit(const actor& hdl, const std::string& name) {
  hdl->attach_functor([=](const error& reason) {
    cout << name << " exited: " << to_string(reason) << endl;
  });
}

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

void write_int(stateful_broker<reliability_state>* self,
               connection_handle hdl, uint64_t value) {
  // write two uint32 values instead (htonl does not work for 64bit integers)
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

void send_ack(stateful_broker<reliability_state>* self,
              connection_handle hdl, seq_num_t seq) {
  write_int(self, hdl, static_cast<uint64_t>(ack_atom::value.uint_value()));
  write_int(self, hdl, seq);
}

void send_message(stateful_broker<reliability_state>* self,
                  connection_handle hdl, const pending_msg& msg) {
  write_int(self, hdl, static_cast<uint64_t>(msg.atm));
  write_int(self, hdl, msg.seq);
  write_int(self, hdl, msg.content);
}

behavior broker_impl(stateful_broker<reliability_state>* self,
                     connection_handle hdl, const actor& buddy) {
  // assumption: we manage exactly one connection`
  assert(self->num_connections() == 1);
  self->monitor(buddy);
  self->set_down_handler([=](down_msg& dm) {
    if (dm.source == buddy) {
      aout(self) << "our buddy is down" << endl;
      self->quit(dm.reason);
    }
  });
  // we are exchanging messages consisting of
  // - an atom                  (as uint64_t),
  // - a sequence number        (as seq_num_t),
  // - *maybe* an integer value (as int32_t)
  self->configure_read(hdl, receive_policy::at_least(sizeof(uint64_t) +
                            sizeof(seq_num_t)));
  return {
    [=](const connection_closed_msg& msg) {
      if (msg.handle == hdl) {
        aout(self) << "connection closed" << endl;
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
      // loose some messages
      if (lost_distribution(gen)) {
        // input delay for the rest
        auto delay = milliseconds{delay_distribution(gen) * 100};
        if (atm == ping_atom::value || atm == pong_atom::value) {
          int32_t ival;
          read_int(msg.buf.data() + sizeof(uint64_t) + sizeof(seq_num_t), ival);
          self->delayed_send(self, delay, atm, seq, ival);
        } else if (atm == ack_atom::value) {
          self->delayed_send(self, delay, atm, seq);
        } else {
          aout(self) << "[" << seq << "] Unexpected message {"
                     << to_string(atm) << "}." << endl;
        }
      } else {
        aout(self) << "[" << seq << "] Message lost." << endl;
      }
    },
    [=](ack_atom, seq_num_t seq) {
      auto& pending = self->state.pending;
      auto msg_itr = find_if(begin(pending), end(pending),
        [seq](const pair<pending_msg,retransmit_cnt>& elem){
          return elem.first.seq == seq;
        }
      );
      if (msg_itr != end(pending)) {
        aout(self) << "[" << seq << "] Received ACK after "
                   << (msg_itr->second) << " tries." << endl;
        pending.erase(msg_itr);
      } else {
        aout(self) << "[" << seq << "] Received out of date ACK." << endl;
      }
    },
    [=](retransmit_atom, seq_num_t seq) {
      // May be easier to look for the next retransmit and set a timer
      // for it instead of using a separate timers for each possible retransmit.
      auto& pending = self->state.pending;
      auto msg_itr = find_if(begin(pending), end(pending),
        [seq](const pair<pending_msg,retransmit_cnt>& elem){
          return elem.first.seq == seq;
        }
      );
      if (msg_itr != end(pending)) {
//        send_message(self, hdl, msg_itr->first);
        write_int(self, hdl, static_cast<uint64_t>(msg_itr->first.atm));
        write_int(self, hdl, msg_itr->first.seq);
        write_int(self, hdl, msg_itr->first.content);
        msg_itr->second += 1; // increase number of retransmits
        aout(self) << "[" << seq << "] Retransmitting ... (try "
                   << (msg_itr->second) << ")" << endl;
        self->delayed_send(self, default_timeout, seq);
      } else {
        aout(self) << "[" << seq << "] Already ACKed." << endl;
      }
    },
    [=](atom_value av, int32_t i) {
      // Message from buddy, forward via our connection handle
      assert(av == ping_atom::value || av == pong_atom::value);
      seq_num_t seq = self->state.next;
      aout(self) << "[" << seq << "] Sending {" << to_string(av) << ", "
                 << i << "}" << endl;
      write_int(self, hdl, static_cast<uint64_t>(av));
      write_int(self, hdl, seq);
      write_int(self, hdl, i);
      self->state.pending.emplace_back(pending_msg{av, seq, i}, 0);
//      send_message(self, hdl, self->state.pending.back().first);
      self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      self->state.next += 1; // increase seqence number for next packet
    },
    [=](atom_value av, seq_num_t seq, int32_t i) {
      // Regular incoming message
      assert(av == ping_atom::value || av == pong_atom::value);
      aout(self) <<  "[" << seq << "] Received {" << to_string(av)
                 << ", " << i << "}" << endl;
      // TODO: check if seq number matches expected ...
      if (seq != self->state.next) {
        aout(self) <<  "[" << seq << "] Expected sequence number " << seq
                   << "." << endl;
      }
      self->delayed_send(buddy, milliseconds(500), av, i);
//      send_ack(self, hdl, seq);
      write_int(self, hdl, static_cast<uint64_t>(ack_atom::value.uint_value()));
      write_int(self, hdl, seq);
      self->state.next = seq + 1;
    }
  };
}

behavior server(stateful_broker<reliability_state>* self, const actor& buddy) {
  aout(self) << "server is running" << endl;
  return {
    [=](const new_connection_msg& msg) {
      aout(self) << "server accepted new connection" << endl;
      // by forking into a new broker, we are no longer
      // responsible for the connection
      auto impl = self->fork(broker_impl, msg.handle, buddy);
      print_on_exit(impl, "broker_impl");
      aout(self) << "quit server (only accept 1 connection)" << endl;
      self->quit();
    }
  };
}

} // namespace relm
