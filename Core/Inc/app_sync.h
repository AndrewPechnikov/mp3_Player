#ifndef APP_SYNC_H
#define APP_SYNC_H

#include "cmsis_os.h"

extern osMutexId_t playerMutexHandle;
extern osMutexId_t fatfsMutexHandle;
extern osSemaphoreId_t uiSemHandle;
extern osSemaphoreId_t fileReadySemHandle;

void App_Player_Lock(void);
void App_Player_Unlock(void);
void App_FatFs_Lock(void);
void App_FatFs_Unlock(void);

void App_UI_RequestUpdate(void);
void App_FileReady_Signal(void);
void App_FileReady_Wait(void);

#endif /* APP_SYNC_H */
