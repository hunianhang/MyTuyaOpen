/**
 * @file app_medicine.c
 * @brief Medicine reminder MCP tools for your_life_companion.
 *
 *  MCP tools registered:
 *    medicine_add     - add a medicine plan via voice
 *    medicine_query   - query all active medicine plans
 *    medicine_delete  - delete a medicine plan by ID
 *    medicine_confirm - patient confirms they have taken the medicine
 *
 *  Behaviour:
 *    - A 60-second timer checks whether any dose time has been reached.
 *    - If the patient has not confirmed, it keeps triggering an alert
 *      (voice + ai_agent_send_text) every 60 seconds until confirmed.
 *    - When the patient calls medicine_confirm, the camera captures one
 *      JPEG frame and saves it to APP_MEDICINE_PHOTO_DIR with a timestamp
 *      filename, e.g. "medicine_20260523_143005.jpg".
 *    - Data is persisted to KV storage as JSON.
 * @version 0.1
 */

#include <string.h>
#include <stdio.h>
#include "tal_api.h"
#include "tal_time_service.h"
#include "tal_fs.h"
#include "ai_mcp_server.h"
#include "ai_agent.h"
#include "ai_audio_player.h"
#include "app_medicine.h"

#if defined(ENABLE_COMP_AI_VIDEO) && (ENABLE_COMP_AI_VIDEO == 1)
#include "ai_video_input.h"
#endif

/***********************************************************
 ************************ macro define **********************
 ***********************************************************/
#define TAG "app_medicine"

/* Hour definitions for each time slot */
#define MEDICINE_MORNING_HOUR   8
#define MEDICINE_MORNING_MIN    0
#define MEDICINE_NOON_HOUR      12
#define MEDICINE_NOON_MIN       0
#define MEDICINE_EVENING_HOUR   18
#define MEDICINE_EVENING_MIN    0
#define MEDICINE_BEDTIME_HOUR   21
#define MEDICINE_BEDTIME_MIN    0

/***********************************************************
 *********************** variable define ********************
 ***********************************************************/
static app_medicine_list_t sg_medicine_list  = {0};
static MUTEX_HANDLE        sg_medicine_mutex = NULL;
static TIMER_ID            sg_medicine_check_timer = NULL;

/***********************************************************
 ********************* helper functions ********************
 ***********************************************************/

static const char *__slot_name(int slot)
{
    switch (slot) {
    case MEDICINE_TIME_MORNING: return "morning";
    case MEDICINE_TIME_NOON:    return "noon";
    case MEDICINE_TIME_EVENING: return "evening";
    case MEDICINE_TIME_BEDTIME: return "bedtime";
    default:                    return "unknown";
    }
}

static void __default_hour_min(int slot, int *hour, int *minute)
{
    switch (slot) {
    case MEDICINE_TIME_MORNING:
        *hour = MEDICINE_MORNING_HOUR; *minute = MEDICINE_MORNING_MIN; break;
    case MEDICINE_TIME_NOON:
        *hour = MEDICINE_NOON_HOUR;    *minute = MEDICINE_NOON_MIN;    break;
    case MEDICINE_TIME_EVENING:
        *hour = MEDICINE_EVENING_HOUR; *minute = MEDICINE_EVENING_MIN; break;
    case MEDICINE_TIME_BEDTIME:
        *hour = MEDICINE_BEDTIME_HOUR; *minute = MEDICINE_BEDTIME_MIN; break;
    default:
        *hour = 8; *minute = 0; break;
    }
}

/* ---- KV persistence ---- */

