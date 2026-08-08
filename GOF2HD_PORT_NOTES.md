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
- SDL сам находит и грузит EGL (несмотря на пустышку `/usr/lib32/libEGL.so.1`
  без символов — SDL2 пробует свои пути, включая libmali), поэтому никакого
  EGL-кода или подложенных библиотек в порте нет вовсе.
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
- **Гироскоп-режим** (`g_gyro_mode`, тумблер START = `leftshoulder`/idx=9):
  левый стик эмулирует акселерометр → `Java_..._ToJNI_handleAccelerometer`.
  Включается кнопкой (START) или env `GOF_GYRO=1`. В режиме гироскопа стик
  перестаёт двигать курсор (его забирает акселерометр), курсор — только крестовина.
- Лог: `[pad] mapped/opened ANBERNIC-keys (idx 0)`, `[pad] gamepad ready`,
  `[pad] gyro mode ON/OFF`.

### Гироскоп-стик: как это работает (реверс)

Движок управляет кораблём от акселерометра: `MGame::handleAccelerometer()`
читает `Engine::GetAccelValue()` (3 double: X/Y/Z из `SetAccelValue`):
- **руль** (left/right) — по `accel[1]` (Y), масштаб ×2.5, порог ±1.0 (т.е. ±0.4);
- **питч** (up/down) — по `accel[2]` (Z): формула `p = f18 - (2-X)` при Z>0,
  сдвиг на константу; нейтраль = **Z ≈ +1.0** (в оригинале Android шлёт
  `z/10 ≈ 0.98` при ровном устройстве).

**ABI-ловушка (критично):** движок softfp (float в r0–r3), host hardfp
(aapcs-vfp, float в s0–s2). Указатель на JNI-функцию с float-аргументами
обязательно помечать `__attribute__((pcs("aapcs")))` — иначе движок читает
мусор из r0–r3 и корабль не реагирует вообще. JNI-стаб `handleAccelerometer`
дополнительно инвертирует первый float: движок видит `SetAccelValue(-a, b, c)`.

**Итоговый маппинг** в `pump_gyro()` (`host/gof2hd.c`), вызывается каждый кадр:
```
ax = 0.0f;      // engine X не используется для руля/питча
ay = -nx;       // engine Y (руль) = -стик X  (стик вправо -> корабль вправо)
az = 1.0f + ny; // engine Z (питч) = 1.0 + стик Y  (нейтраль Z=1, вверх -> >1)
```
Проверено на устройстве: руль и питч работают, в нейтрали корабль летит ровно.

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
  GLES+EGL мост (`gles-bridge.c` → `libGLESv2.so`; EGL — SDL находит сам),
  FMOD-заглушки, хост `gof2hd` (`-lSDL2`), патчит e_flags игры soft→hardfp.
- `start-game.sh` ставит env: `SDL_AUDIODRIVER=dummy` (видеодрайвер SDL2
  выбирает сам),
  `GOF_SHOW_CURSOR=1`, `LD_LIBRARY_PATH=.:/usr/lib32`.

Ручной запуск:

