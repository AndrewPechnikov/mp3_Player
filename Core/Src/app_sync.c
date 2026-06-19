/**
 * @file app_sync.c
 * @brief Wrappers for appMutex and cmdQueue.
 */

#include "app_sync.h"
#include "app_player.h"

/** Lock appMutex. */
void App_Lock(void)
{
	osMutexAcquire(appMutexHandle, osWaitForever);
}

/** Unlock appMutex. */
void App_Unlock(void)
{
	osMutexRelease(appMutexHandle);
}

/** Post a player command to cmdQueue (non-blocking). */
void App_SendCmd(PlayerCmd_t cmd)
{
	(void)osMessageQueuePut(cmdQueueHandle, &cmd, 0, 0);
}

/** Wait for a player command from cmdQueue. */
osStatus_t App_WaitCmd(PlayerCmd_t *cmd, uint32_t timeout_ms)
{
	return osMessageQueueGet(cmdQueueHandle, cmd, NULL, timeout_ms);
}

/** Set force_ui_update under appMutex. */
void App_UI_RequestUpdate(void)
{
	App_Lock();
	force_ui_update = 1;
	App_Unlock();
}
