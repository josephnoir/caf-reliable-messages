// mostly copied from simple_broker example in CAF

#ifdef WIN32
# define _WIN32_WINNT 0x0600
# include <Winsock2.h>
#else
# include <arpa/inet.h> // htonl
#endif

#include <vector>
#include <string>
#include <limits>
#include <memory>
#include <cstdint>
#include <cassert>
#include <iostream>

#include <caf/config.hpp>

#include "include/reliable_broker.hpp"

using namespace std;
using namespace caf;
using namespace caf::io;
using namespace std::chrono;

using seq_num_t = int32_t;
using retransmit_cnt = int32_t;
using clk = std::chrono::high_resolution_clock;
using tp = clk::time_point;

using ack_atom = atom_constant<atom("ack")>;
using retransmit_atom = atom_constant<atom("retransmit")>;

using ping_atom = atom_constant<atom("ping")>;
using pong_atom = atom_constant<atom("pong")>;
using kickoff_atom = atom_constant<atom("kickoff")>;

namespace {
auto default_timeout = milliseconds(200);
} // namespace anonymous

// utility function to print an exit message with custom name
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

behavior broker_impl(stateful_broker<reliability_state>* self,
                     connection_handle hdl, const actor& buddy) {
  // we assume io_fsm manages a broker with exactly one connection,
  // i.e., the connection ponted to by `hdl`
  // assert(self->num_connections() == 1);
  // monitor buddy to quit broker if buddy is done
  self->monitor(buddy);
  self->set_down_handler([=](down_msg& dm) {
    if (dm.source == buddy) {
      aout(self) << "our buddy is down" << endl;
      // quit for same reason
      self->quit(dm.reason);
    }
  });
  // setup: we are exchanging only messages consisting of an atom (as uint64_t),
  // a sequence number (as seq_num_t) and *maybe* an integer value (int32_t)
  self->configure_read(hdl, receive_policy::at_least(sizeof(uint64_t) +
                            sizeof(seq_num_t)));
  // our message handlers
  return {
    [=](const connection_closed_msg& msg) {
      // brokers can multiplex any number of connections, however
      // this example assumes io_fsm to manage a broker with
      // exactly one connection
      if (msg.handle == hdl) {
        aout(self) << "connection closed" << endl;
        // force buddy to quit
        self->send_exit(buddy, exit_reason::remote_link_unreachable);
        self->quit(exit_reason::remote_link_unreachable);
      }
    },
    [=](atom_value av, int32_t i) {
      assert(av == ping_atom::value || av == pong_atom::value);
      aout(self) << "send {" << to_string(av) << ", " << i << "}" << endl;
      // TODO: create new sequence number
      // cast atom to its underlying type, i.e., uint64_t
      write_int(self, hdl, static_cast<uint64_t>(av));
      write_int(self, hdl, i);
    },
    [=](const new_data_msg& msg) {
      // TODO: loose some messages
      // read the atom value as uint64_t from buffer
      uint64_t atm_val;
      read_int(msg.buf.data(), atm_val);
      // cast to original type
      auto atm = static_cast<atom_value>(atm_val);
      // read integer value from buffer, jumping to the correct
      // position via offset_data(...)
      seq_num_t seq;
      read_int(msg.buf.data() + sizeof(uint64_t), seq);
      if (atm == ping_atom::value || atm == pong_atom::value) { // ping / pong
        int32_t ival;
        read_int(msg.buf.data() + sizeof(uint64_t) + sizeof(seq_num_t), ival);
        // show some output
        aout(self) << "received {" << to_string(atm) << ", " << ival << "}"
               << endl;
        // send composed message to our buddy
        self->send(buddy, atm, seq, ival);
        // TODO: send ACK for sequence number
        write_int(self, hdl, ack_atom::value.uint_value());
        write_int(self, hdl, seq);
      } else { //  ACK
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
          aout(self) << "[" << seq << "] Received invalid ACK." << endl;
        }
      }
    },
    [=](retransmit_atom, seq_num_t seq) {
      // May be easier to look for the next retransmit and set a timer for that
      // instead of using a separate timer for each one.
      auto& pending = self->state.pending;
      auto msg_itr = find_if(begin(pending), end(pending),
        [seq](const pair<pending_msg,retransmit_cnt>& elem){
          return elem.first.seq == seq;
        }
      );
      if (msg_itr != end(pending)) {
        write_int(self, hdl, static_cast<uint64_t>(msg_itr->first.atm));
        write_int(self, hdl, msg_itr->first.seq);
        write_int(self, hdl, msg_itr->first.content);
        msg_itr->second += 1;
        aout(self) << "[" << seq << "] Retransmitting ... (try "
                   << (msg_itr->second) << ")" << endl;
        self->delayed_send(self, default_timeout, seq);
      } else {
        aout(self) << "[" << seq << "] Received ACK." << endl;
      }
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
