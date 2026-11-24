<div style="text-align: center; margin-top: 150px; margin-bottom: 250px;">
  <p style="font-size: 14px; text-transform: uppercase; letter-spacing: 2px;">Федеральное государственное образовательное бюджетное
учреждение высшего образования<br>
«Сибирский государственный университет телекоммуникаций и
информатики» (ФГОБУ ВО «СибГУТИ»)</p>
  <h1 style="font-size: 36px; margin-top: 40px;">Отчет SDR №5</h1>
  <p style="font-size: 18px; color: gray;">Формирование сигналов цифровой модуляции: генерация бит, BPSK/QPSK маппинг, апсэмплинг и свертка с формирующим фильтром.</p>
</div>

<div style="text-align: right; margin-right: 50px;">
  <p><b>Выполнил:</b> Студент / Рудометов Д. Р. ИКС-433</p>
  <p><b>Проверил:</b> Преподаватель / Калачиков Александр Александрович</p>
</div>

<div style="text-align: center; margin-top: 150px;">
  <p>Новосибирск — 2026</p>
</div>

<div style="page-break-after: always;"></div>

---

### Цель работы

* Изучение и программная реализация базового тракта цифрового модулятора на языке Си в составе программно-определяемой радиосистемы (SDR).
* Реализация генератора случайной битовой последовательности.
* Освоение принципов квадратурного маппинга бит в комплексные созвездия BPSK и QPSK.
* Реализация операции повышения частоты дискретизации (upsampling) путем вставки нулевых отсчетов между символами.
* Реализация операции цифровой свертки (дискретной фильтрации) с прямоугольным формирующим импульсом (Rectangular Pulse Shaping) для получения готовых отсчетов сигналов в буфере передачи.

---

### Теоретические основы и этапы обработки

Цифровой передатчик формирует радиосигнал во временной области в несколько последовательных этапов:

1. **Генерация бит (Информационный поток):**
   Источник данных формирует случайную последовательность бит $b_k \in \{0, 1\}$.

2. **Модуляция (Маппинг в комплексную плоскость):**
   * **BPSK (Binary Phase Shift Keying):** Каждый бит отображается в один вещественный отсчет:
     $$s_k = \begin{cases} +A, & \text{если } b_k = 1 \\ -A, & \text{если } b_k = 0 \end{cases}, \quad Q_k = 0$$
   * **QPSK (Quadrature Phase Shift Keying):** Поток разбивается на пары бит $(b_{2k}, b_{2k+1})$, каждая пара формирует комплексный символ:
     $$I_k = \begin{cases} +A, & b_{2k} = 1 \\ -A, & b_{2k} = 0 \end{cases}, \quad Q_k = \begin{cases} +A, & b_{2k+1} = 1 \\ -A, & b_{2k+1} = 0 \end{cases}$$

3. **Апсэмплинг (Upsampling / Добавление нулей):**
   Для согласования символьной скорости со скоростью дискретизации ЦАП SDR-устройства частота повышается в $L = \text{SPS}$ (Samples Per Symbol) раз. Между исходными символами вставляется $L - 1$ нулей:
   $$x[m] = \begin{cases} s_{m / L}, & m = 0, L, 2L, \dots \\ 0, & \text{иначе} \end{cases}$$

4. **Формирующий фильтр (Свертка):**
   Чтобы импульсы приобрели прямоугольную форму требуемой длительности $L$, сигнал $x[m]$ сворачивается с импульсной характеристикой прямоугольного фильтра $h[k] = 1, \; k = 0, \dots, L-1$:
   $$y[n] = (x * h)[n] = \sum_{k=0}^{L-1} x[n - k] \cdot h[k] = \sum_{k=0}^{L-1} x[n - k]$$
   Поскольку среди отсчетов $x[n-k]$ на интервале длины $L$ присутствует ровно один ненулевой исходный символ, результатом свертки становится прямоугольный импульс постоянного уровня на протяжении всех $L$ отсчетов символьного интервала.

