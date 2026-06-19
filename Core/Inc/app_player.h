/**
 * @file app_player.h
 * @brief MP3 player core API: shared state, buffers, and task entry points.
 */

#ifndef APP_PLAYER_H
#define APP_PLAYER_H

#include <stdint.h>
#include "fatfs.h"
#include "cmsis_os.h"

#define MAX_SONGS           20
#define MAX_LINES           4
#define MULTICLICK_TIMEOUT  400
#define CMD_QUEUE_LEN       8


#define READ_BUF_SIZE  4096
#define PCM_HALF_BUF_SIZE 2304
#define PCM_BUF_SIZE (PCM_HALF_BUF_SIZE * 2)


/* Shared player state (protected by appMutex) */
extern char playlist[MAX_SONGS][80];
extern int total_songs;
extern int current_song_index;
extern volatile uint8_t file_opened;
extern volatile uint16_t adc_volume;
extern volatile uint8_t is_paused;
extern uint8_t force_ui_update;
extern int window_start;

extern FATFS fs;
extern FIL fil;
extern FRESULT fres;

extern uint8_t readBuf[READ_BUF_SIZE];
extern int16_t pcmBuf[PCM_BUF_SIZE];
extern volatile uint8_t dma_half_ready;
extern volatile uint8_t dma_full_ready;

void App_FS_Init(void);
FRESULT App_ScanFiles(char *path);
void App_Display_Init(void);
void App_UI_Update(void);
void App_Volume_Poll(void);
void App_Button_Poll(void);
void App_Open_Next_Song(void);
void App_DefaultTask_Init(void);
void App_DefaultTask_Loop(void);
void App_AudioTask_Run(void);

#endif /* APP_PLAYER_H */
