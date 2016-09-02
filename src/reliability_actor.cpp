
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
const auto default_timeout = milliseconds(4000);
const auto forward_delay = milliseconds(2000);
const auto ack_interval_time = milliseconds(1000);
const int16_t ack_interval_count = 10;
}

reliable_msg create_ack_msg(reliability_state& state) {
  // just use cumutative acks for now
  if (state.inbox.empty()) {
    state.unacked = state.inbox.size();
    return reliable_msg::ack(state.seq_recv - 1, 0, {{0,0,0}});
  }
  int32_t highest = state.seq_recv - 1;
  // int32_t searching = highest + 1;
  int32_t found = 0;
  // size_t idx = 0;
  std::array<int32_t,3> nacks = {{0,0,0}};
  // if (state.inbox.front().seq == searching) {
  //   cerr << "ERROR: inbox contains undeliverd messages with old seqence number"
  //        << endl;
  //   // deliver messages?
  // }
  // while (idx < state.inbox.size()) {
  //   if (searching < state.inbox[idx].seq) {
  //     if (found < 3) {
  //       nacks[found] = searching;
  //       ++found;
  //     } else {
  //       break; // URGH!
  //     }
  //   } else if (searching == state.inbox[idx].seq) {
  //     highest = state.inbox[idx].seq;
  //   } else { // searching > state.inbox[idx].seq
  //     // this should not happen
  //   }
  //   ++searching;
  //   ++idx;
  // }
  state.unacked = state.inbox.size();
  return reliable_msg::ack(highest, found, nacks);
}

vector<reliable_msg>::iterator find_seq(vector<reliable_msg>& vec,
                                        const int32_t seq) {
  return find_if(begin(vec), end(vec), [seq](const reliable_msg& msg) {
    return msg.seq == seq;
  });
}

behavior init_reliability_actor(stateful_actor<reliability_state>* self,
                                    const actor& app) {
  aout(self) << "Bootstrapping, awaiting message from broker" << endl;
  self->set_default_handler(skip);
  return {
    [=] (register_atom, const actor& broker) {
      self->become(reliability_actor(self, app, broker));
    }
  };
}

void send_acks(stateful_actor<reliability_state>* self,
               const actor& broker) {
  auto ack_msg = create_ack_msg(self->state);
  aout(self) << "[R][" << ack_msg.seq << "][<<] " << to_string(ack_msg) << endl;
  self->send(broker, send_atom::value, move(ack_msg));
  self->state.unacked = self->state.inbox.size();
}

behavior reliability_actor(stateful_actor<reliability_state>* self,
                           const actor& app, const actor& broker) {
  self->set_default_handler(print_and_drop);
  self->delayed_send(self, ack_interval_time, send_acks_atom::value);
  self->state.inbox.reserve(ack_interval_count);
  aout(self) << "[R] Bootstrapping done, now running." << endl;
  return {
    [=](ack_atom, const reliable_msg& msg) {
      assert(msg.atm == ack_atom::value);
      // ack all <= seq
      aout(self) << "[R][" << msg.seq << "][>>] " << to_string(msg) << endl;
      auto& outbox = self->state.outbox;
      auto rm = [msg](const reliable_msg& other) {
        return other.seq <= msg.seq;
      };
// && (num_nacks == 0 || (find(begin(nacks), end(nacks), msg.seq) == end(nacks)));
      auto itr = remove_if(begin(outbox), end(outbox), rm);
      if (itr != end(outbox)) outbox.erase(itr);
    },
    [=](retransmit_atom, int32_t seq) {
      // May be easier to look for the next retransmit and set a timer
      // for it instead of using a separate timers for each possible retransmit.
      auto& outbox = self->state.outbox;
      auto msg_itr = find_seq(outbox, seq);
      if (msg_itr != end(outbox)) {
        self->send(broker, send_atom::value, *msg_itr);
        aout(self) << "[R][" << seq << "][<<] Retransmitting." << endl;
        self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      } else {
        aout(self) << "[R][" << seq << "][<<] Retransmitting unnecessary." << endl;
      }
    },
    [=](send_acks_atom) {
      // a time trigger for sending acks
      if (self->state.unacked > 0)
        send_acks(self, broker);
      self->delayed_send(self, ack_interval_time, send_acks_atom::value);
    },
    [=](recv_atom, reliable_msg& msg) {
      // Incoming message
      if (msg.atm == ping_atom::value || msg.atm == pong_atom::value) {
        // --> APPLICATION
        self->state.unacked += 1;
        auto next = self->state.seq_recv;
        if (msg.seq < next) {
          // OLD
          aout(self) <<  "[R][" << msg.seq << "][>>] " << to_string(msg)
                     << " <-- OLD, awaiting " << next << endl;
          // Sender did not receive ack yet, will be acked automatically
        } else if (msg.seq == next) {
          // EXPECTED
          aout(self) <<  "[R][" << msg.seq << "][>>] " << to_string(msg) << endl;
          self->delayed_send(app, forward_delay, msg.atm, msg.content);
          // ACK will be sent by "send_ack_atom" handler, expecting that all
          // seqs < next_recv have been received ...
          // look for received messages with subsequent sequence numbers
          next += 1;
          auto& early = self->state.inbox;
          auto msg_itr = find_seq(early, next);
          while (msg_itr != end(early)) {
            self->delayed_send(app, forward_delay, msg.atm, msg.content);
            early.erase(msg_itr);
            next += 1;
            msg_itr = find_seq(early, next);
          }
          self->state.seq_recv = next;
        } else if (msg.seq > next) {
          // EARLY
          auto& inbox = self->state.inbox;
          aout(self) << "[R][" << msg.seq << "][>>] " << to_string(msg.atm)
                     << " <-- EARLY, awaiting " << next << endl;
          if (inbox.empty() || inbox.back().seq < msg.seq) {
            inbox.emplace_back(move(msg));
          } else {
            inbox.insert(lower_bound(begin(inbox),end(inbox), msg), move(msg));
          }
        }
        if (self->state.unacked >= ack_interval_count) {
          // TODO: do some acking
          // self->send(self, send_acks_atom::value);
          send_acks(self, broker);
        }
      } else {
        // --> CONTROL
        self->send(self, msg.atm, move(msg));
      }
    },
    [=](atom_value av, int32_t i) {
      // Message from ping actor, forward via our connection handle
      assert(av == ping_atom::value || av == pong_atom::value);
      int32_t seq = self->state.seq_send;
      auto msg = reliable_msg::msg(av, i, seq);
      aout(self) << "[R][" << seq << "][<<] " << to_string(msg) << endl;
      self->send(broker, send_atom::value, msg);
      self->state.outbox.emplace_back(move(msg));
      self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      self->state.seq_send += 1; // increase sequence number for next packet
    }
  };
}

} // namespace relm
