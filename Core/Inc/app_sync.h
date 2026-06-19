/**
 * @file app_sync.h
 * @brief Single mutex, command queue, and dmaSem (declared in main.c).
 *
 * Sync model (3 primitives only):
 *   appMutex  - protects shared player state and FatFS access
 *   cmdQueue  - UI task sends commands to Audio task
 *   dmaSem    - ISR notifies Audio that a PCM half-buffer is free
 */

#ifndef APP_SYNC_H
#define APP_SYNC_H

#include "cmsis_os.h"
#include <stdint.h>

extern osMutexId_t appMutexHandle;
extern osMessageQueueId_t cmdQueueHandle;

/** Commands posted by UI task, consumed by Audio task. */
typedef enum {
	PLAYER_CMD_START = 0,  /**< File is open; begin decode and DMA. */
	PLAYER_CMD_PAUSE,      /**< Pause I2S DMA. */
	PLAYER_CMD_RESUME,     /**< Resume I2S DMA. */
	PLAYER_CMD_NEXT,       /**< Skip to next track. */
	PLAYER_CMD_PREV,       /**< Skip to previous track. */
} PlayerCmd_t;

/** Lock appMutex (state + FatFS). */
void App_Lock(void);

/** Unlock appMutex. */
void App_Unlock(void);

/** Post a player command to cmdQueue (non-blocking). */
void App_SendCmd(PlayerCmd_t cmd);

/** Wait for a player command from cmdQueue. */
osStatus_t App_WaitCmd(PlayerCmd_t *cmd, uint32_t timeout_ms);

/** Set force_ui_update under appMutex. */
void App_UI_RequestUpdate(void);

#endif /* APP_SYNC_H */
