/**
 * @file app_alarm.c
 * @brief Alarm clock MCP tools for your_life_companion.
 *        Provides voice-operable alarm management via three MCP tools:
 *          - alarm_set    : create a new alarm
 *          - alarm_modify : update an existing alarm
 *          - alarm_cancel : cancel an alarm by ID (or all)
 *        Data is persisted to KV storage as JSON.
 *        A 60-second cycle timer fires the alarm and sends a text prompt
 *        to the cloud AI, which then generates a TTS response.
 * @version 0.1
 */

#include <string.h>
#include <stdio.h>
#include "tal_api.h"
#include "tal_time_service.h"
#include "ai_mcp_server.h"
#include "ai_agent.h"
#include "ai_audio_player.h"
#include "app_alarm.h"

/***********************************************************
 ************************ macro define **********************
 ***********************************************************/
#define TAG "app_alarm"

/***********************************************************
 *********************** variable define ********************
 ***********************************************************/
static app_alarm_list_t sg_alarm_list  = {0};
static MUTEX_HANDLE     sg_alarm_mutex = NULL;
static TIMER_ID         sg_alarm_check_timer = NULL;

/***********************************************************
 *********************** function define ********************
 ***********************************************************/

/* ---- KV persistence ---- */

static void __alarm_save(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "next_id", sg_alarm_list.next_id);

    cJSON *arr = cJSON_AddArrayToObject(root, "alarms");
    for (int i = 0; i < sg_alarm_list.count; i++) {
        app_alarm_t *a = &sg_alarm_list.alarms[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id",      a->id);
        cJSON_AddNumberToObject(item, "hour",    a->hour);
        cJSON_AddNumberToObject(item, "minute",  a->minute);
        cJSON_AddNumberToObject(item, "repeat",  a->repeat_days);
        cJSON_AddStringToObject(item, "label",   a->label);
        cJSON_AddBoolToObject(item,   "enabled", a->enabled);
        cJSON_AddItemToArray(arr, item);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return;

    tal_kv_set(APP_ALARM_KV_KEY, (uint8_t *)json_str, strlen(json_str));
    tal_free(json_str);
}

static void __alarm_load(void)
{
    uint8_t *value  = NULL;
    size_t   length = 0;

    if (OPRT_OK != tal_kv_get(APP_ALARM_KV_KEY, &value, &length)) {
        return;
    }

    cJSON *root = cJSON_ParseWithLength((char *)value, length);
    tal_kv_free(value);
    if (!root) return;

    cJSON *next_id_node = cJSON_GetObjectItem(root, "next_id");
    if (cJSON_IsNumber(next_id_node)) {
        sg_alarm_list.next_id = (int)next_id_node->valuedouble;
    }

    cJSON *arr = cJSON_GetObjectItem(root, "alarms");
    int cnt = cJSON_GetArraySize(arr);
    sg_alarm_list.count = 0;

    for (int i = 0; i < cnt && sg_alarm_list.count < APP_ALARM_MAX_COUNT; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        app_alarm_t *a = &sg_alarm_list.alarms[sg_alarm_list.count];

        cJSON *id_node      = cJSON_GetObjectItem(item, "id");
        cJSON *hour_node    = cJSON_GetObjectItem(item, "hour");
        cJSON *minute_node  = cJSON_GetObjectItem(item, "minute");
        cJSON *repeat_node  = cJSON_GetObjectItem(item, "repeat");
        cJSON *label_node   = cJSON_GetObjectItem(item, "label");
        cJSON *enabled_node = cJSON_GetObjectItem(item, "enabled");

        if (!cJSON_IsNumber(id_node) ||
            !cJSON_IsNumber(hour_node) ||
            !cJSON_IsNumber(minute_node)) {
            continue;
        }

        a->id          = (int)id_node->valuedouble;
        a->hour        = (int)hour_node->valuedouble;
        a->minute      = (int)minute_node->valuedouble;
        a->repeat_days = cJSON_IsNumber(repeat_node) ? (int)repeat_node->valuedouble : 0;
        a->enabled     = cJSON_IsTrue(enabled_node);
        a->triggered_today = false;

        if (cJSON_IsString(label_node) && label_node->valuestring) {
            snprintf(a->label, APP_ALARM_LABEL_LEN, "%s", label_node->valuestring);
        } else {
            snprintf(a->label, APP_ALARM_LABEL_LEN, "Alarm");
        }

        sg_alarm_list.count++;
    }

    cJSON_Delete(root);
    PR_DEBUG("[%s] loaded %d alarms from KV", TAG, sg_alarm_list.count);
}

/* ---- helper: build a JSON object for one alarm ---- */
static cJSON *__alarm_to_json(const app_alarm_t *a)
{
    cJSON *item = cJSON_CreateObject();
    if (!item) return NULL;
    cJSON_AddNumberToObject(item, "id",          a->id);
    cJSON_AddNumberToObject(item, "hour",        a->hour);
    cJSON_AddNumberToObject(item, "minute",      a->minute);
    cJSON_AddNumberToObject(item, "repeat_days", a->repeat_days);
    cJSON_AddStringToObject(item, "label",       a->label);
    cJSON_AddBoolToObject(item,   "enabled",     a->enabled);
    return item;
}

/* ---- 60-second cycle timer: check and fire alarms ---- */
static void __alarm_check_timer_cb(TIMER_ID timer_id, void *arg)
{
    if (OPRT_OK != tal_time_check_time_sync()) {
        return;
    }

    POSIX_TM_S tm = {0};
    tal_time_get_local_time_custom(0, &tm);

    tal_mutex_lock(sg_alarm_mutex);

    bool need_save = false;

    for (int i = 0; i < sg_alarm_list.count; i++) {
        app_alarm_t *a = &sg_alarm_list.alarms[i];

        if (!a->enabled) continue;

        if (a->hour == tm.tm_hour && a->minute == tm.tm_min) {
            if (a->triggered_today) continue;

            /* Check repeat_days bitmask: bit0=Sun(0), bit1=Mon(1), ..., bit6=Sat(6) */
            if (a->repeat_days != 0) {
                int today_bit = 1 << tm.tm_wday;
                if (!(a->repeat_days & today_bit)) continue;
            }

            a->triggered_today = true;

            /* One-shot alarm: disable after triggering */
            if (a->repeat_days == 0) {
                a->enabled = false;
                need_save = true;
            }

            /* Capture data before unlocking */
            char label[APP_ALARM_LABEL_LEN];
            int  hour   = a->hour;
            int  minute = a->minute;
            snprintf(label, sizeof(label), "%s", a->label);

            tal_mutex_unlock(sg_alarm_mutex);

            /* Play local alert sound */
            ai_audio_player_alert(AI_AUDIO_ALERT_WAKEUP);

            /* Ask cloud AI to generate TTS announcement */
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "[System] Alarm '%s' is ringing at %02d:%02d. "
                     "Please announce this alarm to the user.",
                     label, hour, minute);
            ai_agent_send_text(msg);

            PR_DEBUG("[%s] alarm fired: '%s' at %02d:%02d", TAG, label, hour, minute);

            tal_mutex_lock(sg_alarm_mutex);
        } else {
            /* Clear triggered_today when the minute changes */
            if (a->triggered_today) {
                a->triggered_today = false;
            }
        }
    }

    if (need_save) {
        __alarm_save();
    }

    tal_mutex_unlock(sg_alarm_mutex);
}

