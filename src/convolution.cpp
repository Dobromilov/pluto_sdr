#include "convolution.hpp"

#include <cstddef>

std::vector<std::complex<double>> convolve(
    const std::vector<std::complex<double>>& signal,
    const std::vector<double>& filter
) {
    if (signal.empty() || filter.empty()) {
        return {};
    }

    // При полной свёртке длина результата равна N + M - 1.
    std::vector<std::complex<double>> result(
        signal.size() + filter.size() - 1,
        std::complex<double>(0.0, 0.0)
    );

    // Каждый входной отсчёт добавляет свою масштабированную копию фильтра.
    for (std::size_t i = 0; i < signal.size(); ++i) {
        for (std::size_t j = 0; j < filter.size(); ++j) {
            result[i + j] += signal[i] * filter[j];
        }
    }

    return result;
}
