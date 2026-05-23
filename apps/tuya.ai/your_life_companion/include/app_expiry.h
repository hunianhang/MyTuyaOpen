/**
 * @file app_expiry.h
 * @brief Food/snack expiry tracking module for your_life_companion.
 *        Registers MCP tools for adding, querying, and deleting products
 *        with expiry dates via voice commands. Data is persisted to KV storage.
 *        An hourly timer checks expiry dates and reminds the user N days
 *        before a product expires.
 * @version 0.1
 */

#ifndef __APP_EXPIRY_H__
#define __APP_EXPIRY_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
 ************************ macro define **********************
 ***********************************************************/
#define APP_EXPIRY_MAX_COUNT       30
#define APP_EXPIRY_NAME_LEN        64
#define APP_EXPIRY_KV_KEY          "app_expiry"
#define APP_EXPIRY_DEFAULT_WARN_DAYS  3   /* warn 3 days before expiry by default */

/***********************************************************
 *********************** typedef define *********************
 ***********************************************************/
typedef struct {
    int  id;
    char name[APP_EXPIRY_NAME_LEN];   /* product name */
    int  year;                         /* expiry year  (e.g. 2026) */
    int  month;                        /* expiry month (1-12) */
    int  day;                          /* expiry day   (1-31) */
    int  warn_days;                    /* remind N days before expiry */
    bool reminded;                     /* has the reminder been sent already */
    bool active;                       /* false = product consumed / deleted */
} app_expiry_item_t;

typedef struct {
    int               count;
    int               next_id;
    app_expiry_item_t items[APP_EXPIRY_MAX_COUNT];
} app_expiry_list_t;

/***********************************************************
 ******************** function declaration ******************
 ***********************************************************/
/**
 * @brief Initialize expiry module, register MCP tools, and start the check timer.
 * @return OPERATE_RET
 */
OPERATE_RET app_expiry_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_EXPIRY_H__ */
