#include "server.hpp"

#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    std::string docroot = argc > 2 ? argv[2] : "www";
    run_server(port, docroot);
}
