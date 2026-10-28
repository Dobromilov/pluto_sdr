from pathlib import Path
import sys

import numpy as np
import matplotlib.pyplot as plt

# Файл, который пишет наш RX. Другой файл можно передать первым аргументом.
name = Path.home() / "Desktop/SDR/adalm_pluto/dev/build/rx_samples.pcm"
if len(sys.argv) > 1:
    name = Path(sys.argv[1]).expanduser()

if not name.is_file():
    raise SystemExit(f"Нет файла: {name}")

# Быстрое чтение всего файла как массива int16 (little-endian).
raw = np.fromfile(name, dtype="<i2")

# Если запись ещё идёт или файл оборван, используем только полные пары I/Q.
count = len(raw) // 2
if count == 0:
    raise SystemExit("В файле нет полных пар I/Q")

raw = raw[: count * 2]
real = raw[0::2]  # Чётные индексы — I
imag = raw[1::2]  # Нечётные индексы — Q

# Модуль (амплитуда/огибающая) комплексного сигнала: sqrt(I^2 + Q^2)
mag = np.hypot(real, imag)
samples = np.arange(count)

# Ваш экспорт в C++-массив.
out = Path(__file__).resolve().with_name("out.txt")
with open(out, "w", encoding="utf-8") as file:
    file.write("static std::vector<std::complex<double>> test_rx_samples = {\n")
    for i in range(count):
        file.write(f"{{{real[i]}, {imag[i]}}},")
        if (i + 1) % 20 == 0:
            file.write("\n")
    file.write("\n};\n")
print(f"Сэмплов: {count}")
print(f"C++-массив: {out}")

# Отрисовка I/Q и модуля: синий — I, красный — Q, зелёный — |IQ|.
fig, ax = plt.subplots(figsize=(12, 5))
ax.plot(samples, imag, color="red", label="Q (imag)", linewidth=0.6, alpha=0.7)
ax.plot(samples, real, color="blue", label="I (real)", linewidth=0.6, alpha=0.7)
ax.plot(samples, mag, color="green", label="|IQ| (модуль)", linewidth=0.9)
ax.set_xlabel("Номер комплексного сэмпла")
ax.set_ylabel("Амплитуда, int16")
ax.set_title(f"RX: I/Q и модуль из {name.name}")
ax.grid(True, alpha=0.3)
ax.legend()
fig.tight_layout()
plot = Path(__file__).resolve().with_name("rx_iq.png")
fig.savefig(plot, dpi=160)
print(f"График: {plot}")
plt.show()