/* ---- MCP tool: alarm_set ---- */
static OPERATE_RET __alarm_set(const MCP_PROPERTY_LIST_T *properties,
                                MCP_RETURN_VALUE_T *ret_val,
                                void *user_data)
{
    int hour = -1, minute = -1, repeat_days = 0;
    const char *label = "Alarm";

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "hour") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            hour = prop->default_val.int_val;
        } else if (strcmp(prop->name, "minute") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            minute = prop->default_val.int_val;
        } else if (strcmp(prop->name, "repeat_days") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            repeat_days = prop->default_val.int_val;
        } else if (strcmp(prop->name, "label") == 0 && prop->type == MCP_PROPERTY_TYPE_STRING &&
                   prop->default_val.str_val && prop->default_val.str_val[0] != '\0') {
            label = prop->default_val.str_val;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error",
            "Parameters 'hour' (0-23) and 'minute' (0-59) are required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    tal_mutex_lock(sg_alarm_mutex);

    if (sg_alarm_list.count >= APP_ALARM_MAX_COUNT) {
        tal_mutex_unlock(sg_alarm_mutex);
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error",
            "Alarm list is full. Please cancel some alarms first.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    app_alarm_t *a = &sg_alarm_list.alarms[sg_alarm_list.count];
    a->id          = ++sg_alarm_list.next_id;
    a->hour        = hour;
    a->minute      = minute;
    a->repeat_days = (repeat_days >= 0 && repeat_days <= 127) ? repeat_days : 0;
    a->enabled     = true;
    a->triggered_today = false;
    snprintf(a->label, APP_ALARM_LABEL_LEN, "%s", label);
    sg_alarm_list.count++;

    __alarm_save();
    cJSON *alarm_json = __alarm_to_json(a);
    tal_mutex_unlock(sg_alarm_mutex);

    cJSON_AddBoolToObject(json, "success", TRUE);
    if (alarm_json) cJSON_AddItemToObject(json, "alarm", alarm_json);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] set alarm id=%d at %02d:%02d label='%s'",
             TAG, sg_alarm_list.alarms[sg_alarm_list.count - 1].id, hour, minute, label);
    return OPRT_OK;
}

