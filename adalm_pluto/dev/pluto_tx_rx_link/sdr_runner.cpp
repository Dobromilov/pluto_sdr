#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Errors.h>

#include <cmath>
#include <complex>
#include <csignal>
#include <iostream>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "config.hpp"

static volatile std::sig_atomic_t running = 1;

static void stop_sdr(int) {
    running = 0;
}

static SoapySDRDevice* open_pluto(const config& cfg, const char* dev_addr) {
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", dev_addr[0] ? dev_addr : "usb:");
    // Сначала настраиваем радио через libiio, затем включаем direct в setupStream.
    SoapySDRKwargs_set(&args, "direct", "0");
    SoapySDRKwargs_set(&args, "loopback", std::to_string(cfg.loopback).c_str());
    SoapySDRDevice* sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);
    return sdr;
}

static void fill_tx_buffer(std::vector<int16_t>& buffer, double sample_rate, size_t n) {
    static double phase = 0.0;
    constexpr double tone_offset_hz = 100e3;
    constexpr double amplitude = 16000.0;
    const double dphase = 2.0 * M_PI * tone_offset_hz / sample_rate;

    for (auto& sample : buffer)
        sample = 0;

    if (n % 2 != 0)
        return;

    // Первые 12 слов пропускаем, как в вашем fill_payload.
    for (size_t i = 12; i + 1 < buffer.size(); i += 2) {
        buffer[i] = static_cast<int16_t>(std::cos(phase) * amplitude);
        buffer[i + 1] = static_cast<int16_t>(std::sin(phase) * amplitude);

        phase += dphase;
        if (phase > 2.0 * M_PI)
            phase -= 2.0 * M_PI;
    }
}

static bool check_result(int result, const char* operation) {
    if (result == 0) return true;
    std::fprintf(stderr, "%s: %s\n", operation, SoapySDRDevice_lastError());
    return false;
}

// Вызывается после обработки TIMEOUT и OVERFLOW в цикле.
static bool check_rx_buffer(int sr, int flags) {
    if (sr <= 0 || sr > SIZE_BUF) {
        std::fprintf(stderr, "RX: неожиданный результат %d (%s)\n", sr,
                     sr < 0 ? SoapySDR_errToStr(sr) : "недопустимое число сэмплов");
        return false;
    }
    if ((flags & SOAPY_SDR_HAS_TIME) == 0) {
        std::fprintf(stderr, "RX: буфер без timestamp, flags=%d\n", flags);
        return false;
    }
    return true;
}

