import os
import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FILE_PATH = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "build", "Rxdata.pcm"))
ZOOM = 3200000

def load_iq_data(file_path):
    print("Читаю:", file_path)
    if not os.path.exists(file_path):
        raise SystemExit(f"Файл не найден: {file_path}")

    raw = np.fromfile(file_path, dtype="<i2")
    if raw.size == 0:
        raise SystemExit(f"Файл {file_path} пуст")

    real = raw[0::2].astype(np.float64)
    imag = raw[1::2].astype(np.float64)
    n = min(len(real), len(imag))

    return real[:n], imag[:n]

def plot_signals(real, imag, zoom_limit):
    n = len(real)
    m = min(zoom_limit, n)
    count = np.arange(n)
    amplitude_abs = np.sqrt(real**2 + imag**2)

    plt.figure(figsize=(12, 5))
    plt.plot(count[:m], real[:m], color="blue", label="I (Real)", linewidth=0.8)
    plt.plot(count[:m], imag[:m], color="red", label="Q (Imag)", linewidth=0.8)
    plt.plot(count[:m], amplitude_abs[:m], color="green", label="|I+jQ| (abs)", linewidth=0.8)
    plt.title(f"Первые {m} сэмплов из {n}")
    plt.xlabel("Сэмплы")
    plt.ylabel("Амплитуда (int16)")
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.tight_layout()

    plt.figure(figsize=(12, 4))
    plt.plot(count[:m], amplitude_abs[:m], color="green", linewidth=0.8)
    plt.title(f"Огибающая (абсолютное значение) — первые {m} сэмплов из {n}")
    plt.xlabel("Сэмплы")
    plt.ylabel("|I+jQ|")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()

    plt.show()

def main():
    real, imag = load_iq_data(FILE_PATH)
    plot_signals(real, imag, ZOOM)

if __name__ == "__main__":
    main()
