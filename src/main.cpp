#include <iostream>

#ifdef WIN32
# define _WIN32_WINNT 0x0600
# include <Winsock2.h>
#else
# include <arpa/inet.h> // htonl
#endif

#include <caf/all.hpp>
#include <caf/config.hpp>

#include <caf/io/all.hpp>
#include <caf/io/middleman.hpp>

#include "include/utility.hpp"
#include "include/ping_pong.hpp"
#include "include/reliability_actor.hpp"
#include "include/unreliable_broker.hpp"

using namespace std;
using namespace caf;
using namespace relm;

class config : public actor_system_config {
public:
  uint16_t port = 0;
  std::string host = "localhost";
  bool server_mode = false;

  config() {
    opt_group{custom_options_, "global"}
    .add(port, "port,p", "set port")
    .add(host, "host,H", "set host (ignored in server mode)")
    .add(server_mode, "server-mode,s", "enable server mode");
  }
};

void caf_main(actor_system& system, const config& cfg) {
  if (cfg.server_mode) {
    cout << "run in server mode" << endl;
    auto application = system.spawn(pong);
    auto reliability = system.spawn(init_reliability_actor, application);
    auto server = system.middleman().spawn_server(relm::server, cfg.port,
                                                  reliability);
    if (!server) {
      std::cerr << "failed to spawn server: "
                << system.render(server.error()) << endl;
      return;
    }
    print_on_exit(reliability, "reliability");
    print_on_exit(application, "application");
    print_on_exit(*server, "server");
    return;
  }
  auto application = system.spawn(ping, size_t{42});
  auto reliability = system.spawn(init_reliability_actor, application);
  auto client = system.middleman().spawn_client(broker_impl, cfg.host,
                                                cfg.port, reliability);
  if (!client) {
    std::cerr << "failed to spawn client: "
               << system.render(client.error()) << endl;
    return;
  }
  print_on_exit(application, "application");
  print_on_exit(reliability, "reliability");
  print_on_exit(*client, "client");
  send_as(reliability, application, kickoff_atom::value, reliability);
}

CAF_MAIN(io::middleman)
