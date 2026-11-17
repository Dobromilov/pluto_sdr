#include "config.hpp"
#include "convolution.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int run_sdr(
    const config& cfg, const char* dev_addr,
    const std::vector<std::complex<double>>& tx_samples
);

// Сохраняем комплексные значения в две колонки для Python: I и Q.
bool save_iq_csv(
    const std::string& filename,
    const std::vector<std::complex<double>>& samples
) {
    std::ofstream file(filename);
    if (!file) {
        std::cerr << "Не удалось открыть файл: " << filename << '\n';
        return false;
    }

    file << "I,Q\n" << std::setprecision(17);
    for (const auto& sample : samples) {
        file << sample.real() << ',' << sample.imag() << '\n';
    }

    file.close();
    if (!file) {
        std::cerr << "Ошибка записи: " << filename << '\n';
        return false;
    }
    return true;
}

// генератор бит для передачи статичный, похже станет динамичным
std::vector<bool> bits_generation(){
    std::string text = "Work - DOBROMILOV";
    std::vector<bool> bits;

    for (unsigned char c : text)
        for (int i = 7; i >= 0; --i)
            bits.push_back((c >> i) & 1);
    return bits;
}

// модуляция для BPSK
std::vector<std::complex<double>> bpsk_modulate(
    const std::vector<bool>& bits
) {
    std::vector<std::complex<double>> symbols;

    for (bool bit : bits) {
        if (bit == false) {
            symbols.push_back(std::complex<double>(1.0, 0.0));
        } else {
            symbols.push_back(std::complex<double>(-1.0, 0.0));
        }
    }
    return symbols;
}

// Первый бит пары задаёт знак I, второй — знак Q.
std::vector<std::complex<double>> qpsk_modulate(const std::vector<bool>& bits) {
    const double level = 1.0 / std::sqrt(2.0);
    std::vector<std::complex<double>> symbols;
    symbols.reserve(bits.size() / 2);
    for (std::size_t i = 0; i < bits.size(); i += 2) {
        const double real = bits[i] ? -level : level;
        const double imag = bits[i + 1] ? -level : level;
        symbols.emplace_back(real, imag);
    }
    return symbols;
}

std::vector<std::complex<double>> upsample(
    const std::vector<std::complex<double>>& symbols,
    size_t samples_per_symbol
) {
    if (symbols.empty() || samples_per_symbol == 0) {
        return {};
    }

    std::vector<std::complex<double>> result(
        symbols.size() * samples_per_symbol,
        std::complex<double>(0.0, 0.0)
    );

    for (size_t i = 0; i < symbols.size(); ++i) {
        result[i * samples_per_symbol] = symbols[i];
    }
    return result;
}

int main(int argc, char* argv[]) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode != "tx" && mode != "rx" && mode != "export") {
        std::cerr << "Usage: pluto_sdr tx [URI] [--bpsk | --qpsk]\n"
                  << "       pluto_sdr rx [URI]\n"
                  << "       pluto_sdr export [--bpsk | --qpsk]\n";
        return 1;
    }
    const bool export_only = mode == "export";
    bool use_qpsk = false;
    bool modulation_selected = false;
    const char* dev_addr = "";
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--bpsk" || arg == "--qpsk") && mode != "rx") {
            if (modulation_selected) {
                std::cerr << "Укажи только один флаг модуляции: --bpsk или --qpsk\n";
                return 1;
            }
            use_qpsk = arg == "--qpsk";
            modulation_selected = true;
        } else if (!export_only && dev_addr[0] == '\0' && !arg.empty() && arg[0] != '-') {
            dev_addr = argv[i];
        } else {
            std::cerr << "Недопустимый аргумент для режима " << mode << ": " << arg << '\n';
            return 1;
        }
    }
    config cfg;
    std::vector<std::complex<double>> tx_samples;
    if (mode == "tx" || export_only) {
        cfg.is_tx = true;
        if (!export_only) {
            std::cout << "start tx\n";
        }

        const auto bits = bits_generation();
        if (use_qpsk && bits.size() % 2 != 0) {
            std::cerr << "QPSK требует чётного числа битов\n";
            return 1;
        }
        const auto symbols = use_qpsk ? qpsk_modulate(bits) : bpsk_modulate(bits);
        const size_t samples_per_symbol = cfg.samples_per_symbol;
        const auto upsampled = upsample(symbols, samples_per_symbol);

        // Форма одного прямоугольного импульса: [1, 1, 1, 1].
        const std::vector<double> filter(samples_per_symbol, 1.0);
        const auto shaped = convolve(upsampled, filter);

        // Известная RX последовательность для поиска пакета и оценки фазы.
        constexpr std::uint64_t preamble_word = 0xE4A91C6F85B327D0ULL;
        std::vector<bool> preamble_bits;
        for (int bit = 63; bit >= 0; --bit) {
            preamble_bits.push_back((preamble_word >> bit) & 1U);
        }
        auto preamble = convolve(
            upsample(bpsk_modulate(preamble_bits), samples_per_symbol), filter);

        // Убираем хвост полной свёртки, чтобы текст шёл сразу за преамбулой.
        preamble.resize(preamble_bits.size() * samples_per_symbol);
        tx_samples = preamble;
        tx_samples.insert(tx_samples.end(), shaped.begin(), shaped.end());

        if (!save_iq_csv("tx_symbols.csv", symbols) ||
            !save_iq_csv("tx_upsampled.csv", upsampled) ||
            !save_iq_csv("tx_shaped.csv", shaped) ||
            !save_iq_csv("tx_preamble.csv", preamble)) {
            return 1;
        }
        std::cout << (use_qpsk ? "QPSK: " : "BPSK: ") << bits.size() << " бит, "
                  << tx_samples.size() << " отсчётов в пакете\n";
        if (export_only) {
            std::cout << "Экспортированы tx_preamble.csv и tx_symbols.csv\n";
            return 0;
        }

    } else {
        cfg.is_tx = false;
        std::cout << "start rx\n";
    }

    return run_sdr(cfg, dev_addr, tx_samples);
}