static void __medicine_save(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "next_id", sg_medicine_list.next_id);

    cJSON *arr = cJSON_AddArrayToObject(root, "plans");
    for (int i = 0; i < sg_medicine_list.count; i++) {
        app_medicine_plan_t *p = &sg_medicine_list.plans[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id",             p->id);
        cJSON_AddStringToObject(item, "name",           p->name);
        cJSON_AddNumberToObject(item, "pills_per_dose", p->pills_per_dose);
        cJSON_AddNumberToObject(item, "doses_per_day",  p->doses_per_day);
        cJSON_AddNumberToObject(item, "total_days",     p->total_days);
        cJSON_AddNumberToObject(item, "taken_days",     p->taken_days);
        cJSON_AddBoolToObject(item,   "active",         p->active);

        cJSON *times_arr = cJSON_CreateArray();
        for (int j = 0; j < p->doses_per_day && j < APP_MEDICINE_MAX_TIMES; j++) {
            cJSON *t = cJSON_CreateObject();
            cJSON_AddNumberToObject(t, "slot",   p->times[j].slot);
            cJSON_AddNumberToObject(t, "hour",   p->times[j].hour);
            cJSON_AddNumberToObject(t, "minute", p->times[j].minute);
            cJSON_AddItemToArray(times_arr, t);
        }
        cJSON_AddItemToObject(item, "times", times_arr);
        cJSON_AddItemToArray(arr, item);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return;

    tal_kv_set(APP_MEDICINE_KV_KEY, (uint8_t *)json_str, strlen(json_str));
    tal_free(json_str);
}

static void __medicine_load(void)
{
    uint8_t *value  = NULL;
    size_t   length = 0;

    if (OPRT_OK != tal_kv_get(APP_MEDICINE_KV_KEY, &value, &length)) {
        return;
    }

    cJSON *root = cJSON_ParseWithLength((char *)value, length);
    tal_kv_free(value);
    if (!root) return;

    cJSON *next_id_node = cJSON_GetObjectItem(root, "next_id");
    if (cJSON_IsNumber(next_id_node)) {
        sg_medicine_list.next_id = (int)next_id_node->valuedouble;
    }

    cJSON *arr = cJSON_GetObjectItem(root, "plans");
    int cnt = cJSON_GetArraySize(arr);
    sg_medicine_list.count = 0;

    for (int i = 0; i < cnt && sg_medicine_list.count < APP_MEDICINE_MAX_COUNT; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        app_medicine_plan_t *p = &sg_medicine_list.plans[sg_medicine_list.count];

        cJSON *id_node    = cJSON_GetObjectItem(item, "id");
        cJSON *name_node  = cJSON_GetObjectItem(item, "name");
        cJSON *ppd_node   = cJSON_GetObjectItem(item, "pills_per_dose");
        cJSON *dpd_node   = cJSON_GetObjectItem(item, "doses_per_day");
        cJSON *td_node    = cJSON_GetObjectItem(item, "total_days");
        cJSON *taken_node = cJSON_GetObjectItem(item, "taken_days");
        cJSON *active_node= cJSON_GetObjectItem(item, "active");

        if (!cJSON_IsNumber(id_node) || !cJSON_IsString(name_node)) continue;

        p->id             = (int)id_node->valuedouble;
        p->pills_per_dose = cJSON_IsNumber(ppd_node)   ? (int)ppd_node->valuedouble   : 1;
        p->doses_per_day  = cJSON_IsNumber(dpd_node)   ? (int)dpd_node->valuedouble   : 1;
        p->total_days     = cJSON_IsNumber(td_node)    ? (int)td_node->valuedouble     : 1;
        p->taken_days     = cJSON_IsNumber(taken_node) ? (int)taken_node->valuedouble  : 0;
        p->active         = cJSON_IsTrue(active_node);
        snprintf(p->name, APP_MEDICINE_NAME_LEN, "%s", name_node->valuestring);

        cJSON *times_arr = cJSON_GetObjectItem(item, "times");
        int tc = cJSON_GetArraySize(times_arr);
        if (tc > APP_MEDICINE_MAX_TIMES) tc = APP_MEDICINE_MAX_TIMES;
        for (int j = 0; j < tc; j++) {
            cJSON *t = cJSON_GetArrayItem(times_arr, j);
            p->times[j].slot   = cJSON_IsNumber(cJSON_GetObjectItem(t, "slot"))   ? (int)cJSON_GetObjectItem(t, "slot")->valuedouble   : j;
            p->times[j].hour   = cJSON_IsNumber(cJSON_GetObjectItem(t, "hour"))   ? (int)cJSON_GetObjectItem(t, "hour")->valuedouble   : 8;
            p->times[j].minute = cJSON_IsNumber(cJSON_GetObjectItem(t, "minute")) ? (int)cJSON_GetObjectItem(t, "minute")->valuedouble : 0;
            p->times[j].confirmed = false;
            p->times[j].triggered = false;
        }

        sg_medicine_list.count++;
    }

    cJSON_Delete(root);
    PR_DEBUG("[%s] loaded %d medicine plans from KV", TAG, sg_medicine_list.count);
}

/* ---- capture and save a JPEG photo ---- */
static void __take_medicine_photo(const char *medicine_name)
{
#if defined(ENABLE_COMP_AI_VIDEO) && (ENABLE_COMP_AI_VIDEO == 1)
    OPERATE_RET rt = OPRT_OK;

    uint8_t  *jpeg_data = NULL;
    uint32_t  jpeg_len  = 0;

    rt = ai_video_get_jpeg_frame(&jpeg_data, &jpeg_len);
    if (rt != OPRT_OK || !jpeg_data || jpeg_len == 0) {
        PR_ERR("[%s] failed to capture JPEG frame, rt:%d", TAG, rt);
        return;
    }

    /* Build timestamp filename */
    POSIX_TM_S local_tm = {0};
    tal_time_get_local_time_custom(0, &local_tm);

    char path[128];
    snprintf(path, sizeof(path), "%s/medicine_%04d%02d%02d_%02d%02d%02d.jpg",
             APP_MEDICINE_PHOTO_DIR,
             local_tm.tm_year + 1900,
             local_tm.tm_mon  + 1,
             local_tm.tm_mday,
             local_tm.tm_hour,
             local_tm.tm_min,
             local_tm.tm_sec);

    /* Ensure directory exists */
    BOOL_T dir_exists = FALSE;
    tal_fs_is_exist(APP_MEDICINE_PHOTO_DIR, &dir_exists);
    if (!dir_exists) {
        tal_fs_mkdir(APP_MEDICINE_PHOTO_DIR);
    }

    /* Write JPEG to file */
    TUYA_FILE fp = tal_fopen(path, "wb");
    if (fp) {
        tal_fwrite(jpeg_data, (int)jpeg_len, fp);
        tal_fsync(fp);
        tal_fclose(fp);
        PR_NOTICE("[%s] medicine photo saved: %s (size=%u)", TAG, path, jpeg_len);
    } else {
        PR_ERR("[%s] failed to open file for writing: %s", TAG, path);
    }

    ai_video_jpeg_image_free(&jpeg_data);
#else
    PR_DEBUG("[%s] camera not available, skip photo", TAG);
#endif
}

/* ---- 60-second cycle timer: check reminder times ---- */
static void __medicine_check_timer_cb(TIMER_ID timer_id, void *arg)
{
    if (OPRT_OK != tal_time_check_time_sync()) {
        return;
    }

    POSIX_TM_S tm = {0};
    tal_time_get_local_time_custom(0, &tm);

    tal_mutex_lock(sg_medicine_mutex);

    bool need_save = false;

    for (int i = 0; i < sg_medicine_list.count; i++) {
        app_medicine_plan_t *p = &sg_medicine_list.plans[i];

        if (!p->active) continue;
        if (p->taken_days >= p->total_days) {
            /* Plan completed */
            p->active = false;
            need_save = true;
            continue;
        }

        for (int j = 0; j < p->doses_per_day && j < APP_MEDICINE_MAX_TIMES; j++) {
            app_medicine_dose_time_t *dt = &p->times[j];

            if (dt->hour == tm.tm_hour && dt->minute == tm.tm_min) {
                /* Reminder window: alert until confirmed */
                if (!dt->confirmed) {
                    dt->triggered = true;

                    /* capture data before unlocking */
                    char name[APP_MEDICINE_NAME_LEN];
                    int  pills = p->pills_per_dose;
                    snprintf(name, sizeof(name), "%s", p->name);

                    tal_mutex_unlock(sg_medicine_mutex);

                    /* Play local alert */
                    ai_audio_player_alert(AI_AUDIO_ALERT_WAKEUP);

                    /* Ask cloud AI to announce medication reminder */
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "[System] It is time for the %s dose of medicine '%s'. "
                             "Please take %d pill(s). "
                             "Remind the patient urgently to take this medicine now. "
                             "Keep repeating the reminder until the patient confirms.",
                             __slot_name(dt->slot), name, pills);
                    ai_agent_send_text(msg);

                    PR_DEBUG("[%s] reminder: '%s' %s dose, %d pill(s)",
                             TAG, name, __slot_name(dt->slot), pills);

                    tal_mutex_lock(sg_medicine_mutex);
                }
            } else {
                /* Outside the reminder minute: if triggered but not confirmed, alert again */
                if (dt->triggered && !dt->confirmed) {
                    if (tm.tm_min % 2 == 0) { /* alert every 2 minutes if still not confirmed */
                        char name[APP_MEDICINE_NAME_LEN];
                        int  pills = p->pills_per_dose;
                        snprintf(name, sizeof(name), "%s", p->name);

                        tal_mutex_unlock(sg_medicine_mutex);

                        ai_audio_player_alert(AI_AUDIO_ALERT_WAKEUP);

                        char msg[256];
                        snprintf(msg, sizeof(msg),
                                 "[System] URGENT: The patient has NOT confirmed taking medicine '%s' (%s dose, %d pill(s)). "
                                 "Please alert the patient again immediately.",
                                 name, __slot_name(dt->slot), pills);
                        ai_agent_send_text(msg);

                        tal_mutex_lock(sg_medicine_mutex);
                    }
                }

                /* Reset confirmed/triggered flags at start of new day */
                if (tm.tm_hour == 0 && tm.tm_min == 0) {
                    dt->confirmed = false;
                    dt->triggered = false;
                }
            }
        }
    }

    if (need_save) {
        __medicine_save();
    }

    tal_mutex_unlock(sg_medicine_mutex);
}

