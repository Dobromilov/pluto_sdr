#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static SoapySDRDevice* open_pluto(const char* uri)
{
    const std::string args = std::string("driver=plutosdr,uri=") + uri;
    SoapySDRDevice* sdr = SoapySDRDevice_makeStrArgs(args.c_str());

    if (sdr == nullptr)
        std::fprintf(stderr, "Не удалось открыть %s: %s\n",
                     uri, SoapySDRDevice_lastError());

    return sdr;
}

static bool check_result(int result, const char* operation)
{
    if (result == 0) return true;

    std::fprintf(stderr, "%s: %s\n", operation, SoapySDRDevice_lastError());
    return false;
}

int main(){
    constexpr size_t channel = 0;
    constexpr double sample_rate = 1e6;
    constexpr double carrier_frequency = 1600e6;
    constexpr double rx_gain = 20.0;
    constexpr long timeout_us = 400000;
    constexpr size_t iteration_count = 50; // сколько буферов принять

    SoapySDRDevice* sdr = open_pluto("usb:2.5.5");
    if (sdr == nullptr) return 1;

    const bool configured =
        check_result(
            SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, channel, sample_rate),
            "Не удалось задать частоту дискретизации RX") &&
        check_result(
            SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, channel,
                                        carrier_frequency, nullptr),
            "Не удалось задать несущую частоту RX") &&
        check_result(
            SoapySDRDevice_setGainMode(sdr, SOAPY_SDR_RX, channel, false),
            "Не удалось включить ручное усиление RX") &&
        check_result(
            SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channel, rx_gain),
            "Не удалось задать усиление RX");

    if (!configured)
    {
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    // Создаём RX-поток (CS16: пары int16_t -> I, Q).
    size_t channels[] = {channel};
    SoapySDRStream* rx_stream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, 1, nullptr);

    if (rx_stream == nullptr)
    {
        std::fprintf(stderr, "Ошибка создания RX-потока: %s\n",
                     SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    const size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rx_stream);

    // Буфер: 2 слова на комплексный сэмпл (I, Q).
    std::vector<int16_t> rx_buffer(2 * rx_mtu);

    // Файл для записи «сырых» I/Q.
    std::FILE* file = std::fopen("rxdata.pcm", "wb");
    if (file == nullptr)
    {
        std::perror("Не удалось открыть rxdata.pcm");
        SoapySDRDevice_closeStream(sdr, rx_stream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    if (!check_result(
            SoapySDRDevice_activateStream(sdr, rx_stream, 0, 0, 0),
            "Не удалось активировать RX-поток"))
    {
        std::fclose(file);
        SoapySDRDevice_closeStream(sdr, rx_stream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    std::printf("RX Pluto готов: MTU=%zu, пишу в rxdata.pcm\n", rx_mtu);

    long long last_time_ns = 0;

    for (size_t buffers_read = 0; buffers_read < iteration_count; ++buffers_read)
    {
        void* rx_buffers[] = {rx_buffer.data()};
        int rx_flags = 0;
        long long time_ns = 0;

        const int samples_read = SoapySDRDevice_readStream(
            sdr, rx_stream, rx_buffers, rx_mtu,
            &rx_flags, &time_ns, timeout_us);

        if (samples_read < 0)
        {
            std::fprintf(stderr, "RX ошибка: %d\n", samples_read);
            continue;
        }

        const long long time_difference =
            last_time_ns == 0 ? 0 : time_ns - last_time_ns;

        std::printf(
            "Buffer: %zu - Samples: %d, Flags: %d, "
            "Time: %lld, TimeDiff: %lld\n",
            buffers_read, samples_read, rx_flags,
            time_ns, time_difference);

        // Пишем только реально полученные комплексные сэмплы (2 слова на сэмпл).
        const size_t words_to_write = 2 * static_cast<size_t>(samples_read);
        const size_t words_written = std::fwrite(
            rx_buffer.data(), sizeof(int16_t), words_to_write, file);
        if (words_written != words_to_write)
        {
            std::perror("Ошибка записи rxdata.pcm");
            break;
        }

        last_time_ns = time_ns;
    }

    std::fclose(file);

    SoapySDRDevice_deactivateStream(sdr, rx_stream, 0, 0);
    SoapySDRDevice_closeStream(sdr, rx_stream);
    SoapySDRDevice_unmake(sdr);

    return 0;
}