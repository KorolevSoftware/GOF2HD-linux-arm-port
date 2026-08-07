# GOF2HD Linux Port — сборка и запуск

Порт **Galaxy on Fire 2 HD** (Android ARMv7) на Linux-устройства с ARM-процессором:
нативный рендер через аппаратный GPU (Mali), без qemu.

- Технический отчёт «как это устроено внутри», ABI-мост, история решений — в [GOF2HD_PORT_NOTES.md](GOF2HD_PORT_NOTES.md)

## Требования к устройству

- ARM Linux (проверено: консоль ANBERNIC, armhf, ядро с fb0)
- **hardfp glibc** и `/usr/lib32/libmali.so` (или иная GLES2-реализация)
- Инструменты сборки на устройстве: `gcc`/`g++` (arm-linux-gnueabihf), `make`,
  `python3`, `dlfcn`
- Файлы игры на устройстве:
  - `base.apk`
  - `main.<ver>.net.fishlabs.gof2hdallandroid2012.obb` (ресурсы)
  - `libgof2hdaa.so` (движок, извлечён из APK — `lib/armeabi-v7a/`)

> Движок собран с **softfp ABI** (armeabi-v7a, aapcs). Порт собирает всё в hardfp
> и оборачивает мягкие float-вызовы движка через ABI-мост в `shim/` и
> `gles-stub/gles-bridge.c`. Патч `e_flags` в `libgof2hdaa.so` делает бинарь
> совместимым с линкером — выполняется автоматически при сборке.

## 1. Копирование исходников на устройство

```sh
# на ПК (если проект не лежит на устройстве)
scp -r port/ root@<dev-ip>:/root/gof2hd/
```

## 2. Сборка

```sh
cd /root/gof2hd/port
./tools/build-native.sh
```

Скрипт:
- собирает shim-библиотеки (`libc/liblog/libandroid/libm/libdl`),
- мост GLES2 (`libGLESv2.so` → libmali),
- звуковые заглушки FMOD (`libfmodex`, `libfmodevent`),
- хост `gof2hd`,
- патчит `e_flags` у `libgof2hdaa.so`,
- кладёт всё в `port/run-native/`.

## 3. Запуск

### Одна команда (рекомендуется)

Скрипт `port/tools/start-game.sh` делает всё сам: закрывает лаунчер консоли
(он держит fb0/GPU), поднимает демон виртуального геймпада, убивает старую
копию игры и запускает новую в фоне с логом:

```bash
bash /root/gof2hd/port/tools/start-game.sh
```

Настройки через окружение (см. таблицу `GOF_*` ниже):
`GOF_ROOT`, `GOF_LOG`, `GOF_WIDTH`, `GOF_HEIGHT`. Лог — `<GOF_ROOT>/run.txt`
(следить: `tail -f`). Геймпад с ПК: `python3 pad-client.py --host <ip>`.

### Запуск вручную

```bash
cd /root/gof2hd/port/run-native

export GOF_FB=/dev/fb0
export LD_LIBRARY_PATH=/root/gof2hd/port/run-native:/usr/lib32

./gof2hd /root/gof2hd/base.apk \
    '/root/gof2hd/obb/net.fishlabs.gof2hdallandroid2012/main.47947006.net.fishlabs.gof2hdallandroid2012.obb' \
    /root/gof2hd/data 640 480
```

Аргументы: `<base.apk> <main.*.obb> <dataDir> [width] [height]`.

> Перед запуском закройте фронтенд/лаунчер консоли (он держит `/dev/fb0`):
> выйдите в чистый TTY (например `Ctrl+Alt+F1`) и при необходимости
> `killall` процесс GUI/фронтенда, иначе fb0 конфликтует с игрой.

## 4. Управление (тач + виртуальный геймпад)

Игра — тач-управляемая. Хост читает события из FIFO `/tmp/gof2hd_touch`
в формате `"pid act x y"` (`act`: 0=нажатие, 1=отпускание):

```bash
PID=$(pgrep -f 'run-native/gof2hd')
echo "$PID 0 320 240 > /tmp/gof2hd_touch" && sleep 0.2 && echo "$PID 1 320 240 >> /tmp/gof2hd_touch"
```

По умолчанию хоста касание в центр экрана (~640×480) закрывает логотип
и переводит в главное меню.

### Виртуальный геймпад (uinput)

Физические кнопки консоли в этой прошивке не отдают события в evdev,
поэтому геймпад реализован как **виртуальный**: ПК-клиент шлёт кнопки по
UDP, демон на устройстве вставляет их в uinput-джойстик, а мост в хосте
конвертирует их в тачи / кнопку Back.

1. Запустить демон на устройстве (создаёт `/dev/input/eventN` — виртуальный
   джойстик, слушает UDP-порт 4444):
   ```sh
   cd /root/gof2hd/port/pad && ./pad-server
   ```
2. Игра при старте найдёт устройство автообнаружением и напишет
   `[pad] found GOF2HD Virtual Gamepad`.
3. С ПК запустить клиент (клавиатура → кнопки геймпада):
   ```sh
   python3 port/pad/pad-client.py --host 192.168.0.128
   ```

Соответствие клавиш: `стрелки/WASD` — D-pad (движение виртуального тача),
`Enter/Space` — A (тап), `Backspace` — B (Back), `Esc` — Select, `Tab` — Start.