/* ---- MCP tool helper: build JSON for one plan ---- */
static cJSON *__plan_to_json(const app_medicine_plan_t *p)
{
    cJSON *item = cJSON_CreateObject();
    if (!item) return NULL;
    cJSON_AddNumberToObject(item, "id",             p->id);
    cJSON_AddStringToObject(item, "name",           p->name);
    cJSON_AddNumberToObject(item, "pills_per_dose", p->pills_per_dose);
    cJSON_AddNumberToObject(item, "doses_per_day",  p->doses_per_day);
    cJSON_AddNumberToObject(item, "total_days",     p->total_days);
    cJSON_AddNumberToObject(item, "taken_days",     p->taken_days);
    cJSON_AddBoolToObject(item,   "active",         p->active);

    cJSON *times_arr = cJSON_CreateArray();
    for (int j = 0; j < p->doses_per_day && j < APP_MEDICINE_MAX_TIMES; j++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "slot",   __slot_name(p->times[j].slot));
        cJSON_AddNumberToObject(t, "hour",   p->times[j].hour);
        cJSON_AddNumberToObject(t, "minute", p->times[j].minute);
        cJSON_AddBoolToObject(t,   "confirmed", p->times[j].confirmed);
        cJSON_AddItemToArray(times_arr, t);
    }
    cJSON_AddItemToObject(item, "dose_times", times_arr);
    return item;
}

