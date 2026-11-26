# SDR — Работа с ADALM-PLUTO через SoapySDR

Проект для работы с программно-определяемым радио (SDR) **ADALM-PLUTO**. Включает инициализацию устройства через библиотеку **SoapySDR**, генерацию и передачу пачечного сигнала (burst mode) с временными метками, прием IQ-отсчетов в формате `CS16`, сохранение данных в бинарные файлы (`.pcm`) и их последующую визуализацию в **Python**.

---

## 1. Требования и установка зависимостей

Для сборки и работы приложения требуются:
* Компилятор C/C++ (`gcc` / `clang`) и система сборки (`cmake`, `make`);
* Библиотеки: **libiio**, **libad9361-iio**, **SoapySDR** и модуль **SoapyPlutoSDR**;
* **Python 3** с библиотеками `numpy` и `matplotlib`.

### Ubuntu / Debian:

```bash
sudo apt update
sudo apt install -y build-essential cmake git \
    libiio-dev libiio-utils \
    libad9361-dev \
    libglfw3-dev libgl1-mesa-dev \
    python3 python3-pip python3-numpy python3-matplotlib
```

### macOS (Homebrew):

```bash
brew install cmake glfw libiio python
pip3 install numpy matplotlib
```

*(Зависимости Dear ImGui и ImPlot загружаются автоматически через CMake FetchContent при сборке проекта, а SoapySDR входит в состав репозитория).*

---

## 2. Клонирование репозитория

Для корректной загрузки проекта вместе со всеми зависимыми субмодулями (`SoapySDR`, `libiio`, `libad9361-iio`, `SoapyPlutoSDR`) клонируйте репозиторий с флагом `--recursive`:

```bash
git clone --recursive https://github.com/quincyque0/SDR.git
cd SDR
```

Если репозиторий уже был склонирован без субмодулей, выполните их инициализацию:

```bash
git submodule update --init --recursive
```

---

## 3. Сборка проекта

### Быстрая сборка (одной командой):

Если встроенная библиотека SoapySDR еще не собрана, сначала собираем и экспортируем её:
```bash
# 1. Сборка встроенного SoapySDR
cmake -B SoapySDR/build -S SoapySDR
cmake --build SoapySDR/build --target SoapySDR SoapySDRUtil -j4
cmake --install SoapySDR/build --prefix SoapySDR/dist

# 2. Сборка основного проекта (main + sdr_gui)
cmake -B build -S .
cmake --build build -j4
```

После завершения компиляции в папке `build` появятся:
- `main` — C++17 CLI-утилита для бесконечной или пакетной передачи (TX) и приёма (RX) с ADALM-Pluto.
- `sdr_gui` — графический высокопроизводительный визуализатор сигналов на **Dear ImGui (Docking)** и **ImPlot** в стиле **Studio Dark**.

---

## 4. Запуск GUI визуализатора (`sdr_gui`)

Запуск интерфейса из корня репозитория:
```bash
./build/sdr_gui
```
Или с автоматической загрузкой IQ-файла:
```bash
./build/sdr_gui Rxdata.pcm
```

### Источники данных в GUI (Signal Source Mode):
В левой панели **DSP Controls** в блоке **Signal Source Mode** доступны 3 режима с мгновенным переключением:

1. **`Live SDR (RX)` (Прямой эфирный приём с Pluto SDR):**
   - Настоящий приём отсчетов в реальном времени с ADALM-Pluto через SoapySDR (не симуляция).
   - Выбор URI устройства: быстрые кнопки пресетов (`usb:1.11.5`, `usb:1.13.5`, `192.168.2.1`) или ручной ввод.
   - Настройка частоты приёма (`RX Center Frequency`, 70 – 6000 МГц), частоты дискретизации (`RX Sample Rate`, 0.521 – 20 MSPS) и усиления (`RX Gain`, 0 – 73 dB).
   - Кнопка **`START LIVE RX STREAM`** запускает аппаратный поток приёма. В верхнем баре загорается зеленый индикатор `LIVE RX (Pluto SDR)`.

