#pragma once

#include <complex>
#include <vector>

// Полная линейная свёртка комплексного сигнала с вещественным фильтром.
std::vector<std::complex<double>> convolve(
    const std::vector<std::complex<double>>& signal,
    const std::vector<double>& filter
);
