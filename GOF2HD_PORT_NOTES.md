# GOF2HD Linux Port — технический отчёт

Дата: 2026-08-07
Проект: порт Galaxy on Fire 2 HD (Android, ARMv7, bionic) на Linux ARM-устройства.
Статус: **работает** — игра запускается и рендерится на аппаратном GPU (Mali)
через SDL2-окно + ABI-мост softfp→hardfp.

## 1. Устройство (эталон)

- **ANBERNIC (стоковая прошивка), root@192.168.0.128, armhf glibc 2.35 + aarch64 kernel.**
- Пути: `/root/gof2hd/` (проект), `/root/gof2hd/port/run-native/` (собранное),
  `/root/gof2hd/port_bak/` (бэкап), `/dev/fb0` (framebuffer), `/dev/input/js0` (геймпад).
- GPU: настоящие EGL/GLES — в `/usr/lib32/libmali.so.0.20.0`. Пустышка
  `/usr/lib32/libEGL.so.1.4.0` (2968 Б, без EGL-символов).
- glibc 2.35: `sscanf` резолвится как `__isoc99_sscanf@GLIBC_2.7` — обычный
  `sscanf` использовать можно.
- Файлы игры: `base.apk`, `main.<ver>.net.fishlabs.gof2hdallandroid2012.obb`,
  `libgof2hdaa.so` (движок, извлечён из APK `lib/armeabi-v7a/`).

## 2. Видео-путь (работает)

- Хост рисует через **SDL2** (видеодрайвер `mali`, ядро fb0) + EGL.
- SDL грузит наш `libEGL.so.1` из `run-native`, наши EGL-символы форвардятся
  на реальные из `libmali.so` (`gles-stub/gles-bridge.c`, секция EGL_FWD).
- В логе: `SDL window ready (video driver: mali)`, `logo=1` — рендер идёт.

## 3. Геймпад (ввод)

- **Встроенный** геймпад консоли `ANBERNIC-keys` (js0/event1, gpio-keys-polled).
  Виртуальный uinput-геймпад **удалён** из проекта.
- SDL GameController, маппинг добавляется в `add_dev_mapping()` в `host/gof2hd.c`:
  `"a:b0,b:b3,x:b2,y:b1,leftshoulder:b7,rightshoulder:b6,lefttrigger:b5,righttrigger:b4,
    back:b12,start:b9,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,
    leftx:a0,lefty:a1,rightx:a2,righty:a3"`
- Hat-значения крестовины (прошивка): UP=1, RIGHT=2, DOWN=4, LEFT=8
  (сдвиг от SDL-стандарта 0/1/2/3 — уже учтено в маппинге).
- Логика: левый стик → курсор (скорость ∝ отклонению, deadzone 4000,
  `SDL_CONTROLLER_AXIS_LEFTX/LEFTY`), крестовина → шаг 10 px,
  A (`b0`) → touch down/up (pid 722), B (`b3`) → BackButtonPressed.
- Лог: `[pad] mapped/opened ANBERNIC-keys (idx 0)`, `[pad] gamepad ready`.

## 4. Сборка и запуск — 3 команды

```sh
# 1) скопировать порт на устройство (с ПК)
scp -r port/ root@192.168.0.128:/root/gof2hd/
# 2) собрать прямо на устройстве (armhf, без кросскомпилятора)
cd /root/gof2hd/port && bash tools/build-native.sh   # ждать "== done =="
# 3) запуск одной командой (сам гасит лаунчер, убивает старую игру, поднимает лог)
bash /root/gof2hd/port/tools/start-game.sh
# лог: tail -f /root/gof2hd/run.txt
```

- Сборка идёт ТОЛЬКО на устройстве (`gcc/g++ arm-linux-gnueabihf`, SDL2 dev
  в системе). `build-native.sh` собирает: shim (libc/liblog/libandroid/libm/libdl),
  GLES+EGL мост (`gles-bridge.c` → `libGLESv2.so`, из него же `libEGL.so(.1)`),
  FMOD-заглушки, хост `gof2hd` (`-lSDL2`), патчит e_flags игры soft→hardfp.
- `start-game.sh` ставит env: `SDL_VIDEODRIVER=mali`, `SDL_AUDIODRIVER=dummy`,
  `GOF_SHOW_CURSOR=1`, `LD_LIBRARY_PATH=.:/usr/lib32`.

Ручной запуск:

```sh
cd /root/gof2hd/port/run-native
export SDL_VIDEODRIVER=mali SDL_AUDIODRIVER=dummy GOF_FB=/dev/fb0 GOF_SHOW_CURSOR=1
export LD_LIBRARY_PATH=/root/gof2hd/port/run-native:/usr/lib32
./gof2hd /root/gof2hd/base.apk \
    '/root/gof2hd/obb/net.fishlabs.gof2hdallandroid2012/main.47947006.net.fishlabs.gof2hdallandroid2012.obb' \
    /root/gof2hd/data 640 480
```

## 5. Как игра работает (механика запуска)

Android-приложение = Java-обёртка + нативный `libgof2hdaa.so`. Наш
`host/gof2hd.c` повторяет последовательность Java через `dlsym`/`dlopen`.

**Последовательность запуска (работает):**
1. `JNI_OnLoad`
2. `setZIPPath(obb)` — путь к OBB-архиву с ресурсами
3. `SetDirectories(dataDir, obbdir)`
4. `setAPKPath(base.apk)`
5. `setEnvironmentVariables(context)`
6. `setCountryCodeOfDevice(0)`
7. `SetOrigamiSuperClub("...")` — иначе краш в SHA256 (NULL-указатель)
8. `initialize(640,480)` → `resize(640,480)`
9. Цикл: `renderstep(now_ms())` каждые ~33 мс

