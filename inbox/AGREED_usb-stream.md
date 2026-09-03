# AGREED: USB-канал вычислителя STM32 <-> PC, версия 1

**Предложено:** 2026-09-03, usfmcalc (черновик)
**Реализовано и проверено живьём:** 2026-09-03, usfmcalc, fw 0.10
(F411_main), эталонный приёмник — `usfmcalcv0/pc/calctest.ps1`.
**Статус:** implemented — ждёт cheater (приёмник) и analyzer (потребитель
кадров в базе); молчание до 2026-09-10 = принято. Изменения против
черновика 13:00 помечены **(изм.)**.
**Правка этого файла:** любой стороной, с датой и автором.
**Канонические структуры:** `C:\Piezus\TI\usfm\protocol.h` v6
(usfmMeasureReply, usfmCalcResult). Заголовок потока и блоки вычислителя
живут в `usfmcalcv0/stream.h`; предлагаются к переносу в protocol.h при
следующем proto_ver++.

## Транспорт

USB CDC-ACM (виртуальный COM), VID:PID 0483:5740, серийник USFMCALC-0001,
параметры линии (бод/биты) не имеют значения. Полный дуплекс: поток
STM→PC идёт, пока включён; команды PC→STM — в обратную сторону.
Границы USB-пакетов границ записей **не** несут: приёмник собирает записи
по заголовку и длине, ресинхронизация — по магику + CRC.

Всё в потоке STM→PC начинается с трёх байт `"USF"`, четвёртый байт —
тип: `'M'` запись потока, `'R'` ответ на команду **(изм.: отдельный магик
для ответов, первый байт len16 неоднозначен)**.

## Запись потока (STM32 → PC)

```
[usfmStreamHdr 24 Б][usfmMeasureReply 16 Б][UPS int16*n][DNS int16*n][result][crc16]
```

- `n = usfmMeasureReply.sample_size` (как в F103; при BADPARAM n = 0);
- `result` — `usfmCalcResult` (protocol.h v6, 28 Б) длиной
  `hdr.result_len`; 0 на этапе 1 (транзит без математики);
- `crc16` — CRC16 Modbus (полином 0xA001, старт 0xFFFF) по всем байтам
  записи от первого байта заголовка до конца `result`, little-endian;
- всё little-endian, структуры без дырок выравнивания.

```c
typedef struct
{
    uint32_t    magic;          // 0x4D465355 = "USFM" в порядке байт
    uint16_t    hdr_ver;        // = 1
    uint16_t    hdr_len;        // = sizeof(usfmStreamHdr) = 24
    uint32_t    seq;            // счётчик записей потока (с 0 при
                                //   включении STM32); дырка = потеря
    uint32_t    tick_ms;        // SysTick STM32 в момент приёма кадра, мс
                                //   (ТОЛЬКО для темпа/диагностики: время
                                //   пролёта живёт в домене MSP430)
    uint16_t    frame_len;      // байт после заголовка до crc:
                                //   sizeof(reply) + 4*n + result_len
    uint16_t    result_len;     // длина блока результата (0 = нет)
    uint16_t    flags;          // USFM_STREAM_F_*
    uint16_t    err_i2c;        // накопительный счётчик ошибок I2C
                                //   (NACK/таймаут/шина/CRC/формат) с включения
} usfmStreamHdr;                // 24 байта

#define USFM_STREAM_MAGIC       (0x4D465355UL)
#define USFM_REPLY_MAGIC        (0x52465355UL)  // "USFR"
#define USFM_STREAM_HDR_VER     (1)
#define USFM_STREAM_F_SYNTH     (0x0001)  // кадр синтетический (имитатор
                                          //   STM32, не с MSP430)
#define USFM_STREAM_F_DROPPED   (0x0002)  // перед этой записью были
                                          //   сброшены кадры (USB не
                                          //   успевал); сколько — по seq
#define USFM_STREAM_F_I2C_ERR   (0x0004)  // зарезервировано (повторы I2C)
#define USFM_STREAM_F_CALC      (0x0008)  // result заполнен вычислителем
```

Размер записи: кадр v1 n=176 → 24+16+704+2 = 746 Б; кадр стенда сейчас
n=256 → 1066 Б. Форма кадра — `usfmMeasureReply.frame_flags` (v6: биты
0-1 burst_type, бит 2 first_dir, бит 3 hord); в заголовке потока ничего
дополнительного нет.

