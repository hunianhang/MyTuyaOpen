/**
 * @file app_weather.h
 * @brief Weather query module for your_life_companion.
 *        Registers a weather_get_current MCP tool that allows the cloud AI
 *        to retrieve current weather conditions via voice command.
 * @version 0.1
 */

#ifndef __APP_WEATHER_H__
#define __APP_WEATHER_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize weather module and register MCP tool.
 *        Registration is deferred until MQTT connects.
 * @return OPERATE_RET
 */
OPERATE_RET app_weather_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_WEATHER_H__ */
