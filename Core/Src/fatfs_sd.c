#include "fatfs_sd.h"
#include "main.h"
#include <stdio.h>
extern SPI_HandleTypeDef hspi1;

// Макроси для керування піном CS
#define SD_CS_LOW()     HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET)
#define SD_CS_HIGH()    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET)

// Команди SD-карти
#define CMD0    (0)         // GO_IDLE_STATE
#define CMD8    (8)         // SEND_IF_COND
#define CMD12   (12)        // STOP_TRANSMISSION
#define CMD17   (17)        // READ_SINGLE_BLOCK
#define CMD24   (24)        // WRITE_BLOCK
#define CMD55   (55)        // APP_CMD
#define ACMD41  (0x80 + 41) // SEND_OP_COND

static volatile DSTATUS Stat = STA_NOINIT;
static BYTE CardType;

// Передача/прийом 1 байта по SPI
static BYTE SPI_TxRx(BYTE data) {
    BYTE rx_data;
    HAL_SPI_TransmitReceive(&hspi1, &data, &rx_data, 1, HAL_MAX_DELAY);
    return rx_data;
}

// Відправка команди на карту
static BYTE SD_SendCmd(BYTE cmd, DWORD arg) {
    BYTE n, res;
    if (cmd & 0x80) {
        cmd &= 0x7F;
        res = SD_SendCmd(CMD55, 0);
        if (res > 1) return res;
    }
    SD_CS_LOW();
    SPI_TxRx(0xFF);
    SPI_TxRx(0x40 | cmd);
    SPI_TxRx((BYTE)(arg >> 24));
    SPI_TxRx((BYTE)(arg >> 16));
    SPI_TxRx((BYTE)(arg >> 8));
    SPI_TxRx((BYTE)arg);
    n = 0x01;
    if (cmd == CMD0) n = 0x95;
    if (cmd == CMD8) n = 0x87;
    SPI_TxRx(n);
    if (cmd == CMD12) SPI_TxRx(0xFF);
    n = 10;
    do { res = SPI_TxRx(0xFF); } while ((res & 0x80) && --n);
    return res;
}

DSTATUS SD_disk_initialize(BYTE pdrv) {
    BYTE n, cmd, ty, ocr[4];
    uint16_t tmr;
    if (pdrv) return STA_NOINIT;
    SD_CS_HIGH();
    for (n = 10; n; n--) SPI_TxRx(0xFF);
    ty = 0;

    BYTE res0 = SD_SendCmd(CMD0, 0);
    printf("CMD0: 0x%02X\r\n", res0);

    if (res0 == 1) {
        BYTE res8 = SD_SendCmd(CMD8, 0x1AA);
        printf("CMD8: 0x%02X\r\n", res8);

        if (res8 == 1) {
            for (n = 0; n < 4; n++) ocr[n] = SPI_TxRx(0xFF);
            printf("OCR: %02X %02X %02X %02X\r\n", ocr[0], ocr[1], ocr[2], ocr[3]);

            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                // Чекаємо завершення ініціалізації (ACMD41)
                for (tmr = 10000; tmr; tmr--) {
                    if (SD_SendCmd(ACMD41, 1UL << 30) == 0) break;
                    HAL_Delay(1);
                }
                printf("ACMD41 loops left: %d\r\n", tmr);

                if (tmr && SD_SendCmd(58, 0) == 0) { // CMD58
                    for (n = 0; n < 4; n++) ocr[n] = SPI_TxRx(0xFF);
                    printf("CMD58 OCR: %02X\r\n", ocr[0]);
                    ty = (ocr[0] & 0x40) ? 12 : 4; // Перевірка типу карти
                } else {
                    printf("CMD58 Failed or ACMD41 Timeout\r\n");
                }
            } else {
                printf("CMD8 OCR Bad Pattern\r\n");
            }
        } else {
            printf("CMD8 Rejected\r\n");
        }
    }

    CardType = ty;
    SD_CS_HIGH();
    SPI_TxRx(0xFF);
    Stat = ty ? 0 : STA_NOINIT;
    printf("Init End, Type: %d, Stat: %d\r\n", ty, Stat);
    return Stat;
}

DSTATUS SD_disk_status(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    return Stat;
}

DRESULT SD_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    // Якщо карта стара (SDSC), переводимо сектори в байти. Для вашої SDXC (Type 12) це ігнорується.
    if (!(CardType & 8)) sector *= 512;

    if (count == 1) {
        // Читання одного сектора (CMD17)
        if (SD_SendCmd(CMD17, sector) == 0) {
            uint16_t tmr;
            // Чекаємо токен даних (0xFE)
            for (tmr = 20000; tmr; tmr--) {
                if (SPI_TxRx(0xFF) == 0xFE) break;
            }
            if (tmr) {
                // Читаємо 512 байт
                for (uint16_t i = 0; i < 512; i++) buff[i] = SPI_TxRx(0xFF);
                SPI_TxRx(0xFF); SPI_TxRx(0xFF); // Пропускаємо 2 байти CRC
                SD_CS_HIGH(); SPI_TxRx(0xFF);
                return RES_OK;
            } else {
                printf("Read token timeout!\r\n");
            }
        }
    } else {
        // Читання кількох секторів (CMD18). FAT32 це дуже любить!
        if (SD_SendCmd(18, sector) == 0) {
            do {
                uint16_t tmr;
                for (tmr = 20000; tmr; tmr--) {
                    if (SPI_TxRx(0xFF) == 0xFE) break;
                }
                if (!tmr) break; // Timeout

                for (uint16_t i = 0; i < 512; i++) *buff++ = SPI_TxRx(0xFF);
                SPI_TxRx(0xFF); SPI_TxRx(0xFF); // Пропускаємо CRC

            } while (--count);

            SD_SendCmd(CMD12, 0); // CMD12 - STOP_TRANSMISSION
            SD_CS_HIGH(); SPI_TxRx(0xFF);
            return count ? RES_ERROR : RES_OK;
        }
    }

    SD_CS_HIGH();
    SPI_TxRx(0xFF);
    return RES_ERROR;
}

DRESULT SD_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
    return RES_OK; // Заглушка, ми поки тільки читаємо!
}

DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    DRESULT res = RES_ERROR;
    if (pdrv) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (cmd == CTRL_SYNC) {
        SD_CS_HIGH(); SPI_TxRx(0xFF); res = RES_OK;
    }
    return res;
}
