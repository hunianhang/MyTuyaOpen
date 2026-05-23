/**
 * @file app_alarm.h
 * @brief Alarm clock module for your_life_companion.
 *        Registers MCP tools for setting, modifying, and cancelling alarms
 *        via voice commands. Data is persisted to KV storage.
 *        A 60-second cycle timer checks for alarm triggers.
 * @version 0.1
 */

#ifndef __APP_ALARM_H__
#define __APP_ALARM_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
 ************************ macro define **********************
 ***********************************************************/
#define APP_ALARM_MAX_COUNT  10
#define APP_ALARM_LABEL_LEN  64
#define APP_ALARM_KV_KEY     "app_alarms"

/***********************************************************
 *********************** typedef define *********************
 ***********************************************************/
typedef struct {
    int  id;
    int  hour;              /* 0-23 */
    int  minute;            /* 0-59 */
    int  repeat_days;       /* 0=one-shot; bitmask bit0=Sun, bit1=Mon, ..., bit6=Sat */
    char label[APP_ALARM_LABEL_LEN];
    bool enabled;
    bool triggered_today;   /* prevent re-trigger within the same minute; not persisted */
} app_alarm_t;

typedef struct {
    int         count;
    int         next_id;
    app_alarm_t alarms[APP_ALARM_MAX_COUNT];
} app_alarm_list_t;

/***********************************************************
 ******************** function declaration ******************
 ***********************************************************/
/**
 * @brief Initialize alarm module, register MCP tools, and start the check timer.
 *        MCP registration is deferred until MQTT connects.
 * @return OPERATE_RET
 */
OPERATE_RET app_alarm_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_ALARM_H__ */
