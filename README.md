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

## Сборка и запуск

Из корня репозитория:

```sh
cmake -S src -B build -G "Unix Makefiles" \
  -DSoapySDR_DIR="$HOME/.local/telecom-sdr/share/cmake/SoapySDR"
make -C build pluto_sdr
```

В двух терминалах из каталога `build` сначала запустите приёмник, затем передатчик:

```sh
./pluto_sdr rx <URI_приёмника>
./pluto_sdr tx <URI_передатчика>
```

Укажите разные URI без угловых скобок. Без URI используется `usb:`. Настройки частот, усиления и потоков задаются в [`include/config.hpp`](include/config.hpp). Остановка — `Ctrl+C`. Приёмник сохраняет `rx_samples.pcm` в текущий рабочий каталог.

Передатчик повторяет пакет из преамбулы и сообщения `Work - DOBROMILOV`. На символ приходится четыре отсчёта. Прямоугольные импульсы формируются свёрткой, преобразуются в CS16 и передаются с временной меткой через `writeStream`. Размер пакета должен помещаться в один TX-буфер драйвера.

Если эталонных CSV нет на машине RX, скопируйте `tx_preamble.csv` и `tx_symbols.csv` с передатчика или сгенерируйте их из той же версии кода и с теми же настройками, с которыми была сделана запись. Из корня проекта выполните `(cd build && ./pluto_sdr export)`. Режим `export` сохраняет CSV в текущий каталог и завершает программу до открытия SDR.

После остановки RX восстановите биты и постройте созвездие из корня репозитория:

```sh
python3 py_script/decode_bpsk.py build/rx_samples.pcm
python3 py_script/constellation.py build/rx_symbols.csv
```

Декодер ищет наиболее уверенное совпадение с преамбулой, оценивает частотное смещение и фазу. Он печатает текст, число несовпадений с исходными битами и сохраняет `rx_bits.txt` и `rx_symbols.csv` рядом с записью. В CSV попадает один комплексный отсчёт на символ до выбора бита. Если TX и RX работали в разных каталогах, передайте декодеру `--tx-dir` с каталогом файлов `tx_preamble.csv` и `tx_symbols.csv`. При изменении параметров в `config.hpp` укажите те же значения через `--sample-rate` и `--samples-per-symbol`.

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

