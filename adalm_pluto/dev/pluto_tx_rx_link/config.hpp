#pragma once

#include <cstddef>

#define SIZE_BUF 1920

struct config {
    bool is_tx;
    size_t channel = 0;
    double sample_rate = 1e6;
    double freq1 = 800e6;
    double freq2 = 850e6;
    double rx_gain = 30.0;
    double tx_gain = 40.0;
    long timeout_us = 400000;
    int direct = 1;
    size_t timestamp_every = 1920;
    int loopback = 0;
    size_t iteration_count = 100;
};
