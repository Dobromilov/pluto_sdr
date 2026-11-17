# ADALM-Pluto SDR

Добромилов Артём

## Структура проекта

```text
docs/
  images/           Иллюстрации к отчётам
  *.md              Отчёты
include/
  config.hpp        Настройки устройства и потоков
  convolution.hpp   Объявление функции свёртки
src/
  CMakeLists.txt
  main.cpp          Выбор режима tx/rx и URI устройства
  sdr_runner.cpp    Работа с SDR
  convolution.cpp  Формирование импульсов с помощью свёртки
py_script/
  oscillogram.py    Построение графиков I/Q и модуля
  constellation.py  Отрисовка созвездия из CSV
  decode_bpsk.py    Поиск пакета и восстановление символов из записи RX
thirdparty/         Подмодули SoapySDR, SoapyPlutoSDR, libad9361-iio, libiio
```

## Сборка на Ubuntu и Debian

### Инструменты и подмодули

```bash
sudo apt update
sudo apt install -y git build-essential cmake pkg-config \
  libusb-1.0-0-dev libxml2-dev python3-numpy python3-matplotlib

git clone --recurse-submodules https://github.com/Dobromilov/pluto_sdr.git
cd pluto_sdr
git submodule status --recursive
```

Если репозиторий уже клонирован, выполните в его каталоге:

```bash
git pull --ff-only
git submodule sync --recursive
git submodule update --init --recursive
git submodule status --recursive
```

### Библиотеки и приложение

Выполните блок из корня репозитория в Bash. Подмодули собираются в порядке зависимостей и устанавливаются в `~/.local/telecom-sdr`. Переменная `CMAKE_POLICY_VERSION_MINIMUM` позволяет CMake 4 настроить подмодули со старыми требованиями к CMake.

```bash
SDR_PREFIX="$HOME/.local/telecom-sdr"
export PKG_CONFIG_PATH="$SDR_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CMAKE_POLICY_VERSION_MINIMUM=3.5

build_dep() {
  local name="$1"
  shift
  cmake -S "thirdparty/$name" -B "build/deps/$name" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$SDR_PREFIX" \
    -DCMAKE_PREFIX_PATH="$SDR_PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_RPATH="$SDR_PREFIX/lib" \
    "$@" &&
  cmake --build "build/deps/$name" --parallel &&
  cmake --install "build/deps/$name"
}

build_dep libiio -DWITH_IIOD=OFF -DWITH_LOCAL_BACKEND=OFF \
  -DHAVE_DNS_SD=OFF -DUDEV_RULES_INSTALL_DIR="$SDR_PREFIX/share/udev/rules.d" &&
build_dep libad9361-iio &&
build_dep SoapySDR -DENABLE_PYTHON=OFF -DENABLE_PYTHON3=OFF &&
build_dep SoapyPlutoSDR &&
cmake -S src -B build -DCMAKE_PREFIX_PATH="$SDR_PREFIX" &&
cmake --build build --target pluto_sdr --parallel
```

### Проверка USB

Подключите Pluto с прошивкой, поддерживающей потоковый USB-интерфейс и аппаратные временные метки. Проверьте, что установленный драйвер находит устройства, и запишите их `uri`:

```bash
"$HOME/.local/telecom-sdr/bin/SoapySDRUtil" --find="driver=plutosdr"
```

Если Linux сообщает об отказе в доступе к USB, установите правило `udev` из собранного `libiio` и переподключите Pluto:

```bash
sudo install -m 644 "$HOME/.local/telecom-sdr/share/udev/rules.d/90-libiio.rules" \
  /etc/udev/rules.d/90-libiio.rules
sudo udevadm control --reload-rules
"$HOME/.local/telecom-sdr/bin/SoapySDRUtil" --find="driver=plutosdr"
```

## Запуск и декодирование

Откройте два терминала в корне проекта. Подставьте разные URI из `--find` вместо примеров. Сначала запустите RX:

```bash
cd build
./pluto_sdr rx "usb:3.19.5"
```

Во втором терминале запустите TX с нужной модуляцией:

```bash
cd build
./pluto_sdr tx "usb:3.18.5" --qpsk  # Для BPSK укажите --bpsk
```