/* ---- MCP tool: alarm_modify ---- */
static OPERATE_RET __alarm_modify(const MCP_PROPERTY_LIST_T *properties,
                                   MCP_RETURN_VALUE_T *ret_val,
                                   void *user_data)
{
    int alarm_id    = -1;
    int new_hour    = -1;
    int new_minute  = -1;
    int new_repeat  = -1;
    int new_enabled = -1;   /* -1 = not specified */
    const char *new_label = NULL;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "alarm_id") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            alarm_id = prop->default_val.int_val;
        } else if (strcmp(prop->name, "hour") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            new_hour = prop->default_val.int_val;
        } else if (strcmp(prop->name, "minute") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            new_minute = prop->default_val.int_val;
        } else if (strcmp(prop->name, "repeat_days") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            new_repeat = prop->default_val.int_val;
        } else if (strcmp(prop->name, "label") == 0 && prop->type == MCP_PROPERTY_TYPE_STRING) {
            new_label = prop->default_val.str_val;
        } else if (strcmp(prop->name, "enabled") == 0 && prop->type == MCP_PROPERTY_TYPE_BOOLEAN) {
            new_enabled = prop->default_val.bool_val ? 1 : 0;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    if (alarm_id <= 0) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'alarm_id' is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    tal_mutex_lock(sg_alarm_mutex);

    int found = -1;
    for (int i = 0; i < sg_alarm_list.count; i++) {
        if (sg_alarm_list.alarms[i].id == alarm_id) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        tal_mutex_unlock(sg_alarm_mutex);
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Alarm not found.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    app_alarm_t *a = &sg_alarm_list.alarms[found];

    if (new_hour >= 0 && new_hour <= 23)     a->hour        = new_hour;
    if (new_minute >= 0 && new_minute <= 59) a->minute      = new_minute;
    if (new_repeat >= 0 && new_repeat <= 127) a->repeat_days = new_repeat;
    if (new_enabled >= 0)                    a->enabled     = (new_enabled == 1);
    if (new_label && new_label[0] != '\0')
        snprintf(a->label, APP_ALARM_LABEL_LEN, "%s", new_label);

    __alarm_save();
    cJSON *alarm_json = __alarm_to_json(a);
    tal_mutex_unlock(sg_alarm_mutex);

    cJSON_AddBoolToObject(json, "success", TRUE);
    if (alarm_json) cJSON_AddItemToObject(json, "alarm", alarm_json);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] modified alarm id=%d", TAG, alarm_id);
    return OPRT_OK;
}

/* ---- MCP tool: alarm_cancel ---- */
static OPERATE_RET __alarm_cancel(const MCP_PROPERTY_LIST_T *properties,
                                   MCP_RETURN_VALUE_T *ret_val,
                                   void *user_data)
{
    int alarm_id = -1;
    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "alarm_id") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            alarm_id = prop->default_val.int_val;
            break;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    if (alarm_id < 0) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'alarm_id' is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    tal_mutex_lock(sg_alarm_mutex);

    if (alarm_id == 0) {
        /* Cancel all alarms */
        int deleted = sg_alarm_list.count;
        sg_alarm_list.count = 0;
        __alarm_save();
        tal_mutex_unlock(sg_alarm_mutex);

        cJSON_AddBoolToObject(json, "success", TRUE);
        cJSON_AddNumberToObject(json, "cancelled_count", deleted);
        PR_DEBUG("[%s] cancelled all %d alarms", TAG, deleted);
    } else {
        int found = -1;
        for (int i = 0; i < sg_alarm_list.count; i++) {
            if (sg_alarm_list.alarms[i].id == alarm_id) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            tal_mutex_unlock(sg_alarm_mutex);
            cJSON_AddBoolToObject(json, "success", FALSE);
            cJSON_AddStringToObject(json, "error", "Alarm not found.");
            ai_mcp_return_value_set_json(ret_val, json);
            return OPRT_OK;
        }
        for (int i = found; i < sg_alarm_list.count - 1; i++) {
            sg_alarm_list.alarms[i] = sg_alarm_list.alarms[i + 1];
        }
        sg_alarm_list.count--;
        __alarm_save();
        tal_mutex_unlock(sg_alarm_mutex);

        cJSON_AddBoolToObject(json, "success", TRUE);
        cJSON_AddNumberToObject(json, "cancelled_id", alarm_id);
        PR_DEBUG("[%s] cancelled alarm id=%d", TAG, alarm_id);
    }

    ai_mcp_return_value_set_json(ret_val, json);
    return OPRT_OK;
}

