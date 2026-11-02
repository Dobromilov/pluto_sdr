#include "config.hpp"

#include <iostream>
#include <string>

int run_sdr(const config& cfg, int argc, char* argv[]);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "err, pls add type (rx or tx)\n";
        return 1;
    }

    std::string mode = argv[1];
    config cfg;
    if (mode == "tx") {
        cfg.is_tx = true;
        std::cout << "start tx\n";
    } else if (mode == "rx") {
        cfg.is_tx = false;
        std::cout << "start rx\n";
    } else {
        std::cerr << "error type send: " << mode << "\n";
        return 1;
    }

    return run_sdr(cfg, argc, argv);
}