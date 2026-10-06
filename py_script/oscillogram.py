import numpy as np
import matplotlib.pyplot as plt

FS = 1e6  # 1 MSPS

raw = np.fromfile("../adalm_pluto/dev/build/rxdata.pcm", dtype="<i2")
iq  = raw.reshape(-1, 2)
i   = iq[:, 0].astype(np.float32)
q   = iq[:, 1].astype(np.float32)

n = np.arange(len(i)) / FS * 1e3  # мс

plt.figure(figsize=(12, 5))
plt.plot(n, i, label="I", linewidth=0.7)
plt.plot(n, q, label="Q", linewidth=0.7)
plt.xlabel("Время, мс")
plt.ylabel("Амплитуда (отсчёты АЦП)")
plt.title("Осциллограмма I/Q")
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.show()