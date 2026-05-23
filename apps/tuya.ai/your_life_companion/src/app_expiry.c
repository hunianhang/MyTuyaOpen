/**
 * @file app_expiry.c
 * @brief Food/snack expiry tracking MCP tools for your_life_companion.
 *
 *  MCP tools registered:
 *    expiry_add    - add a product with its expiry date (voice-operable)
 *    expiry_query  - query products (all / expiring soon / expired)
 *    expiry_delete - mark a product as consumed/removed by ID
 *
 *  Behaviour:
 *    - An hourly timer compares today's date against each product's expiry date.
 *    - When the remaining days reach <= warn_days, it sends an AI voice reminder
 *      (via ai_agent_send_text) once per day.
 *    - When a product has already expired, the reminder says "already expired".
 *    - Data is persisted to KV storage as JSON.
 * @version 0.1
 */

#include <string.h>
#include <stdio.h>
#include "tal_api.h"
#include "tal_time_service.h"
#include "ai_mcp_server.h"
#include "ai_agent.h"
#include "ai_audio_player.h"
#include "app_expiry.h"

/***********************************************************
 ************************ macro define **********************
 ***********************************************************/
#define TAG "app_expiry"

/* Timer interval: check every hour */
#define EXPIRY_CHECK_INTERVAL_MS  (60 * 60 * 1000)

/***********************************************************
 *********************** variable define ********************
 ***********************************************************/
static app_expiry_list_t sg_expiry_list  = {0};
static MUTEX_HANDLE      sg_expiry_mutex = NULL;
static TIMER_ID          sg_expiry_check_timer = NULL;

/***********************************************************
 ********************* helper functions ********************
 ***********************************************************/

/**
 * Convert a calendar date to a simple integer day-count from year 0
 * (Julian Day Number approximation, good enough for date arithmetic).
 * Returns days since a fixed epoch so that subtraction gives day difference.
 */
static int __date_to_days(int year, int month, int day)
{
    /* Zeller/civil date to day-number (no overflow for reasonable years) */
    if (month < 3) {
        month += 12;
        year  -= 1;
    }
    int a = year / 100;
    int b = 2 - a + (a / 4);
    return (int)(365.25 * (year + 4716)) + (int)(30.6001 * (month + 1)) + day + b - 1524;
}

/** Returns how many days until the product expires (negative = already expired). */
static int __days_remaining(const app_expiry_item_t *item, const POSIX_TM_S *now)
{
    int today_days  = __date_to_days(now->tm_year + 1900,
                                     now->tm_mon  + 1,
                                     now->tm_mday);
    int expiry_days = __date_to_days(item->year, item->month, item->day);
    return expiry_days - today_days;
}

/* ---- KV persistence ---- */

static void __expiry_save(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "next_id", sg_expiry_list.next_id);

    cJSON *arr = cJSON_AddArrayToObject(root, "items");
    for (int i = 0; i < sg_expiry_list.count; i++) {
        app_expiry_item_t *it = &sg_expiry_list.items[i];
        if (!it->active) continue;   /* don't persist deleted items */
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id",        it->id);
        cJSON_AddStringToObject(item, "name",      it->name);
        cJSON_AddNumberToObject(item, "year",      it->year);
        cJSON_AddNumberToObject(item, "month",     it->month);
        cJSON_AddNumberToObject(item, "day",       it->day);
        cJSON_AddNumberToObject(item, "warn_days", it->warn_days);
        cJSON_AddBoolToObject(item,   "reminded",  it->reminded);
        cJSON_AddItemToArray(arr, item);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return;

    tal_kv_set(APP_EXPIRY_KV_KEY, (uint8_t *)json_str, strlen(json_str));
    tal_free(json_str);
}

