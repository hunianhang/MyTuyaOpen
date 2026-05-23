/**
 * @file app_todo.h
 * @brief Todo list module for your_life_companion.
 *        Registers MCP tools for adding, querying, and deleting todo items
 *        via voice commands. Data is persisted to KV storage.
 * @version 0.1
 */

#ifndef __APP_TODO_H__
#define __APP_TODO_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
 ************************ macro define **********************
 ***********************************************************/
#define APP_TODO_MAX_COUNT    20
#define APP_TODO_CONTENT_LEN  128
#define APP_TODO_KV_KEY       "app_todos"

/***********************************************************
 *********************** typedef define *********************
 ***********************************************************/
typedef struct {
    int  id;
    char content[APP_TODO_CONTENT_LEN];
    bool done;
} app_todo_t;

typedef struct {
    int         count;
    int         next_id;
    app_todo_t  todos[APP_TODO_MAX_COUNT];
} app_todo_list_t;

/***********************************************************
 ******************** function declaration ******************
 ***********************************************************/
/**
 * @brief Initialize todo module and register MCP tools.
 *        Registration is deferred until MQTT connects.
 * @return OPERATE_RET
 */
OPERATE_RET app_todo_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TODO_H__ */
