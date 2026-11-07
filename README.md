# Говорящие часы на Raspberry Pi Pico 2 W

Основной проект в main - прошивка на C / Pico SDK для **Pico 2 W (RP2350)**. Исходники MicroPython и первоначальная схема Pico сохранены как история.

Часы используют LCD1602 I2C, RTC DS1302, DFPlayer Mini, два энкодера и пять дополнительных кнопок. Pico читает MP3 с отдельной microSD по SPI1, декодирует их через Helix и передаёт SBC-аудио на колонку Bluetooth Classic A2DP.

## Возможности и состояние

- Таймер, пять ежедневных будильников, русская и немецкая речь, тихие часы.
- Меню времени, даты, языка, громкости и аудиовыходов; диагностика по USB.
- Локальный DFPlayer и Bluetooth для речи, таймера, будильников и музыки с локальным резервным воспроизведением.
- Один энкодер управляет активным аудиовыходом. Громкость DFPlayer и Bluetooth хранится раздельно.
- Сканирование Bluetooth, выбор имени/MAC, автоматическое SSP-сопряжение, до четырёх известных устройств, прямое подключение к сохранённому MAC.
- Настройки Flash v7 читают форматы v1-v6. Ключи сопряжения хранятся отдельно средствами BTstack.
- SPI microSD: чтение FAT32 и MP3; запись не реализована.

C-прошивка является текущей разработкой. Перенос таймерного энкодера, кнопка громкости, SPI SD/FAT32, чтение MP3 и воспроизведение через Bluetooth подтверждены аппаратными тестами - см. [сводку с источниками](doc/HARDWARE_TEST_EVIDENCE.md). Поиск, сохранение выбранной JBL Link Portable, переподключение после отключения питания и удаление через bt-forget с autosave также проверены на плате с новым реестром v7. Google Nest Audio ранее проверена для Bluetooth-воспроизведения; переключение двух записей в новом реестре ещё требует проверки. GP6 зарезервирован для внешней подсветки, драйвер PWM не реализован.

## Актуальное подключение

См. [распиновку Pico 2 W](doc/HARDWARE_PICO2W.md). Она имеет приоритет над старым изображением схемы и MicroPython-кодом.

Таймерный энкодер перенесён с DT=GP11 / CLK=GP10 на **GP12 / GP13**. GP8-GP11 используются SD SPI1. Кнопки энкодеров в текущем коде: громкость GP20, таймер GP19. Карты Pico и DFPlayer независимы.

## Обновление Windows

В вашей рабочей папке репозитория закройте монитор последовательного порта. При первом переходе с develop/v3:

```powershell
git status --short
git fetch origin
git switch main
git merge --ff-only origin/main
```

Переключайте ветку при чистой рабочей копии, предварительно сохранив свои изменения. Если локальной main нет: `git switch --track origin/main`.

Последующие обновления:

```powershell
git pull --ff-only
powershell -ExecutionPolicy Bypass -File .\update-pico.ps1 -Build -Bluetooth -Mp3
```

По умолчанию используется COM8; другой порт задаётся через `-Port COM7`. Программный переход в BOOTSEL требует уже установленной прошивки с командами bootsel arm/confirm; первая установка выполняется с BOOTSEL вручную. Скрипт после прошивки проверяет Flash и ответы будильников, но не качество звука и сопряжение.

## Зависимости и сборка

Скрипт ожидает Pico SDK 2.3.1, pico-examples, CMake 4.3.4, ARM GCC, Ninja, pioasm и picotool в `%USERPROFILE%\.pico-sdk`. Для MP3:

```powershell
git clone https://github.com/Lefucjusz/Helix-MP3-Decoder.git third_party/Helix-MP3-Decoder
git -C third_party/Helix-MP3-Decoder checkout e4f5adadb5126708144e55a862f59e318cbffb51
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3
```

Сборка не прошивает плату. Результат: `firmware/pico2w_c/build-bt/speaking_clock_c.uf2`. На Windows используйте короткий путь без кириллицы, например вашу папку `C:\Users\Flint123\projects\Speaking-Timer-Clock-Raspberry-Pi-Pico`.

## Bluetooth

1. Переведите колонку в режим обнаружения, запустите Scan devices в SETTINGS > Bluetooth или `bt-scan`.
2. После окончания поиска: `bt-devices`, затем `bt-use N`, `bt-use Portable` или `bt-use AA:BB:CC:DD:EE:FF`.
3. Дождитесь `[BT-SELECT] stream ACTIVE`, выйдите из меню, дождитесь `[FLASH] autosave=OK`.
4. После перезапуска выполняется прямое подключение к сохранённому MAC без сканирования. Интервалы повторных попыток: 5, 10, затем 30 секунд.
5. `bt-known` показывает сохранённые имена и наличие ключа. `bt-forget N` удаляет устройство и его ключ.

В меню и нумерации сначала идут известные устройства, затем результаты поиска. Одинаковые имена разных устройств требуют номера или MAC. При заполнении четырёх записей заменяется самая ранняя добавленная запись.

LCD показывает имя колонки. Если имя ещё неизвестно, используются Saved speaker или Unknown device; MAC остаётся в USB-логах для диагностики и используется внутри для идентификации и подключения.

Поддерживается Classic A2DP Sink/SBC. BLE-only, HFP и Google Cast этим путём не поддерживаются. SSP подтверждается автоматически; legacy PIN не поддерживается. Если колонка забыла сопряжение, удалите запись и подключите её заново.

## Структура

| Путь | Назначение |
| --- | --- |
| [firmware/pico2w_c](firmware/pico2w_c/) | Текущая C-прошивка и диагностика |
| [doc/HARDWARE_PICO2W.md](doc/HARDWARE_PICO2W.md) | Актуальные подключения |
| [doc/history](doc/history/) | Прежние README и история разработки |
| [source/v3](source/v3/) | Историческая MicroPython-версия |
| [experiments/bluetooth_a2dp_nest](experiments/bluetooth_a2dp_nest/) | Генератор интеграции BTstack A2DP |
| [schematic](schematic/) | Первоначальная схема Pico |
| [sound](sound/) | Звуковые материалы |

## Лицензии

Проект: [GPL-3.0](LICENSE). Внешний Helix не включён в репозиторий; оболочка и ядро сохраняют условия GPL/RPSL/RCSL. При распространении прошивки учитывайте также лицензии Pico SDK и BTstack.