static void __expiry_load(void)
{
    uint8_t *value  = NULL;
    size_t   length = 0;

    if (OPRT_OK != tal_kv_get(APP_EXPIRY_KV_KEY, &value, &length)) {
        return;
    }

    cJSON *root = cJSON_ParseWithLength((char *)value, length);
    tal_kv_free(value);
    if (!root) return;

    cJSON *next_id_node = cJSON_GetObjectItem(root, "next_id");
    if (cJSON_IsNumber(next_id_node)) {
        sg_expiry_list.next_id = (int)next_id_node->valuedouble;
    }

    cJSON *arr = cJSON_GetObjectItem(root, "items");
    int cnt = cJSON_GetArraySize(arr);
    sg_expiry_list.count = 0;

    for (int i = 0; i < cnt && sg_expiry_list.count < APP_EXPIRY_MAX_COUNT; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        app_expiry_item_t *it = &sg_expiry_list.items[sg_expiry_list.count];

        cJSON *id_node       = cJSON_GetObjectItem(item, "id");
        cJSON *name_node     = cJSON_GetObjectItem(item, "name");
        cJSON *year_node     = cJSON_GetObjectItem(item, "year");
        cJSON *month_node    = cJSON_GetObjectItem(item, "month");
        cJSON *day_node      = cJSON_GetObjectItem(item, "day");
        cJSON *warn_node     = cJSON_GetObjectItem(item, "warn_days");
        cJSON *reminded_node = cJSON_GetObjectItem(item, "reminded");

        if (!cJSON_IsNumber(id_node)   ||
            !cJSON_IsString(name_node) ||
            !cJSON_IsNumber(year_node) ||
            !cJSON_IsNumber(month_node)||
            !cJSON_IsNumber(day_node)) {
            continue;
        }

        it->id        = (int)id_node->valuedouble;
        it->year      = (int)year_node->valuedouble;
        it->month     = (int)month_node->valuedouble;
        it->day       = (int)day_node->valuedouble;
        it->warn_days = cJSON_IsNumber(warn_node) ? (int)warn_node->valuedouble : APP_EXPIRY_DEFAULT_WARN_DAYS;
        it->reminded  = cJSON_IsTrue(reminded_node);
        it->active    = true;
        snprintf(it->name, APP_EXPIRY_NAME_LEN, "%s", name_node->valuestring);

        sg_expiry_list.count++;
    }

    cJSON_Delete(root);
    PR_DEBUG("[%s] loaded %d expiry items from KV", TAG, sg_expiry_list.count);
}

/* ---- helper: build JSON for one item ---- */
static cJSON *__item_to_json(const app_expiry_item_t *it, int days_left)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;
    cJSON_AddNumberToObject(obj, "id",          it->id);
    cJSON_AddStringToObject(obj, "name",        it->name);

    char date_str[16];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", it->year, it->month, it->day);
    cJSON_AddStringToObject(obj, "expiry_date", date_str);
    cJSON_AddNumberToObject(obj, "warn_days",   it->warn_days);
    cJSON_AddNumberToObject(obj, "days_left",   days_left);

    if (days_left < 0) {
        cJSON_AddStringToObject(obj, "status", "expired");
    } else if (days_left == 0) {
        cJSON_AddStringToObject(obj, "status", "expires_today");
    } else if (days_left <= it->warn_days) {
        cJSON_AddStringToObject(obj, "status", "expiring_soon");
    } else {
        cJSON_AddStringToObject(obj, "status", "ok");
    }

    return obj;
}

/* ---- Hourly timer: scan for expiring / expired products ---- */
static void __expiry_check_timer_cb(TIMER_ID timer_id, void *arg)
{
    if (OPRT_OK != tal_time_check_time_sync()) {
        return;
    }

    POSIX_TM_S now = {0};
    tal_time_get_local_time_custom(0, &now);

    /* Only perform reminder check once per day, at 9:00 AM */
    if (now.tm_hour != 9) {
        return;
    }

    tal_mutex_lock(sg_expiry_mutex);

    bool need_save = false;

    for (int i = 0; i < sg_expiry_list.count; i++) {
        app_expiry_item_t *it = &sg_expiry_list.items[i];
        if (!it->active) continue;

        int days_left = __days_remaining(it, &now);

        /* Trigger reminder when within warn window or already expired */
        if (days_left <= it->warn_days) {
            if (!it->reminded) {
                it->reminded = true;
                need_save    = true;

                /* Capture before unlocking */
                char name[APP_EXPIRY_NAME_LEN];
                snprintf(name, sizeof(name), "%s", it->name);
                int  warn = it->warn_days;
                int  dl   = days_left;
                int  yr   = it->year;
                int  mo   = it->month;
                int  dy   = it->day;

                tal_mutex_unlock(sg_expiry_mutex);

                /* Play local alert */
                ai_audio_player_alert(AI_AUDIO_ALERT_WAKEUP);

                /* Build and send AI announcement */
                char msg[256];
                if (dl < 0) {
                    snprintf(msg, sizeof(msg),
                             "[System] Food expiry alert: '%s' (expiry date %04d-%02d-%02d) "
                             "has already EXPIRED %d day(s) ago. "
                             "Please warn the user urgently and advise them to discard it.",
                             name, yr, mo, dy, -dl);
                } else if (dl == 0) {
                    snprintf(msg, sizeof(msg),
                             "[System] Food expiry alert: '%s' (expiry date %04d-%02d-%02d) "
                             "expires TODAY. "
                             "Please remind the user to use or discard it immediately.",
                             name, yr, mo, dy);
                } else {
                    snprintf(msg, sizeof(msg),
                             "[System] Food expiry alert: '%s' (expiry date %04d-%02d-%02d) "
                             "will expire in %d day(s). "
                             "Please remind the user to consume or check it soon.",
                             name, yr, mo, dy, dl);
                }
                ai_agent_send_text(msg);

                PR_DEBUG("[%s] expiry reminder sent for '%s', days_left=%d warn=%d",
                         TAG, name, dl, warn);

                tal_mutex_lock(sg_expiry_mutex);
            }
        } else {
            /* Reset reminded flag each day so it can re-trigger if days_left decreases */
            if (it->reminded) {
                it->reminded = false;
                need_save    = true;
            }
        }
    }

    if (need_save) {
        __expiry_save();
    }

    tal_mutex_unlock(sg_expiry_mutex);
}

