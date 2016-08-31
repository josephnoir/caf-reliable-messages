
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
auto forward_delay = milliseconds(50);
auto ack_interval = milliseconds(200);
}

tuple<int32_t, int32_t, array<int32_t,3>> find_acks(reliability_state& state) {
  auto & inbox = state.inbox;
  int32_t highest = state.last_acked;
//  int32_t searching = highest + 1;
  int32_t found = 0;
  size_t idx = 0;
  std::array<int32_t,3> nacks = {{0,0,0}};
  while (found < 3 && idx < inbox.size()) {
    ++idx;
  }
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
  self->delayed_send(self, ack_interval, send_acks_atom::value);
  aout(self) << "[R] Bootstrapping done, now running." << endl;
  return {
    [=](ack_atom, int32_t seq) {
      auto& outbox = self->state.outbox;
      auto msg_itr = outbox.find(seq);
      if (msg_itr != end(outbox)) {
        aout(self) << "[R][" << seq << "] Received ACK." << endl;
        outbox.erase(msg_itr);
      } else {
        aout(self) << "[R][" << seq << "] Received out of date ACK." << endl;
      }
    },
    [=](retransmit_atom, int32_t seq) {
      // May be easier to look for the next retransmit and set a timer
      // for it instead of using a separate timers for each possible retransmit.
      auto& outbox = self->state.outbox;
      auto msg_itr = outbox.find(seq);
      if (msg_itr != end(outbox)) {
        self->send(broker, send_atom::value, *msg_itr);
        aout(self) << "[R][" << seq << "] Retransmitting." << endl;
        self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      } else {
        aout(self) << "[R][" << seq << "] No retransmit necessary." << endl;
      }
    },
    [=](send_acks_atom) {
      // BEGIN: testing ack messages
      int32_t highest;
      int32_t num_nacks;
      array<int32_t, 3> nacks;
      tie(highest, num_nacks, nacks) = find_acks(self->state);
      aout(self) << "[ACKS]  highest: " << highest << ", number of nacks: "
                 << num_nacks << ", nacks: [" << nacks[0] << ", " << nacks[1]
                 << ", " << nacks[2] << "]" << endl;
      // END: testing ack messages
      self->delayed_send(self, ack_interval, send_acks_atom::value);
    },
    [=](recv_atom, const reliable_msg& msg) {
      // Incoming message
      if (msg.atm == ping_atom::value || msg.atm == pong_atom::value) {
        // APPLICATION msg
        auto& next = self->state.next_recv;
        if (msg.seq < next) {
          aout(self) <<  "[R][" << msg.seq << "] Message " << to_string(msg)
                   << " has old sequence number, adding to pending." << endl;
          // will be acked automatically, by cumutative acks
        } else if (msg.seq == next) {
          aout(self) <<  "[R][" << msg.seq << "] Received " << to_string(msg)
                     << "." << endl;
          next += 1;
          self->delayed_send(buddy, forward_delay, msg.atm, msg.content);
          // ACK will be sent by "send_ack_atom" handler, expecting that all
          // seqs < next_recv have been received ...
          // look for received messages with subsequent sequence numbers
          auto& early = self->state.inbox;
          auto msg_itr = early.find(next);
          while (msg_itr != end(early)) {
            self->delayed_send(buddy, forward_delay, msg.atm, msg.content);
            early.erase(msg_itr);
            next += 1;
            msg_itr = early.find(next);
          }
        } else if (msg.seq > next) {
          aout(self) << "[R][" << msg.seq << "] Message " << to_string(msg.atm)
                     << " was early, awaiting "
                     << next << "." << endl;
          self->state.inbox.emplace(msg.seq, msg);
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
      aout(self) << "[R][" << seq << "] Sending " << to_string(msg)
                 << "." << endl;
      self->send(broker, send_atom::value, msg);
      self->state.outbox.emplace(msg.seq,msg);
      self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      next += 1; // increase sequence number for next packet
    }
  };
}

} // namespace relm
