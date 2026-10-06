#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>

#include <cmath>
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
    constexpr double tx_gain = 20.0;
    constexpr long timeout_us = 400000;
    constexpr size_t iteration_count = 100; // сколько буферов отправить

    // Частота тона относительно центра (0 Гц). 100 кГц -> пик справа от центра.
    constexpr double tone_offset_hz = 100e3;

    SoapySDRDevice* sdr = open_pluto("usb:2.6.5");
    if (sdr == nullptr) return 1;

    const bool configured =
        check_result(
            SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, channel, sample_rate),
            "Не удалось задать частоту дискретизации TX") &&
        check_result(
            SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, channel,
                                        carrier_frequency, nullptr),
            "Не удалось задать несущую частоту TX") &&
        check_result(
            SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channel, tx_gain),
            "Не удалось задать усиление TX");

    if (!configured)
    {
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    // Создаём TX-поток (CS16: пары int16_t -> I, Q).
    size_t channels[] = {channel};
    SoapySDRStream* tx_stream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, 1, nullptr);

    if (tx_stream == nullptr)
    {
        std::fprintf(stderr, "Ошибка создания TX-потока: %s\n",
                     SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    const size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, tx_stream);

    if (tx_mtu < 6)
    {
        std::fprintf(stderr, "TX MTU слишком мал для шапки: %zu\n", tx_mtu);
        SoapySDRDevice_closeStream(sdr, tx_stream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    // Буфер: 2 слова на комплексный сэмпл (I, Q).
    std::vector<int16_t> tx_buffer(2 * tx_mtu, 0);

    // Шапка: маркеры FFFF FFFF ... FFFF FFFF вокруг 8 байт timestamp.
    tx_buffer[0]  = static_cast<int16_t>(0xffff);
    tx_buffer[1]  = static_cast<int16_t>(0xffff);
    tx_buffer[10] = static_cast<int16_t>(0xffff);
    tx_buffer[11] = static_cast<int16_t>(0xffff);

    // Полезная часть: комплексный тон на tone_offset_hz от центра.
    // Фаза накапливается между буферами, чтобы не было разрывов на спектре.
    const double dphase = 2.0 * M_PI * tone_offset_hz / sample_rate;
    double phase = 0.0;
    const double amplitude = 24000.0; // запас до 32767

    auto fill_payload = [&]() {
        for (size_t i = 12; i + 1 < tx_buffer.size(); i += 2)
        {
            const int16_t i_val = static_cast<int16_t>(std::cos(phase) * amplitude);
            const int16_t q_val = static_cast<int16_t>(std::sin(phase) * amplitude);

            // 12-битный DAC AD9363 -> значения в старших 12 битах 16-битного слова.
            tx_buffer[i]     = i_val;
            tx_buffer[i + 1] = q_val;
            phase += dphase;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        }
    };

    if (!check_result(
            SoapySDRDevice_activateStream(sdr, tx_stream, 0, 0, 0),
            "Не удалось активировать TX-поток"))
    {
        SoapySDRDevice_closeStream(sdr, tx_stream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    std::printf("TX Pluto готов: MTU=%zu, тон=%.0f Гц\n", tx_mtu, tone_offset_hz);

    for (size_t n = 0; n < iteration_count; ++n){
        for (auto& sample : tx_buffer)
            sample = 0;

        if (n % 2 == 0)
            fill_payload();

        const void* tx_buffers[] = {tx_buffer.data()};
        int tx_flags = 0;

        std::fprintf(stderr, "TX буфер %zu: начинаю\n", n);

        const int sent = SoapySDRDevice_writeStream(
            sdr, tx_stream, tx_buffers, tx_mtu,
            &tx_flags, 0, timeout_us);

        std::fprintf(stderr, "TX буфер %zu: записано %d\n", n, sent);

        if (sent != static_cast<int>(tx_mtu))
            break;
        }
    SoapySDRDevice_deactivateStream(sdr, tx_stream, 0, 0);
    SoapySDRDevice_closeStream(sdr, tx_stream);
    SoapySDRDevice_unmake(sdr);

    return 0;
}