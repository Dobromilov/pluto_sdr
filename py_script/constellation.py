"""Отрисовка комплексных значений из CSV с колонками I,Q."""

import argparse
import csv
import math
from pathlib import Path


def read_iq(path):
    real = []
    imag = []
    with path.open(encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file)
        if reader.fieldnames != ["I", "Q"]:
            raise ValueError("Ожидается CSV с заголовком I,Q")
        for row in reader:
            i, q = float(row["I"]), float(row["Q"])
            if not math.isfinite(i) or not math.isfinite(q):
                raise ValueError("Значения I и Q должны быть конечными числами")
            real.append(i)
            imag.append(q)
    if not real:
        raise ValueError("В файле нет комплексных отсчётов")
    return real, imag


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "file", nargs="?", type=Path,
        default=Path(__file__).resolve().parent.parent / "build/tx_symbols.csv",
        help="CSV с колонками I,Q (по умолчанию build/tx_symbols.csv)",
    )
    parser.add_argument("--output", type=Path, help="Путь для сохранения PNG")
    parser.add_argument("--no-show", action="store_true", help="Сохранить PNG без открытия окна")
    args = parser.parse_args()
    path = args.file.expanduser()

    try:
        real, imag = read_iq(path)
    except (OSError, ValueError, TypeError, KeyError, csv.Error) as error:
        parser.exit(1, f"Не удалось прочитать {path}: {error}\n")

    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(6, 6))
    ax.scatter(real, imag, s=35, alpha=0.65)
    ax.axhline(0, color="gray", linewidth=0.7)
    ax.axvline(0, color="gray", linewidth=0.7)
    ax.set_xlabel("I — действительная часть")
    ax.set_ylabel("Q — мнимая часть")
    ax.set_title(f"Созвездие: {path.stem}\nКомплексных отсчётов: {len(real)}")
    ax.grid(True, alpha=0.3)
    ax.set_aspect("equal", adjustable="box")

    # Одинаковый масштаб осей сохраняет геометрию созвездия.
    limit = max(1.0, max(map(abs, real)), max(map(abs, imag))) * 1.25
    ax.set_xlim(-limit, limit)
    ax.set_ylim(-limit, limit)
    fig.tight_layout()

    output = (args.output.expanduser() if args.output else
              path.with_name(f"{path.stem}_constellation.png"))
    fig.savefig(output, dpi=160)
    print(f"График: {output}")
    if not args.no_show:
        plt.show()
    plt.close(fig)


if __name__ == "__main__":
    main()
