
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
auto ack_interval_time = milliseconds(200);
int16_t ack_interval_count = 50;
}

reliable_msg create_ack_msg(reliability_state& state) {
  int32_t highest = state.seq_recv;
  int32_t searching = highest + 1;
  int32_t found = 0;
  size_t idx = 0;
  std::array<int32_t,3> nacks = {{0,0,0}};
  while (found < 3 && idx < state.inbox.size()) {
    auto itr = find_seq(state.inbox, searching);
    if (itr != state.inbox.end()) {
      
    }
    ++searching;
    ++idx;
  }
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
  self->send(broker, send_atom::value, ack_msg);
}

behavior reliability_actor(stateful_actor<reliability_state>* self,
                           const actor& app, const actor& broker) {
  self->set_default_handler(print_and_drop);
  self->delayed_send(self, ack_interval_time, send_acks_atom::value);
  aout(self) << "[R] Bootstrapping done, now running." << endl;
  return {
    [=](ack_atom, int32_t seq, int32_t num_nacks, array<int32_t,3> nacks) {
      // ack all le to seq
      auto& outbox = self->state.outbox;
      aout(self) << "[R][" << seq << "] Received ACK." << endl;
      erase(remove_if(begin(outbox), end(outbox),
                      [seq,num_nacks,nacks](const reliable_msg& msg) {
        return msg.seq <= seq 
      }));
    },
    [=](retransmit_atom, int32_t seq) {
      // May be easier to look for the next retransmit and set a timer
      // for it instead of using a separate timers for each possible retransmit.
      auto& outbox = self->state.outbox;
      auto msg_itr = find_seq(outbox, seq);
      if (msg_itr != end(outbox)) {
        self->send(broker, send_atom::value, *msg_itr);
        aout(self) << "[R][" << seq << "] Retransmitting." << endl;
        self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      } else {
        aout(self) << "[R][" << seq << "] No retransmit necessary." << endl;
      }
    },
    [=](send_acks_atom) {
      // a time trigger for sending acks
      send_acks(self, broker);
      self->delayed_send(self, ack_interval_time, send_acks_atom::value, true);
    },
    [=](recv_atom, const reliable_msg& msg) {
      // Incoming message
      if (msg.atm == ping_atom::value || msg.atm == pong_atom::value) {
        // --> APPLICATION
        self->state.unacked += 1;
        auto next = self->state.seq_recv + 1;
        if (msg.seq < next) {
          aout(self) <<  "[R][" << msg.seq << "] Message " << to_string(msg)
                   << " has old sequence number, adding to pending." << endl;
          // Sender did not receive ack yet, will be acked automatically
        } else if (msg.seq == next) {
          aout(self) <<  "[R][" << msg.seq << "] Received " << to_string(msg)
                     << "." << endl;
          self->delayed_send(app, forward_delay, msg.atm, msg.content);
          // ACK will be sent by "send_ack_atom" handler, expecting that all
          // seqs < next_recv have been received ...
          // look for received messages with subsequent sequence numbers
          auto& early = self->state.inbox;
          auto msg_itr = find_seq(early, next);
          while (msg_itr != end(early)) {
            self->delayed_send(app, forward_delay, msg.atm, msg.content);
            early.erase(msg_itr);
            next += 1;
            msg_itr = find_seq(early, next);
          }
          self->state.seq_recv += next;
        } else if (msg.seq > next) {
          aout(self) << "[R][" << msg.seq << "] Message " << to_string(msg.atm)
                     << " was early, awaiting " << next << "." << endl;
          self->state.inbox.emplace_back(msg);
        }
        if (self->state.unacked >= ack_interval_count) {
          // TODO: do some acking
          // self->send(self, send_acks_atom::value);
          send_acks(self, broker);
        }
      } else {
        // --> CONTROL
        self->send(self, msg.atm, msg.seq);
      }
    },
    [=](atom_value av, int32_t i) {
      // Message from ping actor, forward via our connection handle
      assert(av == ping_atom::value || av == pong_atom::value);
      auto next = self->state.seq_send + 1;
      int32_t seq = next;
      auto msg = reliable_msg::msg(av, i, seq);
      aout(self) << "[R][" << seq << "] Sending " << to_string(msg)
                 << "." << endl;
      self->send(broker, send_atom::value, msg);
      self->state.outbox.emplace_back(msg);
      self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      self->state.seq_send = next; // increase sequence number for next packet
    }
  };
}

} // namespace relm