2. **`File Player` (Воспроизведение сохраненных файлов):**
   - Чтение бинарных файлов отсчетов (быстрые кнопки для `Rxdata.pcm` и `Txdata.pcm` или произвольный путь).
   - Поддерживаемые форматы: `Int16 / CS16` (формат Pluto SDR), `CF32` (комплексный float), `CU8` (RTL-SDR).
   - Полноценное управление: Play / Pause, Step, Loop (зацикливание), таймлайн-скраббер (перемотка).

3. **`Simulation` (Синтетический генератор):**
   - Генерация сигналов с модуляциями BPSK, QPSK, 16-QAM, FM, OFDM.
   - Имитация радиоканала: добавление шума AWGN ($E_b/N_0$), отстройки частоты, фазового джиттера и компенсатора AFC.

### Главные панели визуализатора:
- **Top Bar:** индикатор источника, кнопки управления транспортом, KPI-бейджи (Sample Rate, Center Frequency, SNR/EVM, FPS / нагрузка DSP потока).
- **Center Main Viewport:**
  - Верхний график: временная область (I / Q) с триггером по фронту и режимом паузы при клике.
  - Нижний график: спектральная плотность мощности (FFT) с Peak-hold и водопад (Waterfall).
- **Right Sidebar:**
  - Диаграмма созвездия (Constellation) с трейлом затухания символов и расчетом EVM%.
  - Глазковая диаграмма (Eye diagram) по символьным переходам.

---

## 5. Определение URI подключенного устройства

Перед запуском убедитесь, что Pluto SDR подключен по USB или сети. Для поиска подключенных устройств выполните:

```bash
./SoapySDR/dist/bin/SoapySDRUtil --find="driver=plutosdr"
# или
iio_info -s
```

> **Примечание:** В коде `src/config.cpp` по умолчанию заданы URI (`usb:1.11.5` и `usb:1.13.5`). Если вывод показывает другой адрес (например, `usb:1.4.5` или сетевой `ip:192.168.2.1`), передайте или измените его в конфигурации. В GUI адрес можно переключать в один клик.

---

## 6. Запуск CLI утилиты (`main`)

Программа `main` принимает один обязательный аргумент:
* `0` — режим **передачи (TX)**;
* `1` — режим **приема (RX)**.

Потоки работают непрерывно до нажатия `Ctrl+C`.

### Запуск в режиме передачи (TX):

```bash
./build/main 0
```
* **Частота передачи:** 800 МГц, усиление: -40 дБ, частота дискретизации: 1 МГц.
* Генерирует непрерывные пачки сигнала и сохраняет сформированные отсчеты в файл `Txdata.pcm`.

### Запуск в режиме приема (RX):

```bash
./build/main 1
```
* **Частота приема:** 800 МГц, усиление: +30 дБ, частота дискретизации: 1 МГц.
* Считывает непрерывный входной поток отсчетов с эфира и записывает в бинарный файл `Rxdata.pcm`.

---

## 7. Альтернативная визуализация (Python)

Для визуализации принятого сигнала запустите скрипт `read.py`. Скрипт считывает отсчеты из `build/Rxdata.pcm`, разделяет синфазную ($I$) и квадратурную ($Q$) составляющие, вычисляет огибающую $|I + jQ|$ и строит графики:

```bash
# Из папки build:
python3 ../read.py

# Либо из корня репозитория:
python3 read.py
```

---

## 7. Структура проекта

```text
SDR/
SDR/
├── CMakeLists.txt          # C++17, SoapySDR
├── main.cpp                # точка входа
├── include/
│   ├── config.hpp          # AppConfig, constexpr константы
│   ├── sdr_device.hpp      # DevicePtr (unique_ptr), create/configure
│   ├── signal_gen.hpp      # ComplexSample, BPSK/QPSK, upsample, convolve
│   └── sdr_stream.hpp      # run_sdr_streams()
├── src/
│   ├── config.cpp
│   ├── sdr_device.cpp      # SoapySDR::Device C++ API
│   ├── signal_gen.cpp      # std::vector, std::complex<int16_t>
│   └── sdr_stream.cpp      # C++ streams, ofstream
├── read.py                 # Python визуализация (без изменений)
├── visualization.html      # JS визуализация (без изменений)
└── reports/                # Отчёты 1-5
```