/* ---- MCP tool: medicine_add ---- */
static OPERATE_RET __medicine_add(const MCP_PROPERTY_LIST_T *properties,
                                   MCP_RETURN_VALUE_T *ret_val,
                                   void *user_data)
{
    const char *name        = NULL;
    int  pills_per_dose     = 1;
    int  doses_per_day      = 1;
    int  total_days         = 1;
    /* time slots: bitmask bit0=morning, bit1=noon, bit2=evening, bit3=bedtime */
    int  time_slots         = 0;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "name") == 0 && prop->type == MCP_PROPERTY_TYPE_STRING) {
            name = prop->default_val.str_val;
        } else if (strcmp(prop->name, "pills_per_dose") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            pills_per_dose = prop->default_val.int_val;
        } else if (strcmp(prop->name, "doses_per_day") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            doses_per_day = prop->default_val.int_val;
        } else if (strcmp(prop->name, "total_days") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            total_days = prop->default_val.int_val;
        } else if (strcmp(prop->name, "time_slots") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            time_slots = prop->default_val.int_val;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    if (!name || name[0] == '\0') {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'name' is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    if (doses_per_day < 1 || doses_per_day > APP_MEDICINE_MAX_TIMES) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'doses_per_day' must be 1-4.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    tal_mutex_lock(sg_medicine_mutex);

    if (sg_medicine_list.count >= APP_MEDICINE_MAX_COUNT) {
        tal_mutex_unlock(sg_medicine_mutex);
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Medicine list is full. Please delete some plans first.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    app_medicine_plan_t *p = &sg_medicine_list.plans[sg_medicine_list.count];
    memset(p, 0, sizeof(*p));
    p->id             = ++sg_medicine_list.next_id;
    p->pills_per_dose = (pills_per_dose > 0) ? pills_per_dose : 1;
    p->doses_per_day  = doses_per_day;
    p->total_days     = (total_days > 0) ? total_days : 1;
    p->taken_days     = 0;
    p->active         = true;
    snprintf(p->name, APP_MEDICINE_NAME_LEN, "%s", name);

    /* Assign dose times based on time_slots bitmask.
     * If time_slots == 0, distribute automatically across the day. */
    int slot_order[APP_MEDICINE_MAX_TIMES] = {
        MEDICINE_TIME_MORNING,
        MEDICINE_TIME_NOON,
        MEDICINE_TIME_EVENING,
        MEDICINE_TIME_BEDTIME
    };

    int assigned = 0;
    if (time_slots != 0) {
        for (int s = 0; s < APP_MEDICINE_MAX_TIMES && assigned < doses_per_day; s++) {
            if (time_slots & (1 << slot_order[s])) {
                p->times[assigned].slot = slot_order[s];
                __default_hour_min(slot_order[s],
                                   &p->times[assigned].hour,
                                   &p->times[assigned].minute);
                p->times[assigned].confirmed = false;
                p->times[assigned].triggered = false;
                assigned++;
            }
        }
    }
    /* Fill remaining with defaults if slots not fully covered */
    for (int s = 0; assigned < doses_per_day && s < APP_MEDICINE_MAX_TIMES; s++) {
        bool already = false;
        for (int k = 0; k < assigned; k++) {
            if (p->times[k].slot == slot_order[s]) { already = true; break; }
        }
        if (!already) {
            p->times[assigned].slot = slot_order[s];
            __default_hour_min(slot_order[s],
                               &p->times[assigned].hour,
                               &p->times[assigned].minute);
            p->times[assigned].confirmed = false;
            p->times[assigned].triggered = false;
            assigned++;
        }
    }

    sg_medicine_list.count++;
    __medicine_save();
    cJSON *plan_json = __plan_to_json(p);
    tal_mutex_unlock(sg_medicine_mutex);

    cJSON_AddBoolToObject(json, "success", TRUE);
    if (plan_json) cJSON_AddItemToObject(json, "plan", plan_json);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] added medicine plan id=%d name='%s'", TAG, p->id, name);
    return OPRT_OK;
}

/* ---- MCP tool: medicine_query ---- */
static OPERATE_RET __medicine_query(const MCP_PROPERTY_LIST_T *properties,
                                     MCP_RETURN_VALUE_T *ret_val,
                                     void *user_data)
{
    int active_only = 1;
    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "active_only") == 0 && prop->type == MCP_PROPERTY_TYPE_BOOLEAN) {
            active_only = prop->default_val.bool_val ? 1 : 0;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    cJSON *arr = cJSON_CreateArray();
    if (!arr) { cJSON_Delete(json); return OPRT_MALLOC_FAILED; }

    tal_mutex_lock(sg_medicine_mutex);
    int matched = 0;
    for (int i = 0; i < sg_medicine_list.count; i++) {
        app_medicine_plan_t *p = &sg_medicine_list.plans[i];
        if (active_only && !p->active) continue;
        cJSON *item = __plan_to_json(p);
        if (item) cJSON_AddItemToArray(arr, item);
        matched++;
    }
    tal_mutex_unlock(sg_medicine_mutex);

    cJSON_AddNumberToObject(json, "count", matched);
    cJSON_AddItemToObject(json, "plans", arr);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] query medicine plans, matched=%d", TAG, matched);
    return OPRT_OK;
}

