# MP3 Player — документація програми

## Зміст

1. [Загальний опис](#загальний-опис)
2. [Апаратна платформа](#апаратна-платформа)
3. [Структура проєкту](#структура-проєкту)
4. [Архітектура програмного забезпечення](#архітектура-програмного-забезпечення)
5. [Запуск системи](#запуск-системи)
6. [FreeRTOS: задачі та синхронізація](#freertos-задачі-та-синхронізація)
7. [Модулі програми](#модулі-програми)
8. [Потік відтворення аудіо](#потік-відтворення-аудіо)
9. [Керування користувачем](#керування-користувачем)
10. [Інтерфейс користувача (OLED)](#інтерфейс-користувача-oled)
11. [Робота з файловою системою](#робота-з-файловою-системою)
12. [Спільний стан програми](#спільний-стан-програми)
13. [Діаграми потоків](#діаграми-потоків)
14. [Налагодження](#налагодження)
15. [Обмеження та відомі особливості](#обмеження-та-відомі-особливості)

---

## Загальний опис

Проєкт — вбудований MP3-плеєр на мікроконтролері **STM32F401**, який:

- читає `.mp3` файли з **SD-карти** (FatFS);
- декодує їх бібліотекою **Helix MP3 Decoder**;
- виводить звук через **I2S + DMA** на аудіокодек/DAC;
- показує плейлист на **OLED-дисплеї SSD1306** (I2C);
- керується **однією кнопкою** (мультіклік) та **потенціометром** (ADC);
- використовує **FreeRTOS** для паралельної роботи UI та аудіо.

Програма розділена на шар STM32CubeMX (`main.c`) та прикладні модулі (`app_*.c`), щоб код Cube IDE не перезаписував логіку плеєра при регенерації.

---

## Апаратна платформа

| Компонент | Інтерфейс MCU | Призначення |
|-----------|---------------|-------------|
| SD-карта | SPI1 + CS (PA4) | Зберігання MP3-файлів |
| OLED SSD1306 | I2C1 | Відображення плейлиста |
| Аудіовихід | I2S2 (SPI2) + DMA1 Stream4 | Цифрова передача PCM |
| Потенціометр | ADC1, канал 0 (PA0) | Регулювання гучності |
| Кнопка Next/Play | GPIOC, PIN 14 | Керування відтворенням |
| Кнопка Prev | GPIOC, PIN 13 | Зарезервована в GPIO |
| UART1 | 115200 бод | Вивід `printf` для налагодження |

Мікроконтролер: **STM32F401CCUx**.

---

## Структура проєкту

```
mp3_player/
├── Core/
│   ├── Inc/
│   │   ├── main.h           — визначення пінів, заголовок Cube
│   │   ├── app_player.h     — API плеєра, константи, спільний стан
│   │   └── app_sync.h       — мютекси та семафори
│   └── Src/
│       ├── main.c           — ініціалізація HAL, FreeRTOS, задачі
│       ├── app_player.c     — плейлист, FS, головний цикл UI-задачі
│       ├── app_input.c      — ADC (гучність), кнопка (мультіклік)
│       ├── app_ui.c         — OLED-дисплей
│       ├── app_audio.c      — MP3-декодування, I2S, DMA
│       ├── app_sync.c       — обгортки Lock/Unlock, Signal/Wait
│       ├── ssd1306.c        — драйвер дисплея
│       ├── fonts.c          — шрифти для OLED
│       └── fatfs_sd.c       — драйвер SD-карти для FatFS
├── FATFS/                   — конфігурація FatFS
├── lib/helix/               — MP3-декодер Helix
├── Middlewares/             — FreeRTOS, FatFS
└── mp3_player.ioc           — конфігурація STM32CubeMX
```

---

## Архітектура програмного забезпечення

```
┌─────────────────────────────────────────────────────────────┐
│                         main.c                              │
│  HAL_Init → MX_*_Init → osKernelInitialize → osKernelStart  │
└──────────────────────────┬──────────────────────────────────┘
                           │
         ┌─────────────────┴─────────────────┐
         ▼                                   ▼
┌─────────────────────┐             ┌─────────────────────┐
│   defaultTask       │             │   AudioTask         │
│   (пріоритет Normal)│             │   (пріоритет High)  │
│                     │             │                     │
│ App_DefaultTask_*   │             │ App_AudioTask_Run   │
│  ├─ Volume (ADC)   │             │  ├─ MP3 decode      │
│  ├─ Button         │             │  ├─ I2S + DMA       │
│  ├─ Open file      │             │  └─ Volume apply    │
│  └─ UI (OLED)      │             │                     │
└─────────┬───────────┘             └─────────┬───────────┘
          │                                   │
          └───────────┬───────────────────────┘
                      ▼
          ┌───────────────────────┐
          │  Спільний стан + sync │
          │  playerMutex          │
          │  fatfsMutex           │
          │  uiSem                │
          │  fileReadySem         │
          │  dmaSem               │
          └───────────────────────┘
```

**Принцип розділення:**

- `main.c` — лише «каркас» Cube IDE (периферія, RTOS, точки входу задач).
- `app_*.c` — вся логіка плеєра, яку Cube не чіпає при регенерації.

---

## Запуск системи

### Послідовність у `main()`

1. `HAL_Init()` — ініціалізація HAL, SysTick.
2. `SystemClock_Config()` — HSE + PLL, системна частота 84 МГц.
3. Ініціалізація периферії:
   - `MX_GPIO_Init()` — кнопки, CS SD-карти
   - `MX_DMA_Init()` — DMA для I2S
   - `MX_SPI1_Init()` — SPI для SD
   - `MX_USART1_UART_Init()` — UART для `printf`
   - `MX_FATFS_Init()` — підготовка FatFS
   - `MX_I2S2_Init()` — I2S для аудіо
   - `MX_I2C1_Init()` — I2C для OLED
   - `MX_ADC1_Init()` — ADC для потенціометра
4. `osKernelInitialize()` — ініціалізація ядра FreeRTOS.
5. Створення об'єктів синхронізації (мютекси, семафори).
6. Створення задач `defaultTask` та `AudioTask`.
7. `osKernelStart()` — запуск планувальника.

Після `osKernelStart()` функція `main()` більше не виконується — керування переходить до задач FreeRTOS.

### Функція `_write()`

Перенаправляє стандартний вивід `printf` на UART1:

```c
int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, 100);
  return len;
}
```

---

## FreeRTOS: задачі та синхронізація

### Задачі

| Задача | Стек | Пріоритет | Функція |
|--------|------|-----------|---------|
| `defaultTask` | 4 КБ | Normal | UI, ввід, відкриття файлів |
| `AudioTask` | 8 КБ | High | Декодування та відтворення |

`AudioTask` має вищий пріоритет, щоб встигати заповнювати аудіобуфер до того, як DMA його відіграє.

### Мютекси

| Мютекс | Призначення | Що захищає |
|--------|-------------|------------|
| `playerMutex` | Стан плеєра | `file_opened`, `skip_track`, `is_paused`, `current_song_index`, `track_direction`, `force_ui_update`, `pending_resume`, `window_start` |
| `fatfsMutex` | Файлові операції | `f_open`, `f_close`, `f_read`, `f_eof`, `f_mount`, `f_opendir` тощо |

**Навіщо `fatfsMutex`, якщо FatFS має `_FS_REENTRANT = 1`?**

FatFS захищає файлову систему на рівні томів, але один об'єкт `FIL fil` використовується двома задачами. Мютекс гарантує, що `f_close()` в одній задачі не виконається під час `f_read()` в іншій.

### Семафори

| Семафор | Початкове значення | Призначення |
|---------|-------------------|-------------|
| `dmaSem` | 1 | Сигнал від DMA: «половина PCM-буфера відіграна, можна заповнювати» |
| `uiSem` | 0 | Сигнал UI-задачі: «потрібно оновити екран» |
| `fileReadySem` | 0 | Сигнал Audio-задачі: «файл відкритий і готовий до відтворення» |

### API синхронізації (`app_sync.c`)

```c
void App_Player_Lock(void);      // Захопити playerMutex
void App_Player_Unlock(void);    // Звільнити playerMutex
void App_FatFs_Lock(void);       // Захопити fatfsMutex
void App_FatFs_Unlock(void);     // Звільнити fatfsMutex
void App_UI_RequestUpdate(void); // force_ui_update=1 + Release(uiSem)
void App_FileReady_Signal(void); // Release(fileReadySem)
void App_FileReady_Wait(void);   // Acquire(fileReadySem) — блокуюче очікування
```

---

## Модулі програми

### `app_player.c` — ядро плеєра

**Відповідальність:**

- зберігання глобального стану (плейлист, буфери, прапорці);
- монтування SD-карти (`App_FS_Init`);
- сканування MP3-файлів (`App_ScanFiles`);
- відкриття наступного треку (`App_Open_Next_Song`);
- ініціалізація та головний цикл UI-задачі.

**Ініціалізація (`App_DefaultTask_Init`):**

1. Ініціалізація OLED.
2. Монтування FatFS.
3. Сканування кореня SD-карти на `.mp3` файли (до 20 штук).
4. Відкриття першого треку.
5. Запит на оновлення UI.

**Головний цикл (`App_DefaultTask_Loop`):**

```c
App_Volume_Poll();       // читання ADC
App_Button_Poll();         // обробка кнопки
App_Open_Next_Song();      // відкриття файлу, якщо потрібно
osSemaphoreAcquire(uiSemHandle, 50);  // чекати сигнал або 50 мс
App_UI_Update();           // оновлення дисплея
```

Цикл комбінує **подієву модель** (семафор `uiSem`) та **періодичне опитування** (таймаут 50 мс для кнопки й гучності).

---

### `app_input.c` — ввід користувача

#### Гучність (`App_Volume_Poll`)

- Запускає одиночне перетворення ADC.
- Записує результат у `adc_volume` (0–4095).
- Аудіо-задача читає це значення при кожному декодованому фреймі.

Формула гучності:

```c
target_pcm[i] = (int16_t)((sample * adc_volume) >> 15);
```

#### Кнопка (`App_Button_Poll`)

Реалізований **мультіклік** з таймаутом 400 мс (`MULTICLICK_TIMEOUT`):

| Кількість кліків | Дія |
|------------------|-----|
| 1 | Pause / Play |
| 2 | Наступний трек |
| 3+ | Попередній трек |

Алгоритм:

1. При натисканні — збільшити лічильник кліків, зафіксувати час.
2. Debounce 20 мс.
3. Якщо після останнього кліку минуло > 400 мс — виконати дію.

При паузі DMA зупиняється одразу (`HAL_I2S_DMAPause`). При resume — спочатку оновлюється екран, потім DMA відновлюється (`HAL_I2S_DMAResume`), щоб уникнути артефактів на OLED.

---

### `app_ui.c` — інтерфейс OLED

**Екран плейлиста:**

- Заголовок `-PLAYLIST-`.
- До 4 рядків (`MAX_LINES`) з іменами файлів.
- Поточний трек позначається `>` (грає) або `||` (пауза).
- Вертикальний скрол через `window_start`.

**Оновлення екрану (`App_UI_Update`):**

1. Перевіряє прапор `force_ui_update`.
2. Малює екран (якщо потрібно).
3. Встановлює `file_opened = 1`.
4. Якщо файл відкривається **вперше** (`was_opened == 0`) — сигналізує `fileReadySem`.
5. Якщо був `pending_resume` — відновлює DMA.

**Захист:** читання/запис стану плеєра під `playerMutex`.

---

### `app_audio.c` — аудіодвижок

**Основний цикл (`App_AudioTask_Run`):**

```
while (1) {
    App_FileReady_Wait();          // чекати готовності файлу
    MP3InitDecoder();              // створити декодер
    f_read() → preload pcmBuf;      // зчитати та декодувати 2 половини
    App_Configure_I2S();           // налаштувати частоту I2S
    HAL_I2S_Transmit_DMA();        // запустити DMA
    App_Playback_Loop();           // цикл відтворення
}
```

**Буфери:**

| Буфер | Розмір | Призначення |
|-------|--------|-------------|
| `readBuf` | 4096 байт | Сирі MP3-дані з файлу |
| `pcmBuf` | 1152×2×2 семплів | Декодований PCM (2 половини для double buffering) |

**Double buffering через DMA:**

```
pcmBuf: [  ПОЛОВИНА 1  |  ПОЛОВИНА 2  ]
              ↑                ↑
         DMA грає         DMA грає
         Audio заповнює   Audio заповнює
```

Колбеки DMA:

- `HAL_I2S_TxHalfCpltCallback` — перша половина відіграна → `dma_half_ready = 1`
- `HAL_I2S_TxCpltCallback` — друга половина відіграна → `dma_full_ready = 1`

Обидва викликають `osSemaphoreRelease(dmaSemHandle)`.

**Цикл відтворення (`App_Playback_Loop`):**

1. Перевірити `skip_track` (під `playerMutex`).
2. Дочитати MP3-дані з файлу в `readBuf` (під `fatfsMutex`).
3. Перевірити кінець файлу.
4. `osSemaphoreAcquire(dmaSem)` — чекати сигнал DMA.
5. Декодувати наступний MP3-фрейм у вільну половину `pcmBuf`.
6. Застосувати гучність.
7. Повторити.

**Перемикання треку (`App_Skip_Track`):**

1. `HAL_I2S_DMAStop()`.
2. `MP3FreeDecoder()`.
3. `f_close()` під `fatfsMutex`.
4. Змінити `current_song_index` під `playerMutex`.
5. `file_opened = 0` — UI-задача відкриє новий файл.

**Кінець файлу (`App_End_Of_Track`):**

Аналогічно skip, але індекс збільшується на 1 (автоперехід вперед).

**Налаштування I2S:**

Частота I2S підлаштовується під sample rate MP3:

| Sample rate MP3 | I2S частота |
|-----------------|-------------|
| ≥ 48000 Гц | 48 кГц |
| ≥ 44100 Гц | 44.1 кГц |
| ≥ 32000 Гц | 32 кГц |

Після зміни частоти виконується `HAL_I2S_DeInit()` + `HAL_I2S_Init()`.

---

## Потік відтворення аудіо

### Повний цикл від старту до наступного треку

```
1. UI: f_open("song.mp3")
2. UI: App_UI_RequestUpdate()
3. UI: App_UI_Update() → file_opened=1, App_FileReady_Signal()
4. Audio: App_FileReady_Wait() → прокидається
5. Audio: f_read → MP3 decode → preload pcmBuf[0] і pcmBuf[1]
6. Audio: HAL_I2S_Transmit_DMA(pcmBuf)
7. [цикл]
   DMA грає половину 1
   → колбек → dmaSem
   → Audio декодує в половину 1
   DMA грає половину 2
   → колбек → dmaSem
   → Audio декодує в половину 2
   ... (повторюється)
8. Audio: EOF → f_close, file_opened=0, current_song_index++
9. UI: App_Open_Next_Song() → f_open наступного файлу
10. UI: App_UI_Update() → fileReadySem → повернення до кроку 4
```

---

## Керування користувачем

### Пауза / Play (1 клік)

```
Кнопка → is_paused = !is_paused
  ├─ Пауза:  HAL_I2S_DMAPause() + оновити екран (||)
  └─ Play:   pending_resume=1 + оновити екран (>)
             → після малювання: HAL_I2S_DMAResume()
```

### Наступний трек (2 кліки)

```
skip_track=1, track_direction=1, is_paused=0
→ Audio: зупинка DMA, f_close, індекс++, file_opened=0
→ UI: відкриває новий файл
```

### Попередній трек (3+ кліків)

```
skip_track=1, track_direction=-1, is_paused=0
→ аналогічно, але індекс--
```

---

## Інтерфейс користувача (OLED)

### Формат екрану

```
 -PLAYLIST-
> song1.mp3
  song2.mp3
  song3.mp3
  song4.mp3
```

- `>` — поточний трек, грає.
- `||` — поточний трек, пауза.
- Пробіл — інші треки.
- Не-ASCII символи замінюються на `?`.
- Ім'я файлу обрізається до 15 символів.

### Скрол плейлиста

`window_start` — індекс першого видимого рядка. Зсувається автоматично, коли `current_song_index` виходить за межі 4 видимих рядків.

---

## Робота з файловою системою

### FatFS

- Файлова система: **FAT32** на SD-карті.
- Монтування: `f_mount(&fs, "", 1)` при старті.
- Сканування: `f_opendir` + `f_readdir` — пошук файлів з розширенням `.mp3`.
- Максимум треків: **20** (`MAX_SONGS`).

### Доступ до файлів між задачами

| Операція | Задача | Захист |
|----------|--------|--------|
| `f_mount` | UI (init) | `fatfsMutex` |
| `f_open` | UI | `fatfsMutex` |
| `f_read` | Audio | `fatfsMutex` |
| `f_close` | Audio | `fatfsMutex` |
| `f_eof` | Audio | `fatfsMutex` |

Один об'єкт `FIL fil` — спільний. Відкриває UI, читає Audio, закриває Audio.

---

## Спільний стан програми

| Змінна | Тип | Хто пише | Хто читає | Захист |
|--------|-----|----------|-----------|--------|
| `playlist[][]` | char | UI (scan) | UI, Audio | — (тільки UI пише) |
| `total_songs` | int | UI | UI, Audio | — |
| `current_song_index` | int | UI, Audio | UI, Audio | `playerMutex` |
| `file_opened` | volatile uint8_t | UI, Audio | UI, Audio | `playerMutex` |
| `adc_volume` | volatile uint16_t | UI | Audio | — (atomic read) |
| `skip_track` | volatile uint8_t | UI | Audio | `playerMutex` |
| `track_direction` | volatile int8_t | UI | UI, Audio | `playerMutex` |
| `is_paused` | volatile uint8_t | UI | UI | `playerMutex` |
| `force_ui_update` | uint8_t | UI | UI | `playerMutex` |
| `pending_resume` | uint8_t | UI | UI | `playerMutex` |
| `window_start` | int | UI | UI | `playerMutex` |
| `dma_half_ready` | volatile uint8_t | ISR | Audio | — |
| `dma_full_ready` | volatile uint8_t | ISR | Audio | — |

---

## Діаграми потоків

### Синхронізація UI ↔ Audio

```
  defaultTask                          AudioTask
      │                                    │
      │ f_open("track.mp3")                │
      │ App_UI_RequestUpdate()             │
      │ App_UI_Update()                    │
      │   file_opened = 1                  │
      │   App_FileReady_Signal() ─────────►│ App_FileReady_Wait()
      │                                    │ MP3InitDecoder()
      │                                    │ f_read() + decode
      │                                    │ HAL_I2S_Transmit_DMA()
      │                                    │ ┌─ playback loop ─┐
      │                                    │ │ dmaSem → decode  │
      │  skip_track=1 (2 кліки)            │ └─────────────────┘
      │◄───────────────────────────────────│ App_Skip_Track()
      │                                    │   f_close()
      │                                    │   file_opened=0
      │ App_Open_Next_Song()               │
      │   f_open(next.mp3)                 │
      │ App_FileReady_Signal() ───────────►│ App_FileReady_Wait()
      │                                    │ (новий трек)
```

### Синхронізація DMA ↔ Audio

```
  DMA (апаратний)          ISR (колбек)           AudioTask
      │                        │                      │
      │ грає половину 1        │                      │
      │───────────────────────►│ TxHalfCplt           │
      │                        │ dma_half_ready=1     │
      │                        │ Release(dmaSem) ────►│ Acquire(dmaSem)
      │                        │                      │ decode → pcmBuf[0]
      │ грає половину 2        │                      │
      │───────────────────────►│ TxCplt               │
      │                        │ dma_full_ready=1     │
      │                        │ Release(dmaSem) ────►│ Acquire(dmaSem)
      │                        │                      │ decode → pcmBuf[1]
      │ (цикл повторюється)    │                      │
```

---

## Налагодження

### UART-лог

Підключіть UART1 (115200, 8N1) і переглядайте повідомлення `printf`:

```
--- SD Card & OLED Test (FreeRTOS) ---
Found song: track1.mp3
Found song: track2.mp3
Found 2 songs. Preparing UI and opening file...
File opened OK
Pre-loading buffers...
Bitrate: 128000, Hz: 44100, Chans: 2
Starting I2S DMA...
Paused
Resumed
Next Track
Skipping track...
End of file. Switching...
```

### Типові проблеми

| Симптом | Можлива причина |
|---------|-----------------|
| `Mount error` | SD-карта не підключена або пошкоджена |
| `No MP3 files found` | На карті немає `.mp3` у корені |
| `File open error` | Пошкоджений файл або проблема з FAT |
| `MP3 decoder init failed` | Нестача пам'яті (heap) |
| `DMA Start Failed` | Проблема з I2S/DMA конфігурацією |
| Немає звуку | Перевірити I2S, DAC, гучність (ADC) |
| Рваний звук | AudioTask не встигає декодувати (пріоритет/навантаження) |

---

## Обмеження та відомі особливості

1. **Максимум 20 треків** у плейлисті (`MAX_SONGS`).
2. **Сканується лише корінь** SD-карти (без підпапок).
3. **Одна кнопка** для всіх дій (мультіклік); `BTN_PREV` підключена в GPIO, але не використовується в коді.
4. **Пауза/resume** — DMA pause/resume, а не повна зупинка декодера.
5. **Гучність** — програмне масштабування PCM, не апаратне.
6. **I2S частота** — підлаштовується під трек; між треками з різною частотою виконується re-init I2S.
7. **FatFS reentrant** увімкнено (`_FS_REENTRANT = 1`), але додатковий `fatfsMutex` захищає спільний `FIL fil`.
8. **Кодування імен файлів** — лише ASCII; інші символи показуються як `?` на дисплеї.

---

## Збірка та прошивка

1. Відкрити проєкт у **STM32CubeIDE**.
2. **Project → Build Project** (Ctrl+B).
3. Підключити ST-Link, натиснути **Run** (F11).
4. Покласти `.mp3` файли в корінь SD-карти (FAT32).
5. Вставити SD-карту, увімкнути пристрій.

---

*Документ створено для проєкту mp3_player (STM32F401 + FreeRTOS).*