**Проверенные факты:**
- Игра **запускается и рендерит** (логотип, меню, кадры идут).
- Touch-событие `(pointerId, action, x, y)` (pointerId=722, 0=down/1=up/2=move)
  из `TouchHandler.java` — A-кнопка шлёт down+up.
- Контент читается **напрямую из OBB через zip** (libzip), НЕ распаковывается;
  `dataDir` остаётся пустым.

## 6. КРИТИЧНО: ABI игры — softfp (и решение)

**Подтверждено дизассемблером `libgof2hdaa.so`:**
- float-аргументы передаются в r0–r3 / на стек (`vmov r0, s2`; `vstr s0, [sp]`);
  `Tag_ABI_HardFP_use: Deprecated`, нет `Tag_ABI_VFP_args` → **softfp ABI**.

На устройстве весь системный стек — hardfp (glibc, libmali, SDL2), а hardfp
loader не грузит softfp `.so`. Это главное противоречие проекта.

**Решение — ВЫБРАНО И РАБОТАЕТ:** конвертировать вызовы игры softfp→hardfp
через ABI-мост учётом бинарника. Суть моста (см. `gles-bridge.c`):

```c
typedef void (*clear_hf)(float,float,float,float) __attribute__((pcs("aapcs-vfp")));
__attribute__((pcs("aapcs"))) void clear_soft(float r,float g,float b,float a) {
    clear_hf(r,g,b,a);  // hardfp-вызов внутрь libmali
}
```

- `pcs("aapcs")` = softfp-приёмник (float приходит в r0-r3)
- `pcs("aapcs-vfp")` = hardfp-указатель (float в s0-s15)

Обёртки живут в: `gles-stub/gles-bridge.c` (GLES + EGL→libmali), `shim/libm.c`
(float-math), `shim/stdio.c`, `shim/abi.c`. У EGL нет float-аргументов, поэтому
для него работает универсальный `EGL_FWD`-макрос (8×`void*`).

**Важно о GLES:** движок импортирует **только GLES2-символы** (проверено
`readelf`; GLES1-функций в UND-списке нет), поэтому в мосте только они + EGL.

## 7. Версии символов (решено)

Движок импортирует bionic-символы с версией `@LIBC`; glibc навязывает
pthread-definitions версию `GLIBC_2.4`, блокируя `@@LIBC`-экспорт.

**Решение:** НЕ включать `<pthread.h>`/`<semaphore.h>`. Типы берутся из
`<sys/types.h>` (→ `bits/pthreadtypes.h`); `sem_t` определён вручную
(`shim/types.h`, 16 Б). После этого экспортируются все 79 символов `@LIBC`,
требуемых игрой (version-script `shim/version.map` / `libm.map`).

**Типы структур на устройстве (armhf glibc):** pthread_mutex_t=24, cond=48,
attr=36, mutexattr=4, condattr=4, sem_t=16, key_t=4, once_t=4.

## 8. Нюансы проекта (уже учтено в коде)

- Сборка только на устройстве: старые кроссприйл-скрипты, софт-GLES стаб,
  viewer и виртуальный uinput-геймпад **удалены** из репозитория.
- В `gof2hd.c` убран костыль `legacy_sscanf` (glibc 2.35 — обычный `sscanf`
  допустим; в коде снова `sscanf("%d %d %d %d", ...)`).

## 9. Операционные нюансы

- **Не использовать `pkill -f`/`pgrep -f` с совпадением имени** — фантом
  убивает саму команду. Процессы убивать только по PID, `kill -9` (SIGTERM
  ловится crash-handler'ом игры).
- SSH из этой среды: `export SSH_ASKPASS=/tmp/opencode/askpass.sh
  SSH_ASKPASS_REQUIRE=force DISPLAY=dummy:0`.
- git push невозможен из среды (SSH с ключом отклоняется) — пушит пользователь.
- gdbserver: `gdbserver 0.0.0.0:2345 ./gof2hd ...`; клиент —
  `arm-buildroot-linux-gnueabi-gdb` + `target remote <dev-ip>:2345`.
- Энва-флаги отладки: `GOF_TRACE` (GL-трейс), `GOF_FB`, `GOF_AUTO_TOUCH`,
  `GOF_GDB`, `GOF_VERBOSE_JNI`, `GOF_SHOW_CURSOR`.

## 10. Структура проекта

| Компонент | Файл | Роль |
|---|---|---|
| host | `host/gof2hd.c` | SDL2-окно/EGL + геймпад-ввод + движения Java-обёртки |
| JNI-эмуляция | `host/jni.c`, `jni.h` | Фейковые JavaVM/Jobject/jstring |
| libc shim | `shim/shim.c`, `abi.c`, `sscanf.c`, `stdio.c` | bionic-символы `@LIBC` → glibc, свой `FILE`-пул |
| libm shim | `shim/libm.c`, `libm.map` | float-math `@LIBC` (softfp→hardfp) |
| GLES+EGL bridge | `gles-stub/gles-bridge.c` | softfp→hardfp в libmali, GLES2 + EGL-форварды |
| fmod stubs | `fmodex-stub/` | Заглушки аудио FMOD |
| Сборка | `tools/build-native.sh` | всё на устройстве |
| Запуск | `tools/start-game.sh` | одна команда |