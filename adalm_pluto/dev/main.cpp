#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>

#include <cstdint>
#include <cstdio>
#include <vector>

static bool check_result(const int result, const char *operation)
{
    if (result == 0) return true;

    std::fprintf(stderr, "%s: %s\n", operation, SoapySDRDevice_lastError());
    return false;
}

int main(){
    constexpr size_t channel = 0;
    constexpr double sample_rate = 1e6;
    constexpr double carrier_frequency = 800e6;
    constexpr double rx_gain = 10.0;
    constexpr double tx_gain = -89.0;
    constexpr long timeout_us = 400000;
    constexpr size_t iteration_count = 10;

    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", "usb:");
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");
    SoapySDRKwargs_set(&args, "loopback", "0");

    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (sdr == nullptr){
        std::fprintf(stderr, "Не удалось открыть PlutoSDR: %s\n",
                     SoapySDRDevice_lastError());
        return 1;
    }

    // Настройка RX и TX.
    if (!check_result(
            SoapySDRDevice_setSampleRate(
                sdr, SOAPY_SDR_RX, channel, sample_rate),
            "Не удалось задать частоту дискретизации RX") ||
        !check_result(
            SoapySDRDevice_setFrequency(
                sdr, SOAPY_SDR_RX, channel, carrier_frequency, nullptr),
            "Не удалось задать несущую частоту RX") ||
        !check_result(
            SoapySDRDevice_setGainMode(
                sdr, SOAPY_SDR_RX, channel, false),
            "Не удалось включить ручное усиление RX") ||
        !check_result(
            SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channel, rx_gain),
            "Не удалось задать усиление RX") ||
        !check_result(
            SoapySDRDevice_setSampleRate(
                sdr, SOAPY_SDR_TX, channel, sample_rate),
            "Не удалось задать частоту дискретизации TX") ||
        !check_result(
            SoapySDRDevice_setFrequency(
                sdr, SOAPY_SDR_TX, channel, carrier_frequency, nullptr),
            "Не удалось задать несущую частоту TX") ||
        !check_result(
            SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channel, tx_gain),
            "Не удалось задать усиление TX"))
    {
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    SoapySDRStream *rx_stream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, &channel, 1, nullptr);
    SoapySDRStream *tx_stream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, &channel, 1, nullptr);

    if (rx_stream == nullptr || tx_stream == nullptr)
    {
        std::fprintf(stderr, "Ошибка создания потока: %s\n",
                     SoapySDRDevice_lastError());
        if (rx_stream != nullptr)
            SoapySDRDevice_closeStream(sdr, rx_stream);
        if (tx_stream != nullptr)
            SoapySDRDevice_closeStream(sdr, tx_stream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    const size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rx_stream);
    const size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, tx_stream);

    if (tx_mtu < 6)
    {
        std::fprintf(stderr, "TX MTU слишком мал для временной метки: %zu\n",
                     tx_mtu);
        SoapySDRDevice_closeStream(sdr, rx_stream);
        SoapySDRDevice_closeStream(sdr, tx_stream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    // CS16: каждый комплексный сэмпл состоит из двух int16_t — I и Q.
    std::vector<int16_t> rx_buffer(2 * rx_mtu);
    std::vector<int16_t> tx_buffer(2 * tx_mtu, 0);

    // Первые 12 слов TX-буфера используются под два маркера и timestamp.
    tx_buffer[0] = static_cast<int16_t>(0xffff);
    tx_buffer[1] = static_cast<int16_t>(0xffff);
    tx_buffer[10] = static_cast<int16_t>(0xffff);
    tx_buffer[11] = static_cast<int16_t>(0xffff);

    // Остальная часть содержит постоянные I/Q. Для 12-битного DAC значения
    // располагаются в старших 12 битах 16-битного слова.
    for (size_t i = 12; i + 1 < tx_buffer.size(); i += 2)
    {
        tx_buffer[i] = static_cast<int16_t>(1500 << 4);     // I
        tx_buffer[i + 1] = static_cast<int16_t>(1500 << 4); // Q
    }

    std::FILE *file = std::fopen("rxdata.pcm", "wb");
    if (file == nullptr)
    {
        std::perror("Не удалось открыть rxdata.pcm");
        SoapySDRDevice_closeStream(sdr, rx_stream);
        SoapySDRDevice_closeStream(sdr, tx_stream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    if (!check_result(
            SoapySDRDevice_activateStream(sdr, rx_stream, 0, 0, 0),
            "Не удалось активировать RX-поток"))
    {
        std::fclose(file);
        SoapySDRDevice_closeStream(sdr, rx_stream);
        SoapySDRDevice_closeStream(sdr, tx_stream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    if (!check_result(
            SoapySDRDevice_activateStream(sdr, tx_stream, 0, 0, 0),
            "Не удалось активировать TX-поток"))
    {
        SoapySDRDevice_deactivateStream(sdr, rx_stream, 0, 0);
        std::fclose(file);
        SoapySDRDevice_closeStream(sdr, rx_stream);
        SoapySDRDevice_closeStream(sdr, tx_stream);
        SoapySDRDevice_unmake(sdr);
        return 1;
    }

    std::printf("PlutoSDR настроен: RX MTU=%zu, TX MTU=%zu\n",
                rx_mtu, tx_mtu);

    long long last_time_ns = 0;

    for (size_t buffers_read = 0;
         buffers_read < iteration_count;
         ++buffers_read)
    {
        void *rx_buffers[] = {rx_buffer.data()};
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

        // Записываем только реально полученные комплексные сэмплы.
        const size_t words_to_write =
            2 * static_cast<size_t>(samples_read);
        const size_t words_written = std::fwrite(
            rx_buffer.data(), sizeof(int16_t), words_to_write, file);
        if (words_written != words_to_write)
        {
            std::perror("Ошибка записи rxdata.pcm");
            break;
        }

        last_time_ns = time_ns;

        // Как в методичке: отправляем один буфер на третьей итерации.
        if (buffers_read == 2)
        {
            if ((rx_flags & SOAPY_SDR_HAS_TIME) == 0)
            {
                std::fprintf(stderr,
                             "RX-буфер не содержит аппаратный timestamp\n");
                continue;
            }

            // Планируем передачу через 4 мс после времени RX-буфера.
            const long long tx_time_ns = time_ns + 4LL * 1000 * 1000;

            // 64-битный timestamp разбивается на восемь байтов. Каждый байт
            // помещается в старшие биты отдельного 16-битного DAC-слова.
            for (size_t i = 0; i < 8; ++i)
            {
                const uint8_t byte = static_cast<uint8_t>(
                    (static_cast<uint64_t>(tx_time_ns) >> (i * 8)) & 0xff);
                tx_buffer[2 + i] = static_cast<int16_t>(byte << 4);
            }

            const void *tx_buffers[] = {tx_buffer.data()};
            int tx_flags = SOAPY_SDR_HAS_TIME;

            const int samples_sent = SoapySDRDevice_writeStream(
                sdr, tx_stream, tx_buffers, tx_mtu,
                &tx_flags, tx_time_ns, timeout_us);

            std::printf("TX: sent %d / %zu samples\n",
                        samples_sent, tx_mtu);

            if (samples_sent != static_cast<int>(tx_mtu))
            {
                std::fprintf(stderr,
                             "TX передал не весь буфер: %d из %zu\n",
                             samples_sent, tx_mtu);
            }
        }
    }

    std::fclose(file);

    SoapySDRDevice_deactivateStream(sdr, rx_stream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, tx_stream, 0, 0);
    SoapySDRDevice_closeStream(sdr, rx_stream);
    SoapySDRDevice_closeStream(sdr, tx_stream);
    SoapySDRDevice_unmake(sdr);

    return 0;
}
