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
> `gles-stub/gles-bridge.c`. Движок использует только GLES2 (контекст он не
> создаёт) — EGL для него не нужен вовсе. Патч `e_flags` в `libgof2hdaa.so`
> делает бинарь совместимым с линкером — выполняется автоматически при сборке.

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
- мост GLES2 (`gles-stub/gles-bridge.c` → `libGLESv2.so`, форвардит только
  GLES в libmali; движок использует лишь GLES2 и EGL не трогает),
- звуковые заглушки FMOD (`libfmodex`, `libfmodevent`),
- хост `gof2hd` (SDL2: `-lSDL2`; модуль оверлея `host/wrap_overlay.c`
  линкуется напрямую с собранным мостом `libGLESv2.so`),
- патчит `e_flags` у `libgof2hdaa.so`,
- кладёт всё в `port/run-native/`.

> Примечание: `/usr/lib32/libEGL.so.1` на прошивке — пустышка без EGL-символов,
> но SDL2 сам находит и грузит EGL по своим путям (в т.ч. libmali), так что
> в `run-native` ничего для EGL класть не нужно.

## 3. Запуск

### Одна команда (рекомендуется)

Скрипт `port/tools/start-game.sh` делает всё сам: закрывает лаунчер консоли
(он держит fb0/GPU), убивает старую копию игры и запускает новую в фоне
с логом:

```bash
bash /root/gof2hd/port/tools/start-game.sh
```

Скрипт ставит окружение: `SDL_AUDIODRIVER=alsa`,
`LD_LIBRARY_PATH=.:/usr/lib32`, `GOF_SHOW_CURSOR=1` и необходимые
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
export LD_LIBRARY_PATH=/root/gof2hd/port/run-native:/usr/lib32

./gof2hd /root/gof2hd/base.apk \
    '/root/gof2hd/obb/net.fishlabs.gof2hdallandroid2012/main.47947006.net.fishlabs.gof2hdallandroid2012.obb' \
    /root/gof2hd/data
```

Аргументы: `<base.apk> <main.*.obb> <dataDir>`.
Окно — полный экран; разрешение хост берёт из SDL
(`SDL_CreateWindow` + `SDL_GetWindowSize`).

> Перед запуском закройте фронтенд/лаунчер консоли (он держит `/dev/fb0`):
> `start-game.sh` или `/etc/init.d/launcher.sh stop`, иначе fb0 конфликтует.

## 4. Управление — встроенный геймпад консоли (SDL2 GameController)

Игра тач-управляемая. Хост получает ввод через SDL2 GameController
(API `SDL_GameController`, маппинг добавляется в рантайме). Опробовано на
встроенном геймпаде ANBERNIC (`ANBERNIC-keys`, /dev/input/js0).

Состояние ввода (режим курсор/гироскоп, позиция курсора, кнопки, вектор
ввода) живёт в `wrap_overlay` (`WrawState`); `gof2hd.c` — бэкенд: жмёт
deadzone/нормализацию, складывает стик с крестовиной в единый вектор
`[-1,1]` (`pump_input_vector`) и кормит движок из геттеров
(`pump_engine_input`). Крестовина дублирует стик — пригодится на
консолях без аналоговых стиков: в режиме курсора она двигает курсор,
в режиме гироскопа — рулит кораблём.

Раскладка (из данных живого захвата консоли):

| Действие | Кнопка | Эффект |
|---|---|---|
| Левый стик | `a0`/`a1` | курсор движется со скоростью отклонения (deadzone ~4000); стик опрашивается каждый кадр — удерживаемый отклонённым стик продолжает двигать курсор |
| Крестовина | hat0 | дублирует стик: даёт полное отклонение в сторону нажатия (курсор движется непрерывно, пока удержана) |
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

- Аудио — **работает**: реальные `libfmodex.so`/`libfmodevent.so` из APK
  (патч e_flags) + фейковый OpenSL ES (`libOpenSLES.c`), который маршрутизирует
  PCM-буферы FMOD в SDL2 → ALSA. Логотип/внутриигровые звуки воспроизводятся.
- Для аудио нужны LD_PRELOAD: `cpuinfo_fake.so` (FMOD читает /proc/cpuinfo и
  требует `neon/vfp`) и `pthread_bionic.so` (bionic pthread/sem → glibc).
  `start-game.sh` ставит их автоматически; `gof2hd` сам вызывает
  `FMOD_Memory_Initialize` (glibc malloc вместо внутреннего пула FMOD).
- Ввод — встроенный геймпад через SDL2 (см. раздел 4); маппинг заточен
  под факт устройства консоли.
- Проверено: логотип (со звуком), стабильный рендер, курсор движется
  стиком/крестовиной, A — тап, B — Back. Переход в главное меню иногда
  затягивается (прогресс по кампании на устройстве не проверялся).

## 8. Структура проекта

| Компонент | Файл | Роль |
|---|---|---|
| host | `host/gof2hd.c` | SDL2-окно/EGL + геймпад-ввод (бэкенд: deadzone/нормализация, стик+крестовина → вектор) + фейковая виртуальная JVM-обёртка |
| GL-оверлей + состояние ввода | `host/wrap_overlay.c/.h` | курсор-прицел в GL-кадре перед свапом + `WrawState` (режим курсор/гиро, курсор, кнопки, вектор ввода), геттеры для движка; линкуется с `libGLESv2.so` (`pcs("aapcs")`-прототипы) |
| JNI-эмуляция | `host/jni.c`, `host/jni.h` | Фейк JavaVM/Jobject/jstring |
| libc shim | `shim/shim.c`, `abi.c`, `sscanf.c` | bionic-символы `@LIBC` → glibc |
| libm shim | `shim`/libm.c, `libm.map` | float-math `@LIBC` |
| GLES bridge | `gles-stub/gles-bridge.c` | мост softfp→hardfp GLES2 в libmali (EGL движок не использует) |
| fmod stubs | `fmodex-stub/` | заглушки аудио (не используются) |
| OpenSL фейк | `fmodex-stub/libOpenSLES.c` | PCM-буферы FMOD → SDL2/ALSA (звук) |
| pthread/cpuinfo | `fmodex-stub/pthread_bionic.c`, `cpuinfo_fake.c` | LD_PRELOAD: bionic pthread→glibc, фейк /proc/cpuinfo |
| Сборка | `tools/build-native.sh` | всё на устройстве (в т.ч. копирование реальных FMOD из APK) |
| Запуск | `tools/start-game.sh` | одна команда |
| Техотчёт | `GOF2HD_PORT_NOTES.md` | ABI-детали, история решений |
</content>