/* ---- MCP tool: expiry_add ---- */
static OPERATE_RET __expiry_add(const MCP_PROPERTY_LIST_T *properties,
                                 MCP_RETURN_VALUE_T *ret_val,
                                 void *user_data)
{
    const char *name = NULL;
    int  year        = -1;
    int  month       = -1;
    int  day         = -1;
    int  warn_days   = APP_EXPIRY_DEFAULT_WARN_DAYS;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "name") == 0 && prop->type == MCP_PROPERTY_TYPE_STRING) {
            name = prop->default_val.str_val;
        } else if (strcmp(prop->name, "year") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            year = prop->default_val.int_val;
        } else if (strcmp(prop->name, "month") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            month = prop->default_val.int_val;
        } else if (strcmp(prop->name, "day") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            day = prop->default_val.int_val;
        } else if (strcmp(prop->name, "warn_days") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            warn_days = prop->default_val.int_val;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    /* Validate */
    if (!name || name[0] == '\0') {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'name' is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }
    if (year < 2000 || year > 2100) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'year' (2000-2100) is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }
    if (month < 1 || month > 12) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'month' (1-12) is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }
    if (day < 1 || day > 31) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'day' (1-31) is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }
    if (warn_days < 0 || warn_days > 30) {
        warn_days = APP_EXPIRY_DEFAULT_WARN_DAYS;
    }

    tal_mutex_lock(sg_expiry_mutex);

    if (sg_expiry_list.count >= APP_EXPIRY_MAX_COUNT) {
        tal_mutex_unlock(sg_expiry_mutex);
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Product list is full. Please delete some items first.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    app_expiry_item_t *it = &sg_expiry_list.items[sg_expiry_list.count];
    memset(it, 0, sizeof(*it));
    it->id        = ++sg_expiry_list.next_id;
    it->year      = year;
    it->month     = month;
    it->day       = day;
    it->warn_days = warn_days;
    it->reminded  = false;
    it->active    = true;
    snprintf(it->name, APP_EXPIRY_NAME_LEN, "%s", name);
    sg_expiry_list.count++;

    __expiry_save();

    /* Compute days_left for response */
    POSIX_TM_S now = {0};
    tal_time_get_local_time_custom(0, &now);
    int days_left = __days_remaining(it, &now);

    cJSON *item_json = __item_to_json(it, days_left);
    tal_mutex_unlock(sg_expiry_mutex);

    cJSON_AddBoolToObject(json, "success", TRUE);
    if (item_json) cJSON_AddItemToObject(json, "item", item_json);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] added expiry item id=%d name='%s' expiry=%04d-%02d-%02d warn=%d days",
             TAG, it->id, name, year, month, day, warn_days);
    return OPRT_OK;
}

/* ---- MCP tool: expiry_query ---- */
static OPERATE_RET __expiry_query(const MCP_PROPERTY_LIST_T *properties,
                                   MCP_RETURN_VALUE_T *ret_val,
                                   void *user_data)
{
    /*
     * filter:
     *   0 = all active items
     *   1 = expiring soon (within warn_days) or already expired
     *   2 = expired only
     */
    int filter = 0;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "filter") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            filter = prop->default_val.int_val;
            break;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    cJSON *arr = cJSON_CreateArray();
    if (!arr) { cJSON_Delete(json); return OPRT_MALLOC_FAILED; }

    POSIX_TM_S now = {0};
    tal_time_get_local_time_custom(0, &now);

    tal_mutex_lock(sg_expiry_mutex);
    int matched = 0;
    for (int i = 0; i < sg_expiry_list.count; i++) {
        app_expiry_item_t *it = &sg_expiry_list.items[i];
        if (!it->active) continue;

        int days_left = __days_remaining(it, &now);

        bool include = false;
        switch (filter) {
        case 1:  include = (days_left <= it->warn_days); break;
        case 2:  include = (days_left < 0);              break;
        default: include = true;                          break;
        }

        if (include) {
            cJSON *obj = __item_to_json(it, days_left);
            if (obj) cJSON_AddItemToArray(arr, obj);
            matched++;
        }
    }
    tal_mutex_unlock(sg_expiry_mutex);

    cJSON_AddNumberToObject(json, "count", matched);
    cJSON_AddItemToObject(json, "items", arr);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] query expiry items filter=%d matched=%d", TAG, filter, matched);
    return OPRT_OK;
}

