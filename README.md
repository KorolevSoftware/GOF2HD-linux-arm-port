# GOF2HD Linux Port — сборка и запуск

Порт **Galaxy on Fire 2 HD** (Android ARMv7) на Linux-устройства с ARM-процессором:
нативный рендер через аппаратный GPU (Mali), без QEMU во время игры.
Сборка выполняется в Docker на ПК; на консоль переносится готовый результат.

- Актуальная архитектура и границы ответственности — в [ARCHITECTURE.md](ARCHITECTURE.md)
- Технический отчёт «как это устроено внутри» и история ABI/FMOD-диагностики — в [GOF2HD_PORT_NOTES.md](GOF2HD_PORT_NOTES.md)

## Требования

Для сборки на ПК:

- Git и запущенный Docker;
- поддержка контейнеров `linux/arm/v7`: на x86-64 и ARM64 нужна ARMv7-эмуляция
  QEMU/binfmt; Docker Desktop включает её по умолчанию, для Docker Engine
  на Linux её нужно настроить при отсутствии поддержки ([документация Docker](https://docs.docker.com/build/building/multi-platform/#qemu));
- файлы оригинальной игры для сборки не нужны;
- доступ к репозиториям образов и пакетов Debian при первом создании образа.

Для запуска на консоли:

- ARM Linux с поддержкой 32-битных ARMv7 hardfp-программ;
- совместимая **glibc**, 32-битные **SDL2** и **EGL/GLES2** из прошивки;
  проверенные варианты — Mali/fbdev и ROCKNIX с Mesa/Panfrost/Wayland;
- пользовательские APK/OBB в `original/`, подготовленные библиотеки игры в
  `gamedata/lib/`, ресурсы и сохранения в `data/`, файлы порта в `port/run-native/`.
  Подробная установка — в [port/PORTMASTER.md](port/PORTMASTER.md).

Docker, компиляторы и заголовки SDL2 на консоли для запуска не требуются.

> Движок собран с **softfp ABI** (armeabi-v7a, aapcs). Порт собирает всё в hardfp
> и оборачивает мягкие float-вызовы движка через ABI-мост в `shim/` и
> `gles-stub/gles-bridge.c`. Движок использует только GLES2 (контекст он не
> создаёт) — EGL для него не нужен вовсе. Патч `e_flags` в `libgof2hdaa.so`
> делает бинарь совместимым с линкером — выполняется отдельной командой
`prepare-game.py` над копиями оригинальных библиотек.

## Ponytail для работы над проектом

Ponytail установлен локально: `AGENTS.md` включает режим `full` для задач
разработки, а `.agents/skills/` содержит шесть навыков. Они доступны со
следующего сообщения в этом проекте; если не появились в списке — перезапустите
Codex. Явный вызов: `$ponytail`, `$ponytail-review`, `$ponytail-audit`,
`$ponytail-debt`, `$ponytail-gain`, `$ponytail-help`.

Установка состоит из проектных правил и навыков. Глобальные плагины, хуки
и настройки пользователя не подключаются. Переключение `lite/full/ultra/off`
действует в текущем чате; исходный help также описывает возможности полной
плагинной установки, которые здесь не включены. Версия и лицензия —
в `.agents/PONYTAIL.md` и `.agents/PONYTAIL-LICENSE`.

## 1. Сборка на ПК через Docker

Все команды выполняются из корня проекта на ПК:

```sh
bash port/tools/build-docker.sh
```

Скрипт создаёт или обновляет локальный образ `gof2hd-build:armhf`, подключает
проект в `/work` и запускает `build-native.sh` внутри контейнера.
`Dockerfile` использует Debian Bookworm для **`linux/arm/v7`** с GCC/G++,
CMake, Make, SDL2 dev и Python3. Базовый образ закреплён по SHA256;
пакеты устанавливаются из репозиториев Debian при создании образа.

Контейнер остаётся ARMv7 и на x86-64/ARM64 ПК: сборка выполняется под
эмуляцией и создаёт файлы для ARM-консоли. Если возникает `exec format error`,
проверьте поддержку ARMv7/binfmt в Docker.

### Результат и повторная сборка

Результат сохраняется на ПК в `port/run-native/`:

- `gof2hd` — Linux-хост с SDL2;
- shim-библиотеки `libc/liblog/libandroid/libm/libdl`;
- GLES2-мост `libGLESv2.so` и аудио/ABI-хелперы.

Движок и FMOD из APK не входят в результат сборки. Старые копии, оставшиеся
в `run-native/` от прежних сборок, автоматически не удаляются; скрипт упаковки
включает только явно перечисленные файлы самого порта.

Корневой `CMakeLists.txt` задаёт окружение сборки и подключает `port/`.
Общие правила порта находятся в `port/CMakeLists.txt`, а цели компонентов —
в `CMakeLists.txt` внутри `port/shim/`, `port/gles-stub/`,
`port/fmodex-stub/` и `port/host/`.

Объекты и `compile_commands.json` находятся в
`port/run-native/build/`. Повторите ту же команду для следующей сборки:
CMake перекомпилирует только изменённые исходники и их зависимости.
Оригинальная игра при сборке не открывается; ресурсы, сохранения и логи
не затрагиваются.

По умолчанию используются две параллельные задачи. Чтобы изменить их число:

```sh
GOF_JOBS=6 bash port/tools/build-docker.sh
```

Скрипт также передаёт `CFLAGS` в контейнер. `GOF_DOCKER_IMAGE` меняет имя
локального образа. Для полной пересборки удалите только каталог
`port/run-native/build/` и снова запустите `build-docker.sh`.

### Проверка сборки

После создания образа можно запустить проверку в том же контейнере:

```sh
docker run --rm --platform linux/arm/v7 \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,source=$(pwd),target=/work" \
  -e GOF_JOBS=6 gof2hd-build:armhf python3 port/tools/test-cmake-build.py
```

Проверка собирает проект, проверяет ELF/ABI файлов порта, отсутствие тестовых хуков и C++-флаги,
затем повторяет сборку и проверяет отсутствие перекомпиляции и сохранность
посторонних файлов в каталоге результата. Запуск игры на консоли проверяется отдельно.

### Образ и ручной запуск сборки

Те же действия, что выполняет `build-docker.sh`, вручную:

```sh
docker build --platform linux/arm/v7 -t gof2hd-build:armhf .
docker run --rm --platform linux/arm/v7 \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,source=$(pwd),target=/work" \
  -e GOF_JOBS=6 gof2hd-build:armhf
```

Образ содержит инструменты. Исходники доступны через подключённый каталог;
оригинальная игра сборке не нужна. `.dockerignore` исключает исходники и
игровые файлы из контекста создания образа. Для переноса уже созданного образа на другой ПК:

```sh
docker save gof2hd-build:armhf | gzip > /tmp/gof2hd-build-armhf.tar.gz
# На другом ПК:
docker load < /tmp/gof2hd-build-armhf.tar.gz
```

## 2. Пакет порта и оригинальная игра

После сборки создайте пакет только из файлов порта:

```sh
python3 port/tools/package-portmaster.py
```

Результат — `dist/gof2hd.zip`: лаунчер, Linux-хост, библиотеки совместимости,
инструменты подготовки и инструкция. APK, OBB, движок и FMOD, распакованные
ресурсы, сохранения и `build/` в него не попадают. Для нестандартного каталога
сборки используйте `--build-dir /путь/к/результату`.

Распакуйте ZIP в каталог Ports консоли, затем положите свои `base.apk` и
`main.*.obb` в `gof2hd/original/`. Например, на ROCKNIX:

```sh
DEVICE=root@192.168.0.128
scp dist/gof2hd.zip "$DEVICE:/storage/roms/ports/"
ssh "$DEVICE" 'unzip -o /storage/roms/ports/gof2hd.zip -d /storage/roms/ports'
# Эти два файла берутся из вашей копии Android-игры:
scp /путь/base.apk /путь/main.*.obb "$DEVICE:/storage/roms/ports/gof2hd/original/"
ssh "$DEVICE" 'python3 /storage/roms/ports/gof2hd/port/tools/prepare-game.py /storage/roms/ports/gof2hd'
```

Нужен Python 3.9+. Подготовку можно выполнить и на ПК в уже распакованной
папке игры, затем перенести её на консоль. Сборка порта не требует Python на
консоли; Python нужен только для отдельной подготовки оригинальной игры.

`prepare-game.py` читает оригиналы из `original/`, извлекает и патчит копии
трёх библиотек в `gamedata/lib/`, распаковывает OBB в `data/`. Существующие
файлы в `data/` не заменяются, оригиналы не меняются. Перед обновлением
остановите игру. Порядок установки, состав APK, версия OBB и переход со старой
структуры описаны в [port/PORTMASTER.md](port/PORTMASTER.md); эта инструкция
также находится в пакете как `gof2hd/README.md`.

Проверка разделения файлов и подготовки:

```sh
python3 port/tools/test-game-files.py
```

Для дополнительной проверки патчей на реальном APK задайте `GOF_TEST_APK`;
она требует уже собранных `libc.so`/`libm.so` в `port/run-native/` или каталоге
`GOF_OUT`. Базовые тесты не требуют оригинальной игры.

## 3. Запуск

### Через PortMaster

Распакуйте `dist/gof2hd.zip` в каталог `ports/`, добавьте оригиналы и выполните
подготовку по инструкции выше. После подготовки структура будет такой:

```text
ports/
├── Galaxy on Fire 2 HD.sh
└── gof2hd/
    ├── original/               # пользовательские base.apk и main.*.obb
    ├── gamedata/lib/           # патченные копии движка и FMOD
    ├── data/                  # распакованные ресурсы и сохранения
    └── port/run-native/       # только хост и библиотеки порта
```

Выберите `Galaxy on Fire 2 HD` в меню Ports. Лаунчер получает раскладку SDL
из PortMaster, подключает стандартную комбинацию выхода, ждёт завершения
игры и вызывает `pm_finish`. Для ROCKNIX используется Wayland и по умолчанию
PulseAudio. Лог — `gof2hd/log.txt`. Для нестандартного расположения доступны
`GOF_ROOT`, `GOF_OBB` и `GOF_LOG`.

Проверка лаунчера с заглушками PortMaster, без ARM-устройства:

```sh
python3 port/tools/test-portmaster-launcher.py
```

### Одна команда (рекомендуется)

Скрипт `port/tools/start-game.sh` делает всё сам: закрывает лаунчер консоли
(он держит fb0/GPU), убивает старую копию игры и запускает новую в фоне
с логом:

```bash
bash /root/gof2hd/port/tools/start-game.sh
```

Скрипт ставит окружение: `SDL_AUDIODRIVER=alsa`,
`LD_LIBRARY_PATH` с каталогами `gamedata/lib`, `port/run-native` и `/usr/lib32`,
`GOF_SHOW_CURSOR=1` и необходимые
FMOD-совместимости (POSIX filesystem, pthread, CPU и math ABI bridge).
Видеодрайвер SDL2 выбирает сам (без `SDL_VIDEODRIVER`).

Настройки через окружение (см. таблицу переменных ниже):
`GOF_ROOT`, `GOF_LOG`. Лог — `<GOF_ROOT>/run.txt`
(следить: `tail -f`).

### Запуск вручную

```bash
cd /root/gof2hd/port/run-native

export SDL_AUDIODRIVER=alsa
export GOF_SHOW_CURSOR=1
export LD_LIBRARY_PATH=/root/gof2hd/gamedata/lib:/root/gof2hd/port/run-native:/usr/lib32
export LD_PRELOAD=/root/gof2hd/port/run-native/libm.so:/root/gof2hd/port/run-native/libfmod_filesystem.so:/root/gof2hd/port/run-native/cpuinfo_fake.so:/root/gof2hd/port/run-native/pthread_bionic.so

./gof2hd /root/gof2hd/original/base.apk \
    '/root/gof2hd/original/main.47947006.net.fishlabs.gof2hdallandroid2012.obb' \
    /root/gof2hd/data/
```

Аргументы: `<base.apk> <main.*.obb> <dataDir>`.
Окно — полный экран; разрешение хост берёт в пикселях framebuffer через
`SDL_GL_GetDrawableSize` после создания GL-контекста. Эти же размеры
используют движок, DeviceInfo и курсор. Начальный размер окна берётся из
текущего видеорежима SDL, чтобы Wayland не оставлял буфер размером 1×1.

> Перед запуском закройте фронтенд/лаунчер консоли (он держит `/dev/fb0`):
> `start-game.sh` или `/etc/init.d/launcher.sh stop`, иначе fb0 конфликтует.

### ROCKNIX с Mesa/Panfrost и Wayland

Для отдельного запуска на ROCKNIX можно использовать `/storage/gof2hd`:
положите APK/OBB в `original/`, файлы порта в `port/run-native/` и выполните
`prepare-game.py /storage/gof2hd`. Сохранения остаются в `data/`.
На консоли запускайте:

```sh
bash /storage/gof2hd/port/tools/start-rocknix.sh
tail -f /storage/gof2hd/logs/latest.log
```

Скрипт использует работающий Wayland-композитор, ARMv7 Mesa из `/usr/lib32`
и PulseAudio/PipeWire. Логи каждого запуска сохраняются отдельно;
`logs/game.pid` содержит PID. `GOF_ROOT`, `GOF_OBB` и `GOF_LOG` переопределяют
пути. GLES-мост выбирает `libGLESv2.so.2`, затем `libmali.so`; переменная
`GOF_GLES_LIBRARY` явно задаёт библиотеку. Хост печатает GL vendor/renderer/version,
а при падении — регистры и адреса загруженных библиотек, включая Mesa.
`GOF_DEBUG=1 bash port/tools/start-rocknix.sh` запускает игру под GDB:
при сигнале сохраняет стеки всех потоков, регистры и карту памяти в тот же лог.
Android pthread-адаптер экспортирует символы `LIBC`, чтобы системные
Mesa/SDL с импортами `GLIBC_*` использовали собственные объекты синхронизации.

## 4. Управление — встроенный геймпад консоли (SDL2 GameController)

Игра тач-управляемая. Хост получает ввод через SDL2 GameController
(API `SDL_GameController`). При запуске через PortMaster раскладку передаёт
`get_controls`; SDL загружает её из `SDL_GAMECONTROLLERCONFIG` и
`SDL_GAMECONTROLLERCONFIG_FILE`. Хост выбирает первый доступный контроллер,
который SDL распознаёт, пропуская устройства без раскладки.
Проверено на H700 Gamepad на ROCKNIX. Встроенных раскладок в хосте нет;
новые раскладки добавляйте в базу контроллеров PortMaster.

Состояние ввода (режим курсор/гироскоп, позиция курсора, кнопки, вектор
ввода) живёт в `wrap_overlay` (`WrawState`); `gof2hd.c` — SDL-бэкенд:
применяет deadzone/нормализацию, складывает стик с крестовиной в единый
вектор `[-1,1]` (`pump_input_vector`), а `overlay_pump` формирует
виртуальные Android-события. Крестовина дублирует стик — пригодится на
консолях без аналоговых стиков: в режиме курсора она двигает курсор,
в режиме гироскопа — рулит кораблём.

Кнопки ниже — логические кнопки SDL; их физические номера определяет
раскладка SDL/PortMaster:

| Кнопка | Эффект |
|---|---|
| Левый стик | курсор движется со скоростью отклонения (deadzone ~4000); стик опрашивается каждый кадр |
| Крестовина | дублирует стик: курсор движется непрерывно, пока удержана |
| **A** | зарезервирована, действие временно отключено |
| **B** | клик (touch down/up) в фиксированной точке `(35,345)` |
| **Y** | touch down при нажатии и up при отпускании в `(37,287)` |
| **L2** | клик (touch down/up) в фиксированной точке `(541,446)` |
| **L1** | свайп влево (уклонение) |
| **R1** | свайп вправо (уклонение) |
| **R2** | в гироскопе fire (pid 723), в режиме курсора — touch в позиции курсора |
| **Start** | переключение режимов курсора и гироскопа |
| **X** | зарезервировано |

При старте в логе должно быть:

```
[pad] opened SDL GameController H700 Gamepad (idx 0)
[pad] mapping: 1900f6a24b480000df14000000010000,H700 Gamepad,...
[pad] gamepad ready
```

Диагностика раскладки: при `SDL_NumJoysticks()` хост печатает все джойстики
(имя, GUID, кнопки/оси/hat), затем выбранный контроллер и его фактическую
раскладку. На проверенной H700/ROCKNIX выход через PortMaster — M + Start.

### Альтернативный путь ввода — тач из FIFO

Поймать то же самое можно напрямую через FIFO `/tmp/gof2hd_touch`,
формат `"pid act x y"` (`act`: 0=`нажатие`, 1=`отпускание`; pid — ид
устройства, обычно 722):

```bash
PID=$(pgrep -f 'run-native/gof2hd')
echo "$PID 0 320 240" > /tmp/gof2hd_touch && sleep 0.2 \
  && echo "$PID 1 320 240" >> /tmp/gof2hd_touch
```

По умолчанию тап в центр экрана закрывает логотип и переводит в меню.

### Курсор-прицел (GOF_SHOW_CURSOR)

Чтобы видеть, куда тапнёт геймпад, хост рисует крест-прицел в позиции
виртуального пальца прямо в GL-кадр (модуль `port/host/wrap_overlay.c`):
после кадра движка, но **до** `SDL_GL_SwapWindow`, поэтому курсор физически
не может мигать (в отличие от старой записи в сырой fb0):

```sh
export GOF_SHOW_CURSOR=1
```

- Шаг движения от стика/крестовины — через единый вектор ввода: скорость ∝ отклонению (см. `overlay_input_vector` в `wrap_overlay.c`).
- Отрисовка: минимальный GLES2-шейдер (vec2-атрибут + орто-проекция
  2D, строится из разрешения игры), крест 25×25 px, 3px толщиной, цвет
  (255,80,80), `glDrawArrays(GL_LINES)` перед свапом, с сохранением
  состояния GL движка (программа/буфер/вьюпорт).
- Модуль **не использует `dlopen`**: `gof2hd` линкуется с мостом
  `libGLESv2.so` напрямую (`NEEDED` на наш шим в `run-native`), вызовы —
  обычные `gl*()`. Мост экспортирует softfp-входы (`pcs("aapcs")`),
  поэтому каждый прототип в `wrap_overlay.c` помечен
  `__attribute__((pcs("aapcs")))` — float-аргументы (`glUniform4f`,
  `glUniformMatrix4fv`) передаются в r0–r3, как ждёт шим.
  Потеря символа при этом — ошибка линковки, а не тихий фейл в рантайме.

## 5. Переменные окружения (опционально)

| Переменная | Назначение |
|---|---|
| `GOF_SHOW_CURSOR` | Рисовать курсор-прицел виртуального пальца в GL-кадре (модуль `wrap_overlay`) |
| `GOF_VERBOSE_JNI` | Логировать JNI-вызовы движка |
| `GOF_GDB` | Не устанавливать обработчик краша (под gdbserver) |
| `GOF_TRACE` | Трассировать GLES-вызовы |
| `GOF_LOG` | Путь лога (использует `start-game.sh`) |
| `GOF_ROOT` | Корень порта на устройстве (через `start-game.sh`) |
| `SDL_AUDIODRIVER` | Аудиодрайвер (для порта — `alsa`) |
| `LD_LIBRARY_PATH` | Должен включать `run-native` и `/usr/lib32` |

## 6. Диагностика

- Лог рендера: каждые 120 кадров `[host] N frames; logo=.. menu=.. exit=..`
- При `exit=-1` цикл завершается.
- Область fb0 под игру — первые `width*height*4` байт; снять кадр:
  ```bash
  dd if=/dev/fb0 of=frame.raw bs=1228800 count=1
  ```
- Если `[pad] no gamepad found` — SDL не видит геймпад или не знает его
  раскладку: проверь `/dev/input/js*`, GUID в логе и базу контроллеров PortMaster.

## 7. Ограничения / статус

- Аудио — **работает**: реальные `libfmodex.so`/`libfmodevent.so` из APK
  (патч e_flags) + фейковый OpenSL ES (`libOpenSLES.c`), который маршрутизирует
  PCM-буферы FMOD в SDL2 → ALSA. Логотип/внутриигровые звуки воспроизводятся.
- Для аудио `start-game.sh` автоматически загружает `libm.so`
  (softfp→hardfp math ABI), `libfmod_filesystem.so` (POSIX FEV/FSB),
  `cpuinfo_fake.so` (ожидаемые FMOD `neon/vfp`) и `pthread_bionic.so`
  (bionic pthread/sem → glibc).
- Ввод — встроенный геймпад через SDL2 (см. раздел 4); маппинг заточен
  под факт устройства консоли.
- Проверено: логотип (со звуком), стабильный рендер, курсор движется
  стиком/крестовиной, A — тап, B — фиксированный клик. Переход в главное меню иногда
  затягивается (прогресс по кампании на устройстве не проверялся).

## 8. Структура проекта

| Компонент | Файл | Роль |
|---|---|---|
| host | `host/gof2hd.c` | SDL2-окно/EGL + геймпад-ввод (бэкенд: deadzone/нормализация, стик+крестовина → вектор) + frame loop |
| config | `host/config.c/.h` | разбор CLI и host env в единую `HostConfig` |
| host config | `host/host_config.h` | host/device-константы: deadzone, raw R2, FIFO, env и frame period |
| engine bridge | `host/engine_bridge.c/.h` | dlopen/dlsym, ABI-типизированные JNI-вызовы и передача input в движок |
| touch FIFO | `host/touch_fifo.c/.h` | внешний debug touch input, буферизация неполных строк |
| GL-оверлей + состояние ввода | `host/wrap_overlay.c/.h` | курсор-прицел в GL-кадре перед свапом + `WrawState` (режим курсор/гиро, курсор, кнопки, вектор ввода), геттеры для движка; линкуется с `libGLESv2.so` (`pcs("aapcs")`-прототипы) |
| JNI-эмуляция | `host/jni.c`, `host/jni.h` | Фейк JavaVM/Jobject/jstring |
| libc shim | `shim/shim.c`, `abi.c`, `sscanf.c` | bionic-символы `@LIBC` → glibc |
| libm shim | `shim`/libm.c, `libm.map` | float-math `@LIBC` |
| GLES bridge | `gles-stub/gles-bridge.c` | мост softfp→hardfp GLES2 в libmali (EGL движок не использует) |
| FMOD-хелперы | `fmodex-stub/` | OpenSL→SDL/ALSA, POSIX filesystem, bionic pthread и CPU compatibility |
| OpenSL фейк | `fmodex-stub/libOpenSLES.c` | PCM-буферы FMOD → SDL2/ALSA (звук) |
| pthread/cpuinfo | `fmodex-stub/pthread_bionic.c`, `cpuinfo_fake.c` | LD_PRELOAD: bionic pthread→glibc, фейк /proc/cpuinfo |
| Сборка | `Dockerfile`, `tools/build-docker.sh`, `CMakeLists.txt` | Docker на ПК; CMake/GCC внутри контейнера через `build-native.sh` |
| Запуск | `tools/start-game.sh` | одна команда |
| Техотчёт | `GOF2HD_PORT_NOTES.md` | ABI-детали, история решений |
</content>
