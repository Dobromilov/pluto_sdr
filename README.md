# ADALM-Pluto SDR

Добромилов Артём

## Структура проекта

```text
docs/
  images/           Иллюстрации к отчётам
  *.md              Отчёты
include/
  config.hpp        Настройки устройства и потоков
src/
  CMakeLists.txt
  main.cpp          Выбор режима tx/rx и URI устройства
  sdr_runner.cpp    Работа с SDR
py_script/
  oscillogram.py    Построение графиков I/Q и модуля
thirdparty/         Подмодули SoapySDR, SoapyPlutoSDR, libad9361-iio, libiio
```

## Сборка и запуск

Из корня репозитория:

```sh
cmake -S src -B build \
  -DSoapySDR_DIR="$HOME/.local/telecom-sdr/share/cmake/SoapySDR"
cmake --build build --target pluto_sdr
```

В двух терминалах из каталога `build` сначала запустите приёмник, затем передатчик:

```sh
./pluto_sdr rx <URI_приёмника>
./pluto_sdr tx <URI_передатчика>
```

Укажите разные URI без угловых скобок. Без URI используется `usb:`. Настройки частот, усиления и потоков задаются в [`include/config.hpp`](include/config.hpp). Остановка — `Ctrl+C`. Приёмник сохраняет `rx_samples.pcm` в текущий рабочий каталог.

## Отрисовка графиков

Скрипту нужны Python 3, NumPy и Matplotlib. Запуск из корня репозитория:

```sh
python3 py_script/oscillogram.py build/rx_samples.pcm
```

## Отчёты

- [FM-радиоприёмник в GNU Radio](docs/radio_sdr_rx.md)
- [Приём Wi-Fi сигнала](docs/rx_wifi.md)
- [Приём и передача с аппаратными временными метками](docs/pluto_sdr_timestamp.md)
- [Передача I/Q между двумя SDR](docs/pluto_tx_rx_link.md)

