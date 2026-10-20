from pathlib import Path
import sys

import numpy as np
import matplotlib.pyplot as plt

# Файл, который пишет наш RX. Другой файл можно передать первым аргументом.
name = Path.home() / "Desktop/SDR/adalm_pluto/dev/build/rx_samples.pcm"
if len(sys.argv) > 1:
    name = Path(sys.argv[1]).expanduser()

# Номера комплексных сэмплов, как в вашем примере: [start, stop).
start = 149000
stop = 243000

if not name.is_file():
    raise SystemExit(f"Нет файла: {name}")

sample_count = name.stat().st_size // 4  # I: 2 байта, Q: 2 байта
if start < 0 or stop <= start or start >= sample_count:
    raise SystemExit(f"Неверный участок [{start}, {stop}); в файле {sample_count} сэмплов")
stop = min(stop, sample_count)

real = []
imag = []
with open(name, "rb") as f:
    f.seek(start * 4)  # Пропускаем данные до выбранного участка.
    index = 0
    while index < 2 * (stop - start) and (byte := f.read(2)):
        if len(byte) != 2:
            break
        value = int.from_bytes(byte, byteorder="little", signed=True)
        if index % 2 == 0:
            real.append(value)  # I
        else:
            imag.append(value)  # Q
        index += 1

# Если запись ещё идёт или файл оборван, используем только полные пары I/Q.
count = min(len(real), len(imag))
real = real[:count]
imag = imag[:count]
if count == 0:
    raise SystemExit("В выбранном участке нет полных пар I/Q")
samples = np.arange(start, start + count)

# Ваш экспорт выбранного участка в C++-массив.
out = Path(__file__).resolve().with_name("out.txt")
with open(out, "w", encoding="utf-8") as file:
    file.write("static std::vector<std::complex<double>> test_rx_samples = {\n")
    for i in range(count):
        file.write("{" + str(real[i]) + ", " + str(imag[i]) + "},")
        if (i + 1) % 20 == 0:
            file.write("\n")
    file.write("\n};\n")
print(f"Сэмплов: {count}; участок [{start}, {start + count})")
print(f"C++-массив: {out}")

# Отрисовка I/Q: синий — I, красный — Q.
fig, ax = plt.subplots(figsize=(12, 5))
ax.plot(samples, imag, color="red", label="Q (imag)", linewidth=0.6)
ax.plot(samples, real, color="blue", label="I (real)", linewidth=0.6)
ax.set_xlabel("Номер комплексного сэмпла")
ax.set_ylabel("Амплитуда, int16")
ax.set_title(f"RX: I/Q из {name.name}")
ax.grid(True, alpha=0.3)
ax.legend()
fig.tight_layout()
plot = Path(__file__).resolve().with_name("rx_iq.png")
fig.savefig(plot, dpi=160)
print(f"График: {plot}")
plt.show()
