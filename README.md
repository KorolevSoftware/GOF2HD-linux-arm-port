# GOF2HD Linux Port — сборка и запуск

Порт **Galaxy on Fire 2 HD** (Android ARMv7) на Linux-устройства с ARM-процессором:
нативный рендер через аппаратный GPU (Mali), без qemu.

- Технический отчёт «как это устроено внутри», ABI-мост, история решений — в [GOF2HD_PORT_NOTES.md](GOF2HD_PORT_NOTES.md)

## Требования к устройству

- ARM Linux (проверено: консоль ANBERNIC, armhf, ядро с fb0)
- **hardfp glibc** и `/usr/lib32/libmali.so` (или иная GLES2-реализация)
- **SDL2** (32-бит armhf): бинарный `.so` в `/usr/lib32/libSDL2-2.0.so.0`,
  заголовки `/usr/include/SDL2`; драйвер `mali` (fbdev)
- Инструменты сборки на устройстве: `gcc`/`g++` (arm-linux-gnueabihf),
  `python3`, `dlfcn`
- Файлы игры на устройстве:
  - `base.apk`
  - `main.<ver>.net.fishlabs.gof2hdallandroid2012.obb` (ресурсы)
  - `libgof2hdaa.so` (движок, извлечён из APK — `lib/armeabi-v7a/`)

> Движок собран с **softfp ABI** (armeabi-v7a, aapcs). Порт собирает всё в hardfp
> и оборачивает мягкие float-вызовы движка через ABI-мост в `shim/` и
> `gles-stub/gles-bridge.c`. Мост также экспортирует EGL-функции в `libmali.so`
> (нужно для SDL). Патч `e_flags` в `libgof2hdaa.so` делает бинарь
> совместимым с линкером — выполняется автоматически при сборке.

## 1. Копирование исходников на устройство

```sh
# на ПК (если проект не лежит на устройстве)
scp -r port/ root@<dev-ip>:/root/gof2hd/
```

## 2. Сборка

Сборка целиком выполняется на устройстве (никакого кросскомпиля).

```sh
cd /root/gof2hd/port
./tools/build-native.sh
```

Скрипт:
- собирает shim-библиотеки (`libc/liblog/libandroid/libm/libdl`),
- мост GLES2 + EGL (`gles-stub/gles-bridge.c` → `libGLESv2.so`, форвардит
  GLES в libmali; из него же кладёт `libEGL.so`/`libEGL.so.1` — их грузит SDL),
- звуковые заглушки FMOD (`libfmodex`, `libfmodevent`),
- хост `gof2hd` (SDL2: `-lSDL2`),
- патчит `e_flags` у `libgof2hdaa.so`,
- кладёт всё в `port/run-native/`.

> Примечание: `/usr/lib32/libEGL.so.1` на прошивке — пустышка без EGL-символов.
> Поэтому SDL грузит наш `libEGL.so.1` из `run-native`, а все EGL-символы
> форвардятся на реальные из `libmali.so`.

## 3. Запуск

### Одна команда (рекомендуется)

Скрипт `port/tools/start-game.sh` делает всё сам: закрывает лаунчер консоли
(он держит fb0/GPU), убивает старую копию игры и запускает новую в фоне
с логом:

```bash
bash /root/gof2hd/port/tools/start-game.sh
```

Скрипт ставит окружение: `SDL_VIDEODRIVER=mali`, `SDL_AUDIODRIVER=dummy`,
`LD_LIBRARY_PATH=.:/usr/lib32`, `GOF_SHOW_CURSOR=1`.

Настройки через окружение (см. таблицу переменных ниже):
`GOF_ROOT`, `GOF_LOG`, `GOF_WIDTH`, `GOF_HEIGHT`. Лог — `<GOF_ROOT>/run.txt`
(следить: `tail -f`).

### Запуск вручную

```bash
cd /root/gof2hd/port/run-native

export SDL_VIDEODRIVER=mali
export SDL_AUDIODRIVER=dummy
export GOF_FB=/dev/fb0
export GOF_SHOW_CURSOR=1
export LD_LIBRARY_PATH=/root/gof2hd/port/run-native:/usr/lib32

./gof2hd /root/gof2hd/base.apk \
    '/root/gof2hd/obb/net.fishlabs.gof2hdallandroid2012/main.47947006.net.fishlabs.gof2hdallandroid2012.obb' \
    /root/gof2hd/data 640 480
```

Аргументы: `<base.apk> <main.*.obb> <dataDir> [width] [height]`.

> Перед запуском закройте фронтенд/лаунчер консоли (он держит `/dev/fb0`):
> `start-game.sh` или `/etc/init.d/launcher.sh stop`, иначе fb0 конфликтует.

## 4. Управление — встроенный геймпад консоли (SDL2 GameController)

Игра тач-управляемая. Хост получает ввод через SDL2 GameController
(API `SDL_GameController`, маппинг добавляется в рантайме). Опробовано на
встроенном геймпаде ANBERNIC (`ANBERNIC-keys`, /dev/input/js0).

Раскладка (из данных живого захвата консоли):

