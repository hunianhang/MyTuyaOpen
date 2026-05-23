/**
 * @file app_todo.c
 * @brief Todo list MCP tools for your_life_companion.
 *        Provides voice-operable todo management via three MCP tools:
 *          - todo_add    : add a new todo item
 *          - todo_query  : list todo items filtered by status
 *          - todo_delete : delete a todo item by ID
 *        Data is persisted to KV storage as JSON.
 * @version 0.1
 */

#include <string.h>
#include "tal_api.h"
#include "ai_mcp_server.h"
#include "app_todo.h"

/***********************************************************
 ************************ macro define **********************
 ***********************************************************/
#define TAG "app_todo"

/***********************************************************
 *********************** variable define ********************
 ***********************************************************/
static app_todo_list_t sg_todo_list = {0};
static MUTEX_HANDLE    sg_todo_mutex = NULL;

/***********************************************************
 *********************** function define ********************
 ***********************************************************/

/* ---- KV persistence ---- */

static void __todo_save(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "next_id", sg_todo_list.next_id);

    cJSON *arr = cJSON_AddArrayToObject(root, "todos");
    for (int i = 0; i < sg_todo_list.count; i++) {
        app_todo_t *t = &sg_todo_list.todos[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id",      t->id);
        cJSON_AddStringToObject(item, "content", t->content);
        cJSON_AddBoolToObject(item,   "done",    t->done);
        cJSON_AddItemToArray(arr, item);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return;

    tal_kv_set(APP_TODO_KV_KEY, (uint8_t *)json_str, strlen(json_str));
    tal_free(json_str);
}

static void __todo_load(void)
{
    uint8_t *value = NULL;
    size_t   length = 0;

    if (OPRT_OK != tal_kv_get(APP_TODO_KV_KEY, &value, &length)) {
        return;
    }

    cJSON *root = cJSON_ParseWithLength((char *)value, length);
    tal_kv_free(value);
    if (!root) return;

    cJSON *next_id_node = cJSON_GetObjectItem(root, "next_id");
    if (cJSON_IsNumber(next_id_node)) {
        sg_todo_list.next_id = (int)next_id_node->valuedouble;
    }

    cJSON *arr = cJSON_GetObjectItem(root, "todos");
    int cnt = cJSON_GetArraySize(arr);
    sg_todo_list.count = 0;

    for (int i = 0; i < cnt && sg_todo_list.count < APP_TODO_MAX_COUNT; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        app_todo_t *t = &sg_todo_list.todos[sg_todo_list.count];

        cJSON *id_node = cJSON_GetObjectItem(item, "id");
        cJSON *content_node = cJSON_GetObjectItem(item, "content");
        cJSON *done_node = cJSON_GetObjectItem(item, "done");

        if (!cJSON_IsNumber(id_node) || !cJSON_IsString(content_node)) continue;

        t->id = (int)id_node->valuedouble;
        snprintf(t->content, APP_TODO_CONTENT_LEN, "%s", content_node->valuestring);
        t->done = cJSON_IsTrue(done_node);
        sg_todo_list.count++;
    }

    cJSON_Delete(root);
    PR_DEBUG("[%s] loaded %d todos from KV", TAG, sg_todo_list.count);
}

/* ---- helper: build a JSON object for one todo ---- */
static cJSON *__todo_to_json(const app_todo_t *t)
{
    cJSON *item = cJSON_CreateObject();
    if (!item) return NULL;
    cJSON_AddNumberToObject(item, "id",      t->id);
    cJSON_AddStringToObject(item, "content", t->content);
    cJSON_AddBoolToObject(item,   "done",    t->done);
    return item;
}

/* ---- MCP tool: todo_add ---- */
static OPERATE_RET __todo_add(const MCP_PROPERTY_LIST_T *properties,
                               MCP_RETURN_VALUE_T *ret_val,
                               void *user_data)
{
    const char *content = NULL;

    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "content") == 0 &&
            prop->type == MCP_PROPERTY_TYPE_STRING &&
            prop->default_val.str_val) {
            content = prop->default_val.str_val;
            break;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    if (!content || content[0] == '\0') {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'content' is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    tal_mutex_lock(sg_todo_mutex);

    if (sg_todo_list.count >= APP_TODO_MAX_COUNT) {
        tal_mutex_unlock(sg_todo_mutex);
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Todo list is full. Please delete some items first.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    app_todo_t *t = &sg_todo_list.todos[sg_todo_list.count];
    t->id   = ++sg_todo_list.next_id;
    t->done = false;
    snprintf(t->content, APP_TODO_CONTENT_LEN, "%s", content);
    sg_todo_list.count++;

    __todo_save();
    cJSON *todo_json = __todo_to_json(t);
    tal_mutex_unlock(sg_todo_mutex);

    cJSON_AddBoolToObject(json, "success", TRUE);
    if (todo_json) cJSON_AddItemToObject(json, "todo", todo_json);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] added todo id=%d: %s", TAG, t->id, content);
    return OPRT_OK;
}

/* ---- MCP tool: todo_query ---- */
static OPERATE_RET __todo_query(const MCP_PROPERTY_LIST_T *properties,
                                 MCP_RETURN_VALUE_T *ret_val,
                                 void *user_data)
{
    /* status: 0=pending, 1=completed, 2=all (default) */
    int status = 2;
    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "status") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            status = prop->default_val.int_val;
            break;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    cJSON *arr = cJSON_CreateArray();
    if (!arr) { cJSON_Delete(json); return OPRT_MALLOC_FAILED; }

    tal_mutex_lock(sg_todo_mutex);
    int matched = 0;
    for (int i = 0; i < sg_todo_list.count; i++) {
        app_todo_t *t = &sg_todo_list.todos[i];
        bool include = (status == 2) ||
                       (status == 0 && !t->done) ||
                       (status == 1 && t->done);
        if (include) {
            cJSON *item = __todo_to_json(t);
            if (item) cJSON_AddItemToArray(arr, item);
            matched++;
        }
    }
    tal_mutex_unlock(sg_todo_mutex);

    cJSON_AddNumberToObject(json, "count", matched);
    cJSON_AddItemToObject(json, "todos", arr);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] query todos status=%d, matched=%d", TAG, status, matched);
    return OPRT_OK;
}