/* ---- MQTT-connected event callback: register tools ---- */
static OPERATE_RET __alarm_mcp_register(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "alarm_set",
        "Set a new alarm on the device.\n"
        "Parameters:\n"
        "- hour (int, 0-23): Hour of the alarm.\n"
        "- minute (int, 0-59): Minute of the alarm.\n"
        "- label (string, optional): Description label, default 'Alarm'.\n"
        "- repeat_days (int, 0-127, optional): Bitmask for repeat days: "
        "bit0=Sun, bit1=Mon, bit2=Tue, bit3=Wed, bit4=Thu, bit5=Fri, bit6=Sat. "
        "0 means one-shot (default).\n"
        "Response: the created alarm object with its ID.",
        __alarm_set,
        NULL,
        MCP_PROP_INT_RANGE("hour",        "Hour of the alarm (0-23).",   0, 23),
        MCP_PROP_INT_RANGE("minute",      "Minute of the alarm (0-59).", 0, 59),
        MCP_PROP_STR_DEF("label",         "Alarm label.", "Alarm"),
        MCP_PROP_INT_DEF_RANGE("repeat_days", "Repeat bitmask (0=one-shot).", 0, 0, 127)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "alarm_modify",
        "Modify an existing alarm by its ID.\n"
        "Parameters:\n"
        "- alarm_id (int): ID of the alarm to modify.\n"
        "- hour (int, -1=keep): New hour, or -1 to keep current.\n"
        "- minute (int, -1=keep): New minute, or -1 to keep current.\n"
        "- label (string, ''=keep): New label, or empty to keep current.\n"
        "- repeat_days (int, -1=keep): New repeat bitmask, or -1 to keep current.\n"
        "- enabled (bool): Whether the alarm is active.\n"
        "Response: updated alarm object or error.",
        __alarm_modify,
        NULL,
        MCP_PROP_INT("alarm_id",              "ID of the alarm to modify."),
        MCP_PROP_INT_DEF("hour",              "New hour (0-23), or -1 to keep.", -1),
        MCP_PROP_INT_DEF("minute",            "New minute (0-59), or -1 to keep.", -1),
        MCP_PROP_STR_DEF("label",             "New label, or empty to keep.", ""),
        MCP_PROP_INT_DEF("repeat_days",       "New repeat bitmask, or -1 to keep.", -1),
        MCP_PROP_BOOL_DEF("enabled",          "Enable or disable the alarm.", TRUE)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "alarm_cancel",
        "Cancel an alarm by its ID.\n"
        "Parameters:\n"
        "- alarm_id (int): ID of the alarm to cancel. Use 0 to cancel all alarms.\n"
        "Response: success flag and cancelled ID or count.",
        __alarm_cancel,
        NULL,
        MCP_PROP_INT("alarm_id", "ID of the alarm to cancel. Use 0 to cancel all alarms.")
    ), err);

    PR_DEBUG("[%s] MCP tools registered: alarm_set, alarm_modify, alarm_cancel", TAG);
    return OPRT_OK;

err:
    PR_ERR("[%s] failed to register MCP tools, rt:%d", TAG, rt);
    return rt;
}

OPERATE_RET app_alarm_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&sg_alarm_mutex));

    memset(&sg_alarm_list, 0, sizeof(sg_alarm_list));
    sg_alarm_list.next_id = 0;

    __alarm_load();

    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(__alarm_check_timer_cb, NULL, &sg_alarm_check_timer));
    TUYA_CALL_ERR_RETURN(tal_sw_timer_start(sg_alarm_check_timer, 60 * 1000, TAL_TIMER_CYCLE));

    TUYA_CALL_ERR_RETURN(tal_event_subscribe(EVENT_MQTT_CONNECTED, "app_alarm_init",
                                              __alarm_mcp_register, SUBSCRIBE_TYPE_ONETIME));
    return rt;
}
