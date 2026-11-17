"""Восстановление BPSK или QPSK из записи CS16 по преамбуле TX."""

import argparse
from pathlib import Path

import numpy as np


def read_iq(path):
    iq = np.loadtxt(path, delimiter=",", skiprows=1, ndmin=2)
    if iq.shape[1] != 2 or not np.isfinite(iq).all():
        raise ValueError(f"Ожидаются две колонки I,Q с конечными значениями: {path}")
    return iq[:, 0] + 1j * iq[:, 1]


def find_packet(capture, preamble, samples_per_symbol, packet_size, phase_power=2):
    best_score = 0.0
    best_packet = None
    best_start = 0
    best_frequency = 0.0
    preamble_energy = np.vdot(preamble, preamble).real
    start_count = len(capture) - packet_size + 1
    block_size = 128 * 1024

    for offset in range(0, start_count, block_size):
        count = min(block_size, start_count - offset)
        # Захватываем хвост, чтобы проверять пакеты на границе блоков.
        iq = capture[offset:offset + count + packet_size - 1]
        samples = iq[:, 0] + 1j * iq[:, 1]
        samples -= samples.mean()

        # Квадрат для BPSK или четвёртая степень для QPSK убирают фазу модуляции.
        powered = samples ** phase_power
        lag = samples_per_symbol
        frequency = np.angle(np.vdot(powered[:-lag], powered[lag:])) / (phase_power * lag)
        samples *= np.exp(-1j * frequency * np.arange(samples.size))

        # Корреляция ищет преамбулу в любом месте, независимо от границ символов.
        kernel = np.conj(preamble[::-1])
        fft_size = 1 << (samples.size + kernel.size - 2).bit_length()
        correlation = np.fft.ifft(np.fft.fft(samples, fft_size) *
                                 np.fft.fft(kernel, fft_size))
        correlation = correlation[kernel.size - 1:kernel.size - 1 + count]

        energy = np.r_[0.0, np.cumsum(np.abs(samples) ** 2)]
        energy = energy[preamble.size:preamble.size + count] - energy[:count]
        score = np.zeros(count)
        np.divide(np.abs(correlation), np.sqrt(preamble_energy * np.maximum(energy, 0)),
                  out=score, where=energy > 0)
        strongest = int(np.argmax(score))
        if score[strongest] > best_score:
            best_score = float(score[strongest])
            best_packet = samples[strongest:strongest + packet_size].copy()
            best_start = offset + strongest
            best_frequency = frequency

    return best_packet, best_start, best_score, best_frequency


def recover_symbols(packet, reference, samples_per_symbol):
    # Среднее по символу совпадает с прямоугольным согласованным фильтром
    # при выборке в конце каждого интервала из samples_per_symbol отсчётов.
    pilot = packet[:reference.size * samples_per_symbol]
    pilot = pilot.reshape(-1, samples_per_symbol).mean(axis=1) * np.conj(reference)
    lag = min(8, pilot.size - 1)
    fine_frequency = np.angle(np.vdot(pilot[:-lag], pilot[lag:])) / (lag * samples_per_symbol)
    corrected = packet * np.exp(-1j * fine_frequency * np.arange(packet.size))
    symbols = corrected.reshape(-1, samples_per_symbol).mean(axis=1)
    gain = np.vdot(reference, symbols[:reference.size]) / np.vdot(reference, reference).real
    if abs(gain) < 1e-12:
        raise ValueError("Не удалось оценить амплитуду по преамбуле")
    return symbols[reference.size:] / gain, fine_frequency


def demap_symbols(symbols, use_qpsk):
    in_phase = symbols.real < 0
    if use_qpsk:
        quadrature = symbols.imag < 0
        return np.column_stack((in_phase, quadrature)).ravel()
    return in_phase


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="Запись RX в формате CS16")
    parser.add_argument("--tx-dir", type=Path, help="Папка с tx_preamble.csv и tx_symbols.csv")
    modulation = parser.add_mutually_exclusive_group()
    modulation.add_argument("--bpsk", dest="qpsk", action="store_false",
                            help="Декодировать данные BPSK (по умолчанию)")
    modulation.add_argument("--qpsk", action="store_true", help="Декодировать данные QPSK")
    parser.set_defaults(qpsk=False)
    parser.add_argument("--samples-per-symbol", type=int, default=4)
    parser.add_argument("--sample-rate", type=float, default=2e6)
    parser.add_argument("--min-score", type=float, default=0.55)
    args = parser.parse_args()
    if (args.samples_per_symbol < 1 or not np.isfinite(args.sample_rate) or
            args.sample_rate <= 0 or not 0 < args.min_score <= 1):
        parser.error("Проверьте значение частоты, отсчётов на символ и порога корреляции")

    capture_path = args.capture.expanduser()
    tx_dir = args.tx_dir.expanduser() if args.tx_dir else capture_path.parent
    try:
        preamble = read_iq(tx_dir / "tx_preamble.csv")
        expected = read_iq(tx_dir / "tx_symbols.csv")
        if args.qpsk and (np.any(expected.real == 0) or np.any(expected.imag == 0)):
            raise ValueError("Для --qpsk нужны эталонные tx_symbols.csv от передатчика QPSK")
        if not args.qpsk and np.any(expected.imag != 0):
            raise ValueError("Эталон tx_symbols.csv содержит Q-компоненту. Для QPSK укажите --qpsk")
        sps = args.samples_per_symbol
        if preamble.size < 2 * sps or preamble.size % sps or not np.any(preamble):
            raise ValueError("Длина преамбулы должна содержать целое число символов (минимум два)")
        packet_size = preamble.size + expected.size * sps
        sample_count = capture_path.stat().st_size // 4
        if sample_count < packet_size:
            raise ValueError("Запись короче одного пакета")
        capture = np.memmap(capture_path, dtype="<i2", mode="r", shape=(sample_count, 2))
        phase_power = 4 if args.qpsk else 2
        packet, start, score, coarse = find_packet(capture, preamble, sps, packet_size, phase_power)
        if packet is None or score < args.min_score:
            raise ValueError(f"Пакет не найден: корреляция {score:.3f}, порог {args.min_score:.3f}")

        symbols, fine = recover_symbols(packet, preamble[::sps], sps)
        bits = demap_symbols(symbols, args.qpsk)
        expected_bits = demap_symbols(expected, args.qpsk)
        errors = np.count_nonzero(bits != expected_bits)
        output = capture_path.with_name("rx_symbols.csv")
        np.savetxt(output, np.column_stack((symbols.real, symbols.imag)),
                   delimiter=",", header="I,Q", comments="", fmt="%.10g")
        capture_path.with_name("rx_bits.txt").write_text(
            "".join("1" if bit else "0" for bit in bits) + "\n", encoding="ascii")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Ошибка декодирования: {error}\n")

    print(f"Пакет: отсчёт {start}, корреляция {score:.3f}")
    print(f"Смещение частоты: {(coarse + fine) * args.sample_rate / (2 * np.pi):.1f} Гц")
    print(f"Ошибки: {errors}/{bits.size} бит")
    if bits.size % 8 == 0:
        print("Текст:", np.packbits(bits).tobytes().decode("utf-8", errors="replace"))
    print(f"Принятые символы: {output}")


if __name__ == "__main__":
    main()