---

### Программная реализация модулей

Вся цепочка цифровой обработки реализована в модуле `signal_gen` (`include/signal_gen.h` и `src/signal_gen.c`):

#### 1. Генератор бит
```c
void generate_random_bits(uint8_t *bits, size_t num_bits)
{
    for (size_t i = 0; i < num_bits; ++i) {
        bits[i] = (uint8_t)(rand() & 1);
    }
}
```

#### 2. Модуляция (BPSK и QPSK)
```c
void modulate_bpsk(const uint8_t *bits, size_t num_bits, ComplexSample16 *symbols, int16_t amplitude)
{
    for (size_t i = 0; i < num_bits; ++i) {
        symbols[i].i = bits[i] ? amplitude : (int16_t)(-amplitude);
        symbols[i].q = 0;
    }
}

void modulate_qpsk(const uint8_t *bits, size_t num_bits, ComplexSample16 *symbols, int16_t amplitude)
{
    size_t num_symbols = num_bits / 2;
    for (size_t i = 0; i < num_symbols; ++i) {
        uint8_t bit_i = bits[2 * i];
        uint8_t bit_q = bits[2 * i + 1];

        symbols[i].i = bit_i ? amplitude : (int16_t)(-amplitude);
        symbols[i].q = bit_q ? amplitude : (int16_t)(-amplitude);
    }
}
```

#### 3. Апсэмплинг (вставка нулей)
```c
void upsample_symbols(const ComplexSample16 *symbols, size_t num_symbols, size_t sps, ComplexSample16 *upsampled)
{
    size_t total_samples = num_symbols * sps;
    memset(upsampled, 0, total_samples * sizeof(ComplexSample16));

    for (size_t i = 0; i < num_symbols; ++i) {
        upsampled[i * sps] = symbols[i];
    }
}
```

#### 4. Дискретная свертка (формирование прямоугольных импульсов)
```c
void convolve_rectangular(const ComplexSample16 *upsampled, size_t num_samples, size_t sps, ComplexSample16 *shaped)
{
    for (size_t n = 0; n < num_samples; ++n) {
        int32_t acc_i = 0;
        int32_t acc_q = 0;

        for (size_t k = 0; k < sps; ++k) {
            if (n >= k) {
                acc_i += upsampled[n - k].i;
                acc_q += upsampled[n - k].q;
            }
        }

        shaped[n].i = (int16_t)acc_i;
        shaped[n].q = (int16_t)acc_q;
    }
}
```

---

### Интеграция в тракт передачи PlutoSDR

Функция `signal_gen_fill_tx_buffer` объединяет все 4 этапа и формирует отсчеты для буфера передачи `tx_buffer`:
1. В паузе пачечного цикла (`!is_burst`) буфер заполняется нулями (`memset`).
2. В активной пачке генерируются биты, модулируются в комплексные отсчеты QPSK с амплитудой $A = 2000$, дополняются нулями с коэффициентом $\text{SPS} = 16$ и сворачиваются с прямоугольным фильтром.
3. Полученные синфазная и квадратурная компоненты последовательно заносятся в буфер формата `CS16` (`tx_buffer[2*n] = I`, `tx_buffer[2*n + 1] = Q`) для последующей передачи через SoapySDR.

---

### Вывод

В ходе лабораторной работы был спроектирован и реализован полный тракт цифрового модулятора сигналов:
1. Создан генератор случайного двоичного потока.
2. Реализованы квадратурные модуляторы BPSK и QPSK с формированием комплексных отсчетов.
3. Освоен механизм апсэмплинга (вставки нулей между символами) для изменения частоты дискретизации.
4. Реализован алгоритм дискретной свертки с прямоугольной импульсной характеристикой, что обеспечило гладкое формирование прямоугольных импульсов заданной длительности ($\text{SPS} = 16$) в буфере передачи SDR.
