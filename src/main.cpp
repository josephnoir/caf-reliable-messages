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

#include "include/reliable_broker.hpp"

using namespace std;
using namespace caf;

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

int main(int argc, char** argv) {
  config cfg;
  // read CLI options
  cfg.parse(argc, argv);
  // return immediately if a help text was printed
  if (cfg.cli_helptext_printed)
    return 0;
  // load modules
  cfg.load<io::middleman>();
  // create actor system and call caf_main
  actor_system system{cfg};
}