/* ---- MCP tool: todo_delete ---- */
static OPERATE_RET __todo_delete(const MCP_PROPERTY_LIST_T *properties,
                                  MCP_RETURN_VALUE_T *ret_val,
                                  void *user_data)
{
    int todo_id = -1;
    for (int i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (strcmp(prop->name, "todo_id") == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            todo_id = prop->default_val.int_val;
            break;
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) return OPRT_MALLOC_FAILED;

    if (todo_id < 0) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Parameter 'todo_id' is required.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    tal_mutex_lock(sg_todo_mutex);

    if (todo_id == 0) {
        /* Delete all completed todos */
        int deleted = 0;
        int new_count = 0;
        for (int i = 0; i < sg_todo_list.count; i++) {
            if (sg_todo_list.todos[i].done) {
                deleted++;
            } else {
                sg_todo_list.todos[new_count++] = sg_todo_list.todos[i];
            }
        }
        sg_todo_list.count = new_count;
        __todo_save();
        tal_mutex_unlock(sg_todo_mutex);

        cJSON_AddBoolToObject(json, "success", TRUE);
        cJSON_AddNumberToObject(json, "deleted_count", deleted);
        PR_DEBUG("[%s] deleted %d completed todos", TAG, deleted);
    } else {
        /* Delete by specific ID */
        int found = -1;
        for (int i = 0; i < sg_todo_list.count; i++) {
            if (sg_todo_list.todos[i].id == todo_id) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            tal_mutex_unlock(sg_todo_mutex);
            cJSON_AddBoolToObject(json, "success", FALSE);
            cJSON_AddStringToObject(json, "error", "Todo item not found.");
            ai_mcp_return_value_set_json(ret_val, json);
            return OPRT_OK;
        }
        for (int i = found; i < sg_todo_list.count - 1; i++) {
            sg_todo_list.todos[i] = sg_todo_list.todos[i + 1];
        }
        sg_todo_list.count--;
        __todo_save();
        tal_mutex_unlock(sg_todo_mutex);

        cJSON_AddBoolToObject(json, "success", TRUE);
        cJSON_AddNumberToObject(json, "deleted_id", todo_id);
        PR_DEBUG("[%s] deleted todo id=%d", TAG, todo_id);
    }

    ai_mcp_return_value_set_json(ret_val, json);
    return OPRT_OK;
}

/* ---- MQTT-connected event callback: register tools ---- */
static OPERATE_RET __todo_mcp_register(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "todo_add",
        "Add a new todo item to the device's todo list.\n"
        "Parameters:\n"
        "- content (string): The todo item description.\n"
        "Response: the created todo object with its ID.",
        __todo_add,
        NULL,
        MCP_PROP_STR("content", "The content description of the todo item.")
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "todo_query",
        "Query todo items from the device's todo list.\n"
        "Parameters:\n"
        "- status (int): Filter by status: 0=pending only, 1=completed only, 2=all (default).\n"
        "Response: count and array of matching todo items.",
        __todo_query,
        NULL,
        MCP_PROP_INT_DEF_RANGE("status", "Filter: 0=pending, 1=completed, 2=all.", 2, 0, 2)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "todo_delete",
        "Delete a todo item by its ID.\n"
        "Parameters:\n"
        "- todo_id (int): ID of the item to delete. Use 0 to delete all completed items.\n"
        "Response: success flag and deleted item ID or count.",
        __todo_delete,
        NULL,
        MCP_PROP_INT("todo_id", "ID of the todo item to delete. Use 0 to delete all completed items.")
    ), err);

    PR_DEBUG("[%s] MCP tools registered: todo_add, todo_query, todo_delete", TAG);
    return OPRT_OK;

err:
    PR_ERR("[%s] failed to register MCP tools, rt:%d", TAG, rt);
    return rt;
}

OPERATE_RET app_todo_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&sg_todo_mutex));

    memset(&sg_todo_list, 0, sizeof(sg_todo_list));
    sg_todo_list.next_id = 0;

    __todo_load();

    TUYA_CALL_ERR_RETURN(tal_event_subscribe(EVENT_MQTT_CONNECTED, "app_todo_init",
                                              __todo_mcp_register, SUBSCRIBE_TYPE_ONETIME));
    return rt;
}
