/**
 * @file app_medicine.h
 * @brief Medicine reminder module for your_life_companion.
 *        Provides voice-operable medicine plan management via MCP tools:
 *          - medicine_add    : add a medicine plan (name, dose, frequency, times, days)
 *          - medicine_query  : list all active medicine plans
 *          - medicine_delete : delete a medicine plan
 *          - medicine_confirm: patient confirms they have taken medicine
 *        A 60-second cycle timer checks reminder times and alerts if not confirmed.
 *        Camera captures a photo (JPEG, timestamp filename) when medicine is taken.
 * @version 0.1
 */

#ifndef __APP_MEDICINE_H__
#define __APP_MEDICINE_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
 ************************ macro define **********************
 ***********************************************************/
#define APP_MEDICINE_MAX_COUNT     10
#define APP_MEDICINE_NAME_LEN      64
#define APP_MEDICINE_KV_KEY        "app_medicine"
#define APP_MEDICINE_PHOTO_DIR     "/sdcard/medicine"
#define APP_MEDICINE_MAX_TIMES     4    /* max dose times per day: 早/中/晚/睡前 */

/***********************************************************
 *********************** typedef define *********************
 ***********************************************************/

/* Dose time slot: 0=morning, 1=noon, 2=evening, 3=bedtime */
typedef enum {
    MEDICINE_TIME_MORNING  = 0,
    MEDICINE_TIME_NOON     = 1,
    MEDICINE_TIME_EVENING  = 2,
    MEDICINE_TIME_BEDTIME  = 3,
} app_medicine_time_slot_e;

/* One dose time entry within a plan */
typedef struct {
    int  slot;          /* app_medicine_time_slot_e */
    int  hour;          /* reminder hour  (0-23) */
    int  minute;        /* reminder minute (0-59) */
    bool confirmed;     /* has the patient confirmed this dose today */
    bool triggered;     /* has the reminder been triggered today */
} app_medicine_dose_time_t;

/* One medicine plan */
typedef struct {
    int  id;
    char name[APP_MEDICINE_NAME_LEN];   /* medicine name */
    int  pills_per_dose;                /* pills per single dose */
    int  doses_per_day;                 /* how many times per day (1-4) */
    int  total_days;                    /* total treatment days */
    int  taken_days;                    /* days already completed */
    bool active;                        /* plan active flag */
    app_medicine_dose_time_t times[APP_MEDICINE_MAX_TIMES];
} app_medicine_plan_t;

typedef struct {
    int                 count;
    int                 next_id;
    app_medicine_plan_t plans[APP_MEDICINE_MAX_COUNT];
} app_medicine_list_t;

/***********************************************************
 ******************** function declaration ******************
 ***********************************************************/
/**
 * @brief Initialize medicine module, register MCP tools, and start the check timer.
 * @return OPERATE_RET
 */
OPERATE_RET app_medicine_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MEDICINE_H__ */