/* ---- MCP tool: medicine_delete ---- */
static OPERATE_RET __medicine_delete(const MCP_PROPERTY_LIST_T *properties,
                                      MCP_RETURN_VALUE_T *ret_val,
                                      void *user_data)
{
    int plan_id = -1;
    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "plan_id") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            plan_id = prop->default_val.int_val;
            break;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    if (plan_id <= 0) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'plan_id' (>0) is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    tal_mutex_lock(sg_medicine_mutex);

    int found = -1;
    for (int i = 0; i < sg_medicine_list.count; i++) {
        if (sg_medicine_list.plans[i].id == plan_id) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        tal_mutex_unlock(sg_medicine_mutex);
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Medicine plan not found.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    for (int i = found; i < sg_medicine_list.count - 1; i++) {
        sg_medicine_list.plans[i] = sg_medicine_list.plans[i + 1];
    }
    sg_medicine_list.count--;
    __medicine_save();
    tal_mutex_unlock(sg_medicine_mutex);

    cJSON_AddBoolToObject(json, "success", TRUE);
    cJSON_AddNumberToObject(json, "deleted_id", plan_id);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] deleted medicine plan id=%d", TAG, plan_id);
    return OPRT_OK;
}

/* ---- MCP tool: medicine_confirm ---- */
static OPERATE_RET __medicine_confirm(const MCP_PROPERTY_LIST_T *properties,
                                       MCP_RETURN_VALUE_T *ret_val,
                                       void *user_data)
{
    int plan_id   = -1;
    int slot_val  = -1;  /* -1 = confirm all triggered slots for this plan */

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "plan_id") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            plan_id = prop->default_val.int_val;
        } else if (strcmp(prop->name, "slot") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            slot_val = prop->default_val.int_val;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    if (plan_id <= 0) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'plan_id' (>0) is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    tal_mutex_lock(sg_medicine_mutex);

    int found = -1;
    for (int i = 0; i < sg_medicine_list.count; i++) {
        if (sg_medicine_list.plans[i].id == plan_id) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        tal_mutex_unlock(sg_medicine_mutex);
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Medicine plan not found.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    app_medicine_plan_t *p = &sg_medicine_list.plans[found];
    int confirmed_count = 0;
    bool all_doses_done = true;

    for (int j = 0; j < p->doses_per_day && j < APP_MEDICINE_MAX_TIMES; j++) {
        app_medicine_dose_time_t *dt = &p->times[j];
        bool match = (slot_val < 0) ? dt->triggered : (dt->slot == slot_val);
        if (match && !dt->confirmed) {
            dt->confirmed = true;
            confirmed_count++;
        }
        if (!dt->confirmed) {
            all_doses_done = false;
        }
    }

    /* If all doses for today are confirmed, increment taken_days */
    if (all_doses_done) {
        p->taken_days++;
        if (p->taken_days >= p->total_days) {
            p->active = false;
        }
    }

    __medicine_save();

    /* Capture medicine name for photo, then release lock */
    char name[APP_MEDICINE_NAME_LEN];
    snprintf(name, sizeof(name), "%s", p->name);
    int remaining = p->total_days - p->taken_days;
    tal_mutex_unlock(sg_medicine_mutex);

    /* Take a photo to record medicine intake */
    __take_medicine_photo(name);

    /* Notify via AI */
    char msg[256];
    if (remaining > 0) {
        snprintf(msg, sizeof(msg),
                 "[System] Patient confirmed taking '%s'. "
                 "%d day(s) of treatment remaining. "
                 "Please acknowledge and encourage the patient.",
                 name, remaining);
    } else {
        snprintf(msg, sizeof(msg),
                 "[System] Patient confirmed taking '%s'. "
                 "Treatment course is now complete. "
                 "Please congratulate the patient on completing the full course.",
                 name);
    }
    ai_agent_send_text(msg);

    cJSON_AddBoolToObject(json, "success", TRUE);
    cJSON_AddNumberToObject(json, "confirmed_slots", confirmed_count);
    cJSON_AddNumberToObject(json, "remaining_days",  remaining > 0 ? remaining : 0);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] confirmed medicine plan id=%d, %d slot(s) confirmed", TAG, plan_id, confirmed_count);
    return OPRT_OK;
}