| Действие | Кнопка | Эффект |
|---|---|---|
| Левый стик | `a0`/`a1` | курсор движется со скоростью отклонения (deadzone ~4000) |
| Крестовина | hat0 | курсор на 10 px (UP=1, RIGHT=2, DOWN=4, LEFT=8 — факт для прошивки) |
| **A** | `b0` | тап (touch down/up) в позиции курсора |
| **B** | `b3` | Back (BackButtonPressed — закрыть меню/диалог) |
| **X** | `b2` | зарезервировано |
| **Y** | `b1` | зарезервировано |

Устройство и его GUID: `ANBERNIC-keys`
(`19000000010000000100000000010000`, 17 кнопок, 4 оси, 1 hat).
Маппинг кнопок/осей задаётся в `add_dev_mapping()` в `host/gof2hd.c`
и зарегистрирован через `SDL_GameControllerAddMapping`. Если прошивка
отдаёт другой GUID/раскидкой — поправьте лишь строку маппинга там же.

При старте в логе должно быть:

```
[pad] mapped ANBERNIC-keys (19000000010000000100000000010000)
[pad] opened ANBERNIC-keys (idx 0)
[pad] gamepad ready
```

Диагностика раскладки: при `SDL_NumJoysticks()` хост печатает все джойстики
(имя, GUID, кнопки/оси/hat). Кнопки A/B/X/Y определяются по live-захвату.

### Альтернативный путь ввода — тач из FIFO

Поймать то же самое можно напрямую через FIFO `/tmp/gof2hd_touch`,
формат `"pid act x y"` (`act`: 0=`нажатие`, 1=`отпускание`; pid — ид
устройства, обычно 722):

```bash
PID=$(pgrep -f 'run-native/gof2hd')
echo "$PID 0 320 240" > /tmp/gof2hd_touch && sleep 0.2 \
  && echo "$PID 1 320 240" >> /tmp/gof2hd_touch
```

По умолчанию тап в центр экрана (~640×480) закрывает логотип и переводит
в меню.

### Курсор-прицел (GOF_SHOW_CURSOR)

Чтобы видеть, куда тапнёт геймпад, хост рисует крест-прицел в позиции
виртуального пальца прямо в framebuffer поверх кадра (после swap):

```sh
export GOF_SHOW_CURSOR=1
```

- Шаг движения от крестовины — **10 px** на событие (см. `pad_move`).
- Отрисовка: mmap `/dev/fb0`, крест 25×25 px, 3px толщиной, цвет (255,80,80)
  BGRA.

## 5. Переменные окружения (опционально)

| Переменная | Назначение |
|---|---|
| `GOF_FB` | Путь fbdev-устройства (по умолчанию `/dev/fb0`) |
| `GOF_SHOW_CURSOR` | Рисовать курсор-прицел виртуального пальца поверх кадра |
| `GOF_VERBOSE_JNI` | Логировать JNI-вызовы движка |
| `GOF_GDB` | Не устанавливать обработчик краша (под gdbserver) |
| `GOF_TRACE` | Трассировать GLES-вызовы |
| `GOF_LOG` | Путь лога (использует `start-game.sh`) |
| `GOF_WIDTH`/`GOF_HEIGHT` | Разрешение (через `start-game.sh`) |
| `GOF_ROOT` | Корень порта на устройстве (через `start-game.sh`) |
| `SDL_VIDEODRIVER` | Видеодрайвер SDL2 (у нас — `mali`) |
| `SDL_AUDIODRIVER` | Аудиодрайвер (для порта — `dummy`) |
| `LD_LIBRARY_PATH` | Должен включать `run-native` и `/usr/lib32` |

## 6. Диагностика

- Лог рендера: каждые 120 кадров `[host] N frames; logo=.. menu=.. exit=..`
- При `exit=-1` цикл завершается.
- Область fb0 под игру — первые `width*height*4` байт; снять кадр:
  ```bash
  dd if=/dev/fb0 of=frame.raw bs=1228800 count=1
  ```
- Если `[pad] no gamepad found` — SDL не видит геймпад: проверь
  `/dev/input/js*` и имя устройства (`ANBERNIC-keys`), GUID в логе.

## 7. Ограничения / статус

- Аудио (FMOD) — заглушки (`fmodex-stub/`), звука нет.
- Ввод — встроенный геймпад через SDL2 (см. раздел 4); маппинг заточен
  под факт устройства консоли.
- Проверено: логотип → главное меню с анимированным фоном, стабильный рендер,
  кур-сор движется стиком/крестовиной, A — тап, B — Back. Дальнейший
  прогресс по кампании на устройстве не проверялся.

## 8. Структура проекта

| Компонент | Файл | Роль |
|---|---|---|
| host | `host/gof2hd.c` | SDL2-окно/EGL + геймпад-ввод + фейковая виртуальная JVM-обёртка |
| JNI-эмуляция | `host/jni.c`, `host/jni.h` | Фейк JavaVM/Jobject/jstring |
| libc shim | `shim/shim.c`, `abi.c`, `sscanf.c` | bionic-символы `@LIBC` → glibc |
| libm shim | `shim`/libm.c, `libm.map` | float-math `@LIBC` |
| GLES+EGL bridge | `gles-stub/gles-bridge.c` | мост softfp→hardfp в libmali + EGL-форвардеры |
| fmod stubs | `fmodex-stub/` | заглушки аудио |
| Сборка | `tools/build-native.sh` | всё на устройстве |
| Запуск | `tools/start-game.sh` | одна команда |
| Техотчёт | `GOF2HD_PORT_NOTES.md` | ABI-детали, история решений |
</content>