### Курсор-прицел (GOF_SHOW_CURSOR)

Чтобы видеть, куда тапнет геймпад, хост рисует красный крест-прицел в позиции
виртуального пальца прямо в framebuffer поверх кадра (после `eglSwapBuffers`):

```sh
export GOF_SHOW_CURSOR=1
```

- Шаг движения прицела от D-pad — **10 px** на событие (см. `pad_move` в
  `host/gof2hd.c`).
- Отрисовка: mmap `/dev/fb0`, крест 25×25 px, 3px толщиной, цвет (255,80,80)
  BGRA — подтверждено на экране устройства (141 красный пиксель в центре).

### Физический стик и кнопки консоли

Хост сканирует `/dev/input/event*` и сам находит аналоговый стик + D-pad +
кнопки консоли. Раскладка evdev зависит от прошивки, поддерживаются оба
варианта:

- ядро с `adc-joystick` → устройство "adc-joystick", оси ABS_X/ABS_Y;
- сток Anbernic ("ANBERNIC-keys") → стик на ABS_RX/ABS_RY или ABS_Z/ABS_RZ
  (−4096..4096, flat 32), D-pad на стрелках KEY_UP/DOWN/LEFT/RIGHT, кнопки
  BTN_SOUTH/EAST/NORTH/WEST.

Стик двигает крестик **инерциально**: наклон = скорость в px/с, deadzone
берётся из `flat` устройства, в покое крестик стоит:

```sh
export GOF_SHOW_CURSOR=1
```

- Скорость — `GOF_JOY_SPEED` px/с (по умолчанию 350).
- Если стик найден неверно/не найден: `GOF_JOY_DEV=/dev/input/eventN` (или
  через запятую несколько путей), инверсия осей — `GOF_JOY_INVERT=1`.
- Лог при старте: `[joy] stick: <имя> on /dev/input/eventN
  (axes <x>/<y>)`.
- Кнопки: `South`/`East`/`Enter`/`Space`/`LeftCtrl` — тап по крестику,
  `North`/`West`/`Select`/`Esc` — Back, стрелки/D-pad — движение 10px.

## 5. Переменные окружения (опционально)

| Переменная | Назначение |
|---|---|
| `GOF_FB` | Путь fbdev-устройства (по умолчанию `/dev/fb0`) |
| `GOF_EGL_LIB` | Путь к `libEGL.so` (по умолчанию `/usr/lib32/libEGL.so.1`) |
| `GOF_EGL_REQUIRED` | Умерять, если EGL-контекст не создан (иначе продолжить без GPU) |
| `GOF_VERBOSE_JNI` | Логировать JNI-вызовы движка |
| `GOF_TRACE` | Трассировать GLES-вызовы |
| `GOF_AUTO_TOUCH` | Автотап в центр каждые 8 кадров |
| `GOF_GL_DIAG` | Печатать GL_VENDOR/RENDERER/VERSION |
| `GOF_RED_TEST` | Рендер тестовой красной сцены (проверка EGL-path) |
| `GOF_SHOW_CURSOR` | Рисовать курсор-прицел виртуального пальца поверх кадра |
| `GOF_JOY_SPEED` | Скорость физ. стика, px/с (по умолчанию 350) |
| `GOF_JOY_DEV` | Явные пути evdev для физ. стика/кнопок (через запятую) |
| `GOF_JOY_INVERT` | Инвертировать оси физ. стика |
| `GOF_GDB` | Не устанавливать обработчик краша (под gdbserver) |

## 6. Диагностика

- Лог рендера в stdout: каждые 120 кадров пишется `[host] N frames; logo=.. menu=.. exit=..`
- При `exit=-1` цикл завершается.
- Область fb0 под игру — первые `width*height*4` байт; можно снять кадр:
  ```bash
  dd if=/dev/fb0 of=frame.raw bs=1228800 count=1
  ```

## 7. Ограничения / статус

- Аудио (FMOD) — заглушки (`fmodex-stub/`), звука нет.
- Ввод — тач + виртуальный геймпад (см. раздел 4). Физические кнопки консоли
  в evdev не отдают события, поэтому геймпад — uinput-демон с UDP-приёмом.
- Проверено: логотип → главное меню с анимированным фоном, рендер стабильный.
  Геймпад (виртуальный): D-pad → курсор, A → тап, B → Back. Дальнейший
  прогресс по кампании на устройстве не проверялся.

## 8. Структура проекта

| Компонент | Файл | Роль |
|---|---|---|
| host | `host/gof2hd.c` | Фейковая Java-обёртка + рендер-цикл |
| JNI-эмуляция | `host/jni.c`, `host/jni.h` | Фейк JavaVM/Jobject/jstring |
| libc shim | `shim/shim.c`, `abi.c`, `sscanf.c` | bionic-символы `@LIBC` → glibc |
| libm shim | `shim`/libm.c, `libm.map` | float-math `@LIBC` |
| GLES bridge | `gles-stub/gles-bridge.c` | мост softfp→hardfp в libmali |
| fmod stubs | `fmodex-stub/` | заглушки аудио |
| Сборка | `tools/build-native.sh` | всё на устройстве |
| Техотчёт | `GOF2HD_PORT_NOTES.md` | ABI-детали, история решений |
</content>