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

```bash
cd /root/gof2hd/port/run-native

export GOF_FB=/dev/fb0
export LD_LIBRARY_PATH=/root/gof2hd/port/run-native:/usr/lib32

./gof2hd /root/gof2hd/base.apk \
    '/root/gof2hd/obb/net.fishlabs.gof2hdallandroid2012/main.47947006.net.fishlabs.gof2hdallandroid2012.obb' \
    /root/gof2hd/data 640 480
```

Аргументы: `<base.apk> <main.*.obb> <dataDir> [width] [height]`.

## 4. Управление (тач)

Игра — тач-управляемая. Хост читает события из FIFO `/tmp/gof2hd_touch`
в формате `"pid act x y"` (`act`: 0=нажатие, 1=отпускание):

```bash
PID=$(pgrep -f 'run-native/gof2hd')
echo "$PID 0 320 240 > /tmp/gof2hd_touch" && sleep 0.2 && echo "$PID 1 320 240 >> /tmp/gof2hd_touch"
```

По умолчанию хоста касание в центр экрана (~640×480) закрывает логотип
и переводит в главное меню.

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
- Ввод — только тач (и API `BackButtonPressed` в JNI); геймпад пока не подключён
  (см. план: виртуальный геймпад через `/dev/uinput` + UDP-мост).
- Проверено: логотип → главное меню с анимированным фоном, рендер стабильный.
  Дальнейший прогресс по кампании на устройстве не проверялся.

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