```sh
cd /root/gof2hd/port/run-native
export SDL_AUDIODRIVER=dummy GOF_FB=/dev/fb0 GOF_SHOW_CURSOR=1
export LD_LIBRARY_PATH=/root/gof2hd/port/run-native:/usr/lib32
./gof2hd /root/gof2hd/base.apk \
    '/root/gof2hd/obb/net.fishlabs.gof2hdallandroid2012/main.47947006.net.fishlabs.gof2hdallandroid2012.obb' \
    /root/gof2hd/data
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
8. `initialize(w,h)` → `resize(w,h)` — w/h = разрешение экрана,
   полученное от SDL (полный экран, `SDL_GetWindowSize`)
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

Обёртки живут в: `gles-stub/gles-bridge.c` (GLES→libmali), `shim/libm.c`
(float-math), `shim/stdio.c`, `shim/abi.c`. EGL движок не использует:
контекст создаёт SDL, а тот находит EGL сам (см. §2) — никакого кода EGL
в мосте нет.

**Почему нельзя просто `SF float acosf(float a) { return acosf(a); }`:**
- Это рекурсивный вызов самого себя (компилятор видит вызов символа `acosf`,
  который мы и определяем) → бесконечная рекурсия / переполнение стека.
- Даже если бы имя резолвилось на glibc-функцию, вызов без `pcs("aapcs-vfp")`
  передал бы float неверно: движок кладёт float в r0-r3 (softfp), а hardfp
  функция ждёт его в s0-s15. `pcs` входит в тип, поэтому нужен явный указатель
  с правильной конвенцией.

Правильный паттерн (`shim/libm.c`):
```c
typedef float (*HF)(float) __attribute__((pcs("aapcs-vfp")));
static HF gl_acosf;                       // тип = hardfp (float в s0-s15)
__attribute__((constructor)) void libm_init(void) {
    gl_acosf = (HF)dlsym(dlopen("libm.so.6", RTLD_NOW), "acosf"); // настоящий символ
}
SF float acosf(float a) { return gl_acosf(a); }  // вызов через HF-pointer
```

Так работает и в `gles-bridge.c` (каждая GLES2-функция: `__SF` обёртка +
`__HF` указатель на libmali), и в `stdio.c`/`shim.c`.

**Важно о GLES:** движок импортирует **только GLES2-символы** (проверено
`readelf`; GLES1-функций в UND-списке нет), поэтому в мосте только они.
EGL в движке нет вовсе — его контекст не создаётся игрой.

## 7. Версии символов и маппинг на glibc (решено: ELF-патч)

Движок импортирует bionic-символы с версией `@LIBC`; glibc не имеет такого
узла. **Решение — гибридный ELF-патч** (`tools/patch-versions.py`).

### Как функции попадают в glibc или в shim

После патча каждый импорт движка резолвится ровно одним из двух путей:

1. **`@LIBC` остаётся** у символов, которые предоставляет наш shim:
   stat/fstat/mktime, FILE-пул (`__sF`, fopen/fread/...), pthread_*,
   softfp-математика (`sinf` и т.п. через `libm.so`). Динамический линкер
   обязан резолвить их в shim (единственный провайдер узла `LIBC`) — именно
   это гарантирует попадание в наши ABI-трансляторы (см. §6).
2. **Версия зануляется** у всех прочих импортов (malloc/strlen/memcpy/open/...):
   они становятся unversioned и биндятся **сразу в glibc** (первый провайдер
   в scope) — без обёрток-форвардеров. Т.е. обычные функции libc «мапаются
   на glibc» автоматически самим динамическим линкером: патчер лишь убирает
   версию `@LIBC`, чтобы glibc (а не наш shim) их подхватил.

### Роль `tools/patch-versions.py`

- Читает `.dynsym`/`.dynstr`/`.gnu.version` движка и экспорт собранных
  `libc.so`/`libm.so`; «keep-list» (что оставить `@LIBC`) берётся **из
  реального экспорта shim-библиотек**, поэтому ручная поддержка списка не
  нужна — добавил символ в shim → патчер автоматически оставит ему версию.
- `.gnu.version_r`/DT_VERNEED **не трогаются** — узел `LIBC` остаётся
  резолвимым для shim-набора.
- Вызывается из `tools/build-native.sh` после e_flags-патча:
  `python3 tools/patch-versions.py <engine.so> <shim/libc.so> <shim/libm.so>`.

**Важно (урок):** нельзя занулять ВСЕ версии сразу — unversioned-импорты
резолвятся по порядку scope, где glibc libc.so.6 (зависимость host-бинаря)
стоит раньше нашего `./libc.so`, поэтому трансляционные обёртки
(stat/FILE/pthread) молча перестают использоваться и игра падает в glibc на
bionic-структурах (крэш `fault 0x8` внутри pthread/stdio).

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
- Энва-флаги отладки: `GOF_TRACE` (GL-трейс), `GOF_FB`,
  `GOF_GDB`, `GOF_VERBOSE_JNI`, `GOF_SHOW_CURSOR`.

## 10. Структура проекта

| Компонент | Файл | Роль |
|---|---|---|
| host | `host/gof2hd.c` | SDL2-окно/EGL + геймпад-ввод + движения Java-обёртки |
| JNI-эмуляция | `host/jni.c`, `jni.h` | Фейковые JavaVM/Jobject/jstring |
| libc shim | `shim/shim.c`, `abi.c`, `sscanf.c`, `stdio.c` | трансляция `@LIBC`-набора (FILE/stat/pthread), спецсимволы |
| libm shim | `shim/libm.c`, `libm.map` | float-math `@LIBC` (softfp→hardfp) |
| GLES bridge | `gles-stub/gles-bridge.c` | softfp→hardfp GLES2 в libmali (EGL движок не использует) |
| fmod stubs | `fmodex-stub/` | Заглушки аудио FMOD |
| Патч версий | `tools/patch-versions.py` | зануляет VERSYM у не-shim импортов движка |
| Сборка | `tools/build-native.sh` | всё на устройстве |
| Запуск | `tools/start-game.sh` | одна команда |