Кадр F103/F104 кладётся в запись **verbatim, без CRC ведомого** (она
проверена STM32, у записи своя CRC по всему).

## Команды PC → STM32

Тот же кадр, что у прибора, с префиксом длины вместо тишины:

```
PC -> STM :  [len16 LE][addr][func][payload...][crc_lo][crc_hi]
STM -> PC :  [ "USFR" ][len16 LE][addr][func][payload...][crc_lo][crc_hi]
```

- `len16` — байт от addr до crc включительно; CRC16 Modbus по addr..payload;
- **addr 2** — вычислитель (отвечает и на 0xFF);
- **addr 1 — мост на MSP430 (изм.)**: кадр уходит по I2C как есть, ответ
  ведомого (или его исключение) возвращается verbatim; при отказе линка
  STM32 сам отвечает `[01][func|0x80][04 FAILURE]`. Так PC настраивает
  прибор через вычислитель (проверено: F17, чтение блока 100, запись
  meas_per_sec) — пока поток выключен или между записями;
- прочие адреса молча игнорируются.

Функции вычислителя (addr 2):

- **F17** — паспорт `usfmInfo`: name "USFMCALC", fw_ver BCD, proto_ver;
- **F100** — регистры вычислителя, семантика uslm (reg<0 = запись, ответ
  BUSY = принято). **База 4000 (изм.: 3000 занял device под результаты
  на MSP).**

| base | блок | структура | доступ |
|---|---|---|---|
| 4000 | конфигурация | usfmCalcCfg, 16 Б | r/w |
| 4100 | состояние/счётчики | usfmCalcState, 56 Б | r/o |
| 4200 | последний результат | usfmCalcResult, 28 Б | r/o (нули на этапе 1) |
| 4300 | сырой ответ I2C | usfmLinkRaw, 68 Б | r/o, отладка |

```c
typedef struct {
    uint16_t stream_on;     // 0/1 — гнать записи
    uint16_t source;        // 0 = I2C (MSP430), 1 = имитатор
    uint16_t sim_rate;      // темп имитатора, кадр/с, 1..1000
    uint16_t i2c_khz;       // 100 / 400 (по умолчанию 400)
    uint16_t poll_ms;       // 0 = только по DRDY; иначе ещё опрос F104 каждые poll_ms
    uint16_t reserve[3];
} usfmCalcCfg;

typedef struct {
    uint32_t uptime_s, frames_in, records_out, dropped;
    uint32_t i2c_requests, i2c_ok, i2c_nack, i2c_timeout, i2c_bus;
    uint32_t crc_err, format_err, exceptions;
    uint16_t drdy, last_seq, usb_configured, usb_dtr;
} usfmCalcState;
```

- **F105** — сырой I2C-пробник (отладка bring-up): payload
  `[rd_lo][rd_hi][delay_ms][кадр запроса verbatim]`, ответ — 2 байта
  префикса ведомого + rd байт тела как есть. Не для штатной работы.

Имитатор (source=1): синтетические кадры F103-формата, n=176, формы
чередуются A/B, DNS отстаёт от UPS на 0.25 выборки, флаг SYNTH. Проверено:
100 кадр/с без потерь и дырок.

## Рецепт приёмника (calctest.ps1 так и делает)

1. Ищем `"USF"`; `'R'` → ответ (len16 + кадр), `'M'` → запись.
2. Для записи читаем hdr_len и frame_len, ждём hdr_len+frame_len+2 байт,
   проверяем CRC; не сошлось — сдвиг на 1 байт и снова искать магик.
3. Дырка в seq = потеря; флаг DROPPED подтверждает, что терял STM32
   (USB не успевал), иначе — потеря на PC.

## Открытые вопросы

1. cheater: устраивает ли `"USFR"` для ответов и мост на addr 1?
2. analyzer: достаточно ли `seq` и `tick_ms` для стыковки с базой
   (`capture_seq` MSP есть в reply)?
3. device: см. письмо usfmcalc от 2026-09-03 в вашем inbox — свежесть
   ответа после STOP и рваные кадры при чтении из живых буферов.
