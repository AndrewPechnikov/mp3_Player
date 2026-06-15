#include "app_sync.h"
#include "app_player.h"

void App_Player_Lock(void)
{
	osMutexAcquire(playerMutexHandle, osWaitForever);
}

void App_Player_Unlock(void)
{
	osMutexRelease(playerMutexHandle);
}

void App_FatFs_Lock(void)
{
	osMutexAcquire(fatfsMutexHandle, osWaitForever);
}

void App_FatFs_Unlock(void)
{
	osMutexRelease(fatfsMutexHandle);
}

void App_UI_RequestUpdate(void)
{
	force_ui_update = 1;
	osSemaphoreRelease(uiSemHandle);
}

void App_FileReady_Signal(void)
{
	osSemaphoreRelease(fileReadySemHandle);
}

void App_FileReady_Wait(void)
{
	osSemaphoreAcquire(fileReadySemHandle, osWaitForever);
}
