
#include <cassert>
#include <iostream>
#include <functional>

#include "include/ping_pong.hpp"
#include "include/reliability_actor.hpp"

using namespace caf;
using namespace std;
using namespace std::chrono;

namespace relm {

namespace {
auto default_timeout = milliseconds(300);
auto forward_delay = milliseconds(500);
}

vector<reliable_msg>::iterator find_seq(int32_t seq,
                                        vector<reliable_msg>& container) {
  return find_if(begin(container), end(container),
                 [seq](const reliable_msg& msg) { return msg.seq == seq; });
}

tuple<int32_t, int32_t, array<int32_t,3>> find_acks(reliability_state& state) {
  auto & rcvd = state.early;
  int32_t found = 0;
  int32_t highest = state.next_recv - 1;
  int32_t current = highest;
  std::array<int32_t,3> nacks;
  std::sort(begin(rcvd), end(rcvd));
  int idx = 0;
  while (found < 3 && idx < rcvd.size()) {
    if (rcvd[idx].seq == current + 1) {
      highest = current + 1;
    } else {
      nacks[found] = current;
      ++found;
    }
    ++current;
    ++idx;
  }
  // for (auto& msg : state.pending) {
  //   if (found >= 3) break;
  // }
  return make_tuple(highest, found, nacks);
}

behavior init_reliability_actor(stateful_actor<reliability_state>* self,
                                    const actor& buddy) {
  aout(self) << "Bootstrapping, awaiting message from broker" << endl;
  self->set_default_handler(skip);
  return {
    [=] (register_atom, const actor& broker) {
      self->become(reliability_actor(self, buddy, broker));
    }
  };
}

behavior reliability_actor(stateful_actor<reliability_state>* self,
                           const actor& buddy, const actor& broker) {
  self->set_default_handler(print_and_drop);
  aout(self) << "[R] Bootstrapping done, now running." << endl;
  return {
    [=](ack_atom, int32_t seq) {
      auto& pending = self->state.pending;
      auto msg_itr = find_if(begin(pending), end(pending),
        [seq](const reliable_msg& elem) {
          return elem.seq == seq;
        }
      );
      if (msg_itr != end(pending)) {
        aout(self) << "[R][" << seq << "] Received ACK." << endl;
        pending.erase(msg_itr);
      } else {
        aout(self) << "[R][" << seq << "] Received out of date ACK." << endl;
      }
    },
    [=](retransmit_atom, int32_t seq) {
      // May be easier to look for the next retransmit and set a timer
      // for it instead of using a separate timers for each possible retransmit.
      auto& pending = self->state.pending;
      auto msg_itr = find_seq(seq, pending);
      if (msg_itr != end(pending)) {
        self->send(broker, send_atom::value, *msg_itr);
        aout(self) << "[R][" << seq << "] Retransmitting." << endl;
        self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      } else {
        aout(self) << "[R][" << seq << "] No retransmit necessary." << endl;
      }
    },
    [=](recv_atom, const reliable_msg& msg) {
      // Incoming message
      if (msg.atm == ping_atom::value || msg.atm == pong_atom::value) {
        // APPLICATION msg
        auto& next = self->state.next_recv;
        if (msg.seq < next) {
          aout(self) <<  "[R][" << msg.seq << "] Message " << to_string(msg)
                   << " has old sequence number, sending ack." << endl;
          self->send(broker, send_atom::value, reliable_msg::ack(msg.seq));
        } else if (msg.seq == next) {
          aout(self) <<  "[R][" << msg.seq << "] Received " << to_string(msg)
                     << "." << endl;
          self->delayed_send(buddy, forward_delay, msg.atm, msg.content);
          self->send(broker, send_atom::value, reliable_msg::ack(msg.seq));
          next += 1;
          // look for early messages
          auto& early = self->state.early;
          auto msg_itr = find_seq(next, early);
          while (msg_itr != end(early)) {
            self->delayed_send(buddy, forward_delay, msg.atm, msg.content);
            early.erase(msg_itr);
            next += 1;
            msg_itr = find_seq(next, early);
          }
        } else if (msg.seq > next) {
          aout(self) << "[R][" << msg.seq << "] Message " << to_string(msg.atm)
                     << " was early, awaiting "
                     << next << "." << endl;
          self->state.early.push_back(msg);
        }
      } else {
        // CONTROL msg
        self->send(self, msg.atm, msg.seq);
      }
    },
    [=](atom_value av, int32_t i) {
      // Message from ping actor, forward via our connection handle
      assert(av == ping_atom::value || av == pong_atom::value);
      auto& next = self->state.next_send;
      int32_t seq = next;
      auto msg = reliable_msg::msg(av, i, seq);
      aout(self) << "[R][" << seq << "] Sending " << to_string(msg) << "." << endl;
      self->send(broker, send_atom::value, msg);
      self->state.pending.push_back(msg);
      self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      next += 1; // increase sequence number for next packet
    }
  };
}

} // namespace relm