Настройки частот, усиления и потоков задаются в [`include/config.hpp`](include/config.hpp). Через несколько секунд остановите оба процесса через `Ctrl+C`. Приёмник сохраняет `rx_samples.pcm` в текущий рабочий каталог.

Передатчик повторяет пакет из преамбулы и сообщения `Work - DOBROMILOV`. На символ приходится четыре отсчёта. Прямоугольные импульсы формируются свёрткой, преобразуются в CS16 и передаются с временной меткой через `writeStream`. Размер пакета должен помещаться в один TX-буфер драйвера.

Если эталонных CSV нет на машине RX, скопируйте `tx_preamble.csv` и `tx_symbols.csv` с передатчика или сгенерируйте их из той же версии кода и с теми же настройками модуляции. Для QPSK выполните из корня проекта `(cd build && ./pluto_sdr export --qpsk)`, для BPSK укажите `--bpsk`. Режим `export` сохраняет CSV в текущий каталог и завершает программу до открытия SDR.

Из корня репозитория восстановите биты и постройте созвездие. Передайте декодеру тот же флаг, что и передатчику:

```sh
python3 py_script/decode_bpsk.py build/rx_samples.pcm --qpsk &&
python3 py_script/constellation.py build/rx_symbols.csv
```

Декодер ищет наиболее уверенное совпадение с преамбулой, оценивает частотное смещение и фазу. Он печатает текст и число несовпадений с исходными битами, а также сохраняет `rx_bits.txt` и `rx_symbols.csv` рядом с записью. В CSV попадает один комплексный отсчёт на символ до выбора бита. Если TX и RX работали в разных каталогах, передайте декодеру `--tx-dir` с каталогом файлов `tx_preamble.csv` и `tx_symbols.csv`. При изменении параметров в `config.hpp` укажите те же значения через `--sample-rate` и `--samples-per-symbol`.

В контрольном пакете ожидается текст `Work - DOBROMILOV` и `Ошибки: 0/136 бит`. Для BPSK ожидаются 136 символов и два облака на созвездии, для QPSK — 68 символов и четыре облака около `(±1/√2, ±1/√2)`. В QPSK первый бит пары задаёт знак `I`, второй — знак `Q`. Преамбула передаётся в BPSK в обоих режимах. Без флага передатчик и декодер выбирают BPSK.

## Отрисовка графиков

Скрипту нужны Python 3, NumPy и Matplotlib. Запуск из корня репозитория:

```sh
python3 py_script/oscillogram.py build/rx_samples.pcm
```

## Созвездие BPSK

При запуске TX программа сохраняет сформированные символы в `tx_symbols.csv`, результат upsampling — в `tx_upsampled.csv`, а сигнал после свёртки — в `tx_shaped.csv` в текущем рабочем каталоге. При запуске из `build` команды отрисовки из корня репозитория:

```sh
python3 py_script/constellation.py build/tx_symbols.csv
python3 py_script/constellation.py build/tx_upsampled.csv
python3 py_script/constellation.py build/tx_shaped.csv
```

Без аргумента скрипт читает `build/tx_symbols.csv`. Нужен Matplotlib. PNG сохраняется рядом с CSV; параметр `--no-show` отключает открытие окна графика.

У символов BPSK ожидаются две точки: `(-1, 0)` и `(1, 0)`. В результате upsampling также присутствует точка `(0, 0)` от вставленных нулей. Свёртка с фильтром `[1, 1, 1, 1]` формирует прямоугольные импульсы по четыре отсчёта на символ. Полная свёртка добавляет три нулевых отсчёта в конце массива. Эти CSV показывают передаваемый сигнал на разных этапах формирования, а `rx_symbols.csv` содержит восстановленные приёмником символы.

## Отчёты

- [FM-радиоприёмник в GNU Radio](docs/radio_sdr_rx.md)
- [Приём Wi-Fi сигнала](docs/rx_wifi.md)
- [Приём и передача с аппаратными временными метками](docs/pluto_sdr_timestamp.md)
- [Передача I/Q между двумя SDR](docs/pluto_tx_rx_link.md)
- [Передача и декодирование BPSK](docs/pluto_bpsk.md)

