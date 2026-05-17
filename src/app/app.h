/**
 * @file  app.h
 * @brief Application entry — call App_Init() once after MX_*_Init(),
 *        then App_Loop() in the while(1).
 */
#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

void App_Init(void);
void App_Loop(void);

#ifdef __cplusplus
}
#endif

#endif