/* ---- MQTT-connected event callback: register tools ---- */
static OPERATE_RET __medicine_mcp_register(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "medicine_add",
        "Add a medicine plan for a patient via voice.\n"
        "Parameters:\n"
        "- name (string): Medicine name.\n"
        "- pills_per_dose (int, default 1): Number of pills per single dose.\n"
        "- doses_per_day (int, 1-4): How many times per day to take the medicine.\n"
        "- total_days (int): Total number of days for the treatment course.\n"
        "- time_slots (int, bitmask): When to take the medicine each day. "
        "  bit0=morning(8:00), bit1=noon(12:00), bit2=evening(18:00), bit3=bedtime(21:00). "
        "  e.g. 3=morning+noon, 7=morning+noon+evening, 0=auto-assign.\n"
        "Response: the created medicine plan object.",
        __medicine_add,
        NULL,
        MCP_PROP_STR("name",              "Medicine name."),
        MCP_PROP_INT_DEF_RANGE("pills_per_dose", "Pills per dose.",  1, 1, 20),
        MCP_PROP_INT_DEF_RANGE("doses_per_day",  "Doses per day.",   1, 1, 4),
        MCP_PROP_INT_DEF_RANGE("total_days",     "Total treatment days.", 1, 1, 365),
        MCP_PROP_INT_DEF_RANGE("time_slots", "Bitmask: bit0=morning, bit1=noon, bit2=evening, bit3=bedtime. 0=auto.", 0, 0, 15)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "medicine_query",
        "Query medicine plans on the device.\n"
        "Parameters:\n"
        "- active_only (bool, default true): If true, only return active plans.\n"
        "Response: count and array of matching medicine plan objects.",
        __medicine_query,
        NULL,
        MCP_PROP_BOOL_DEF("active_only", "Only return active plans.", TRUE)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "medicine_delete",
        "Delete a medicine plan by its ID.\n"
        "Parameters:\n"
        "- plan_id (int): ID of the medicine plan to delete.\n"
        "Response: success flag and deleted plan ID.",
        __medicine_delete,
        NULL,
        MCP_PROP_INT("plan_id", "ID of the medicine plan to delete.")
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "medicine_confirm",
        "Patient confirms they have taken their medicine.\n"
        "This stops the current reminder alarm and triggers a camera photo.\n"
        "Parameters:\n"
        "- plan_id (int): ID of the medicine plan to confirm.\n"
        "- slot (int, optional): Dose time slot to confirm: 0=morning, 1=noon, 2=evening, 3=bedtime. "
        "  -1 (default) confirms all currently triggered slots.\n"
        "Response: success flag, number of confirmed slots, and remaining treatment days.",
        __medicine_confirm,
        NULL,
        MCP_PROP_INT("plan_id", "ID of the medicine plan to confirm."),
        MCP_PROP_INT_DEF("slot", "Dose slot: 0=morning,1=noon,2=evening,3=bedtime. -1=all triggered.", -1)
    ), err);

    PR_DEBUG("[%s] MCP tools registered: medicine_add, medicine_query, medicine_delete, medicine_confirm", TAG);
    return OPRT_OK;

err:
    PR_ERR("[%s] failed to register MCP tools, rt:%d", TAG, rt);
    return rt;
}

OPERATE_RET app_medicine_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&sg_medicine_mutex));

    memset(&sg_medicine_list, 0, sizeof(sg_medicine_list));
    sg_medicine_list.next_id = 0;

    __medicine_load();

    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(__medicine_check_timer_cb, NULL, &sg_medicine_check_timer));
    TUYA_CALL_ERR_RETURN(tal_sw_timer_start(sg_medicine_check_timer, 60 * 1000, TAL_TIMER_CYCLE));

    TUYA_CALL_ERR_RETURN(tal_event_subscribe(EVENT_MQTT_CONNECTED, "app_medicine_init",
                                              __medicine_mcp_register, SUBSCRIBE_TYPE_ONETIME));
    return rt;
}