/* ---- MCP tool: expiry_delete ---- */
static OPERATE_RET __expiry_delete(const MCP_PROPERTY_LIST_T *properties,
                                    MCP_RETURN_VALUE_T *ret_val,
                                    void *user_data)
{
    int item_id = -1;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "item_id") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            item_id = prop->default_val.int_val;
            break;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    if (item_id <= 0) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'item_id' (>0) is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    tal_mutex_lock(sg_expiry_mutex);

    int found = -1;
    for (int i = 0; i < sg_expiry_list.count; i++) {
        if (sg_expiry_list.items[i].id == item_id && sg_expiry_list.items[i].active) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        tal_mutex_unlock(sg_expiry_mutex);
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Product not found.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    /* Compact the array */
    for (int i = found; i < sg_expiry_list.count - 1; i++) {
        sg_expiry_list.items[i] = sg_expiry_list.items[i + 1];
    }
    sg_expiry_list.count--;
    __expiry_save();
    tal_mutex_unlock(sg_expiry_mutex);

    cJSON_AddBoolToObject(json, "success", TRUE);
    cJSON_AddNumberToObject(json, "deleted_id", item_id);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] deleted expiry item id=%d", TAG, item_id);
    return OPRT_OK;
}

/* ---- MQTT-connected event callback: register MCP tools ---- */
static OPERATE_RET __expiry_mcp_register(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "expiry_add",
        "Add a food or snack product with its expiry date.\n"
        "The user can say the product name and expiry date by voice.\n"
        "Parameters:\n"
        "- name (string): Product name (e.g. 'potato chips', 'milk').\n"
        "- year  (int): Expiry year  (e.g. 2026).\n"
        "- month (int, 1-12): Expiry month.\n"
        "- day   (int, 1-31): Expiry day.\n"
        "- warn_days (int, default 3): How many days before expiry to start reminding.\n"
        "Response: the created product item with its ID and days_left.",
        __expiry_add,
        NULL,
        MCP_PROP_STR("name",   "Product name."),
        MCP_PROP_INT_RANGE("year",  "Expiry year (e.g. 2026).",  2000, 2100),
        MCP_PROP_INT_RANGE("month", "Expiry month (1-12).",       1,   12),
        MCP_PROP_INT_RANGE("day",   "Expiry day (1-31).",         1,   31),
        MCP_PROP_INT_DEF_RANGE("warn_days",
            "Remind this many days before expiry (default 3).",
            APP_EXPIRY_DEFAULT_WARN_DAYS, 0, 30)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "expiry_query",
        "Query the list of tracked food/snack products.\n"
        "Parameters:\n"
        "- filter (int, default 0): "
        "  0=all active, 1=expiring soon or expired, 2=expired only.\n"
        "Response: count and array of product items with days_left and status.",
        __expiry_query,
        NULL,
        MCP_PROP_INT_DEF_RANGE("filter",
            "0=all active, 1=expiring soon or expired, 2=expired only.",
            0, 0, 2)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "expiry_delete",
        "Remove a product from the expiry tracking list (e.g. it has been consumed or discarded).\n"
        "Parameters:\n"
        "- item_id (int): ID of the product to remove.\n"
        "Response: success flag and deleted item ID.",
        __expiry_delete,
        NULL,
        MCP_PROP_INT("item_id", "ID of the product to remove.")
    ), err);

    PR_DEBUG("[%s] MCP tools registered: expiry_add, expiry_query, expiry_delete", TAG);
    return OPRT_OK;

err:
    PR_ERR("[%s] failed to register MCP tools, rt:%d", TAG, rt);
    return rt;
}

OPERATE_RET app_expiry_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&sg_expiry_mutex));

    memset(&sg_expiry_list, 0, sizeof(sg_expiry_list));
    sg_expiry_list.next_id = 0;

    __expiry_load();

    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(__expiry_check_timer_cb, NULL, &sg_expiry_check_timer));
    TUYA_CALL_ERR_RETURN(tal_sw_timer_start(sg_expiry_check_timer, EXPIRY_CHECK_INTERVAL_MS, TAL_TIMER_CYCLE));

    TUYA_CALL_ERR_RETURN(tal_event_subscribe(EVENT_MQTT_CONNECTED, "app_expiry_init",
                                              __expiry_mcp_register, SUBSCRIBE_TYPE_ONETIME));
    return rt;
}