int run_sdr(const config& cfg, int argc, char* argv[]) {
    running = 1;
    std::signal(SIGINT, stop_sdr);
    std::signal(SIGTERM, stop_sdr);
    const char* dev_addr = (argc > 2) ? argv[2] : "";
    SoapySDRDevice* sdr = open_pluto(cfg, dev_addr);
    if (!sdr) {
        std::cerr << "Не удалось открыть SDR устройство\n";
        return 1;
    }

    const bool configured =
        check_result(SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, cfg.channel, cfg.sample_rate), "setSampleRate RX") &&
        check_result(SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, cfg.channel, cfg.sample_rate), "setSampleRate TX") &&
        check_result(SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, cfg.channel, cfg.is_tx ? cfg.freq2 : cfg.freq1, NULL), "setFrequency RX") &&
        check_result(SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, cfg.channel, cfg.is_tx ? cfg.freq1 : cfg.freq2, NULL), "setFrequency TX") &&
        check_result(SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, cfg.channel, cfg.rx_gain), "setGain RX") &&
        check_result(SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, cfg.channel, cfg.tx_gain), "setGain TX");
    if (!configured) {
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    // Драйвер может вывести ошибку setSampleRate, но вернуть успех: читаем значение обратно.
    const double rx_rate = SoapySDRDevice_getSampleRate(sdr, SOAPY_SDR_RX, cfg.channel);
    const double tx_rate = SoapySDRDevice_getSampleRate(sdr, SOAPY_SDR_TX, cfg.channel);
    if (!std::isfinite(rx_rate) || !std::isfinite(tx_rate) ||
        std::abs(rx_rate - cfg.sample_rate) > 1.0 || std::abs(tx_rate - cfg.sample_rate) > 1.0) {
        std::fprintf(stderr, "Частота дискретизации не настроена: RX=%.0f, TX=%.0f, нужно %.0f Гц\n",
                     rx_rate, tx_rate, cfg.sample_rate);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    size_t channels[] = { cfg.channel };
    size_t channel_count = 1;
    SoapySDRKwargs stream_args = {};
    SoapySDRKwargs_set(&stream_args, "direct", std::to_string(cfg.direct).c_str());
    SoapySDRKwargs_set(&stream_args, "timestamp_every", std::to_string(cfg.timestamp_every).c_str());
    SoapySDRStream* rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, channel_count, &stream_args);
    if (!rxStream) {
        std::fprintf(stderr, "Не удалось создать RX-поток: %s\n", SoapySDRDevice_lastError());
        SoapySDRKwargs_clear(&stream_args);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    // Оба тракта настроены, но передаёт только узел с is_tx=true.
    SoapySDRStream* txStream = nullptr;
    if (cfg.is_tx) {
        txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, channel_count, &stream_args);
        if (!txStream) {
            std::fprintf(stderr, "Не удалось создать TX-поток: %s\n", SoapySDRDevice_lastError());
            SoapySDRKwargs_clear(&stream_args);
            SoapySDRDevice_closeStream(sdr, rxStream);
            SoapySDRDevice_unmake(sdr);
            return 1;
        }
    }
    SoapySDRKwargs_clear(&stream_args);

    const bool rx_active = check_result(SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0), "activateStream RX");
    const bool tx_active = rx_active && cfg.is_tx && check_result(SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0), "activateStream TX");
    if (!rx_active || (cfg.is_tx && !tx_active)) {
        if (rx_active) SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
        SoapySDRDevice_closeStream(sdr, rxStream);
        if (txStream) SoapySDRDevice_closeStream(sdr, txStream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    std::vector<int16_t> pluto_rx_buffer(2 * SIZE_BUF, 0);
    std::vector<int16_t> pluto_tx_buffer(2 * SIZE_BUF, 0);
    std::vector<std::complex<double>> raw_samples(SIZE_BUF);
    FILE* file = nullptr;
    long long last_time = 0;
    size_t buffers_read = 0;
    int result = 0;

    if (!cfg.is_tx) {
        file = std::fopen("rx_samples.pcm", "wb");
        if (!file) {
            std::perror("Не удалось открыть rx_samples.pcm");
            result = 1;
            running = 0;
        }
    }

    // Один цикл, как на фото: RX -> timestamp -> обработка по is_tx.
    while (running) {
        void* rx_buffs[] = { pluto_rx_buffer.data() };
        int flags = 0;
        long long timeNs = 0;
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, SIZE_BUF, &flags, &timeNs, cfg.timeout_us);
        if (!running) break;
        if (sr == SOAPY_SDR_TIMEOUT) continue;
        if (sr == SOAPY_SDR_OVERFLOW) {
            std::fprintf(stderr, "RX overflow: пропущены сэмплы\n");
            continue;
        }
        if (!check_rx_buffer(sr, flags)) {
            result = 1;
            break;
        }

        if (!cfg.is_tx) {
            std::printf("Buffer: %zu - Samples: %d, Flags: %d, Time: %lld, TimeDiff: %lld\n",
                        buffers_read, sr, flags, timeNs,
                        buffers_read == 0 ? 0LL : timeNs - last_time);
            const size_t words = 2 * static_cast<size_t>(sr);
            if (std::fwrite(pluto_rx_buffer.data(), sizeof(int16_t), words, file) != words) {
                std::perror("Ошибка записи RX");
                result = 1;
                break;
            }
            for (size_t i = 0; i < static_cast<size_t>(sr); i++) {
                raw_samples[i] = std::complex<double>(pluto_rx_buffer[2 * i], pluto_rx_buffer[2 * i + 1]);
            }
        }

        last_time = timeNs;
        long long tx_time = timeNs + (4 * 1000 * 1000);

        if (cfg.is_tx) {
            if (buffers_read % 10 == 0) {
                fill_tx_buffer(pluto_tx_buffer, cfg.sample_rate, buffers_read / 10);
                pluto_tx_buffer[0] = pluto_tx_buffer[1] = static_cast<int16_t>(0xffff);
                pluto_tx_buffer[10] = pluto_tx_buffer[11] = static_cast<int16_t>(0xffff);
                for (size_t i = 0; i < 8; i++) {
                    uint8_t tx_time_byte = (static_cast<uint64_t>(tx_time) >> (i * 8)) & 0xff;
                    pluto_tx_buffer[2 + i] = tx_time_byte << 4;
                }

                const void* tx_buffs[] = { pluto_tx_buffer.data() };
                flags = SOAPY_SDR_HAS_TIME;
                int st = SoapySDRDevice_writeStream(sdr, txStream, tx_buffs, SIZE_BUF, &flags, tx_time, cfg.timeout_us);
                if (st != SIZE_BUF) {
                    std::fprintf(stderr, "TX: результат %d / %d (%s)\n", st, SIZE_BUF,
                                 st < 0 ? SoapySDR_errToStr(st) : "буфер передан не полностью");
                    result = 1;
                    break;
                }
            }
        }
        buffers_read++;
    }

    if (file && std::fclose(file) != 0) {
        std::perror("Ошибка закрытия RX-файла");
        result = 1;
    }
    if (!check_result(SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0), "deactivateStream RX")) result = 1;
    if (tx_active && !check_result(SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0), "deactivateStream TX")) result = 1;
    if (!check_result(SoapySDRDevice_closeStream(sdr, rxStream), "closeStream RX")) result = 1;
    if (txStream && !check_result(SoapySDRDevice_closeStream(sdr, txStream), "closeStream TX")) result = 1;
    if (!check_result(SoapySDRDevice_unmake(sdr), "unmake")) result = 1;
    return result;
}
