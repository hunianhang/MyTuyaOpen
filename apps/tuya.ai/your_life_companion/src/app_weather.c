/**
 * @file app_weather.c
 * @brief Weather query MCP tool for your_life_companion.
 *        Wraps Tuya cloud weather APIs and exposes a single MCP tool
 *        "weather_get_current" that the cloud AI can call to answer
 *        voice weather queries.
 * @version 0.1
 */

#include "tal_api.h"
#include "tuya_weather.h"
#include "ai_mcp_server.h"
#include "app_weather.h"

/***********************************************************
 ************************ macro define **********************
 ***********************************************************/
#define TAG "app_weather"

/***********************************************************
 ********************** function define ********************
 ***********************************************************/

/* Map Tuya weather codes to human-readable English descriptions */
static const char *__weather_code_to_desc(int code)
{
    switch (code) {
    case TW_WEATHER_SUNNY:                  return "Sunny";
    case TW_WEATHER_MOSTLY_CLEAR:           return "Mostly Clear";
    case TW_WEATHER_CLEAR:                  return "Clear";
    case TW_WEATHER_PARTLY_CLOUDY:          return "Partly Cloudy";
    case TW_WEATHER_CLOUDY:                 return "Cloudy";
    case TW_WEATHER_OVERCAST:               return "Overcast";
    case TW_WEATHER_FOG:                    return "Foggy";
    case TW_WEATHER_FREEZING_FOG:           return "Freezing Fog";
    case TW_WEATHER_HAZE:                   return "Hazy";
    case TW_WEATHER_LIGHT_RAIN:             return "Light Rain";
    case TW_WEATHER_MODERATE_RAIN:          return "Moderate Rain";
    case TW_WEATHER_RAIN:                   return "Rain";
    case TW_WEATHER_HEAVY_RAIN:             return "Heavy Rain";
    case TW_WEATHER_LIGHT_TO_MODERATE_RAIN: return "Light to Moderate Rain";
    case TW_WEATHER_MODERATE_TO_HEAVY_RAIN: return "Moderate to Heavy Rain";
    case TW_WEATHER_RAINSTORM:              return "Rainstorm";
    case TW_WEATHER_HEAVY_RAIN_TO_RAINSTORM:return "Heavy Rain to Rainstorm";
    case TW_WEATHER_EXTREME_RAINSTORM:      return "Extreme Rainstorm";
    case TW_WEATHER_DOWNPOUR:               return "Downpour";
    case TW_WEATHER_SHOWER:                 return "Shower";
    case TW_WEATHER_HEAVY_SHOWER:           return "Heavy Shower";
    case TW_WEATHER_ISOLATED_SHOWER:        return "Isolated Shower";
    case TW_WEATHER_LIGHT_SHOWER:           return "Light Shower";
    case TW_WEATHER_THUNDERSHOWER:          return "Thundershower";
    case TW_WEATHER_THUNDER_AND_LIGHTNING:  return "Thunder and Lightning";
    case TW_WEATHER_THUNDERSTORM:           return "Thunderstorm";
    case TW_WEATHER_THUNDERSHOWER_AND_HAIL: return "Thundershower with Hail";
    case TW_WEATHER_HAIL:                   return "Hail";
    case TW_WEATHER_SLEET:                  return "Sleet";
    case TW_WEATHER_FREEZING_RAIN:          return "Freezing Rain";
    case TW_WEATHER_LIGHT_SNOW:             return "Light Snow";
    case TW_WEATHER_MODERATE_SNOW:          return "Moderate Snow";
    case TW_WEATHER_SNOW:                   return "Snow";
    case TW_WEATHER_HEAVY_SNOW:             return "Heavy Snow";
    case TW_WEATHER_BLIZZARD:               return "Blizzard";
    case TW_WEATHER_LIGHT_TO_MODERATE_SNOW: return "Light to Moderate Snow";
    case TW_WEATHER_LIGHT_SNOW_SHOWER:      return "Light Snow Shower";
    case TW_WEATHER_SNOW_SHOWER:            return "Snow Shower";
    case TW_WEATHER_NEEDLE_ICE:             return "Needle Ice";
    case TW_WEATHER_ICE_PELLETS:            return "Ice Pellets";
    case TW_WEATHER_DUST:                   return "Dusty";
    case TW_WEATHER_DUST_DEVIL:             return "Dust Devil";
    case TW_WEATHER_SANDSTORM:              return "Sandstorm";
    case TW_WEATHER_STRONG_SANDSTORM:       return "Strong Sandstorm";
    case TW_WEATHER_SAND_BLOWING:           return "Blowing Sand";
    default:                                return "Unknown";
    }
}

/* MCP tool callback: retrieve current weather conditions */
static OPERATE_RET __weather_get_current(const MCP_PROPERTY_LIST_T *properties,
                                          MCP_RETURN_VALUE_T *ret_val,
                                          void *user_data)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        PR_ERR("[%s] create json failed", TAG);
        return OPRT_MALLOC_FAILED;
    }

    if (!tuya_weather_allow_update()) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Weather service unavailable. Device may not be activated or network is disconnected.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    WEATHER_CURRENT_CONDITIONS_T cond = {0};
    if (OPRT_OK != tuya_weather_get_current_conditions(&cond)) {
        cJSON_AddBoolToObject(json, "success", FALSE);
        cJSON_AddStringToObject(json, "error", "Failed to retrieve weather data from cloud.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    int high_temp = 0, low_temp = 0;
    tuya_weather_get_today_high_low_temp(&high_temp, &low_temp);

    char city[64] = {0}, province[64] = {0}, area[64] = {0};
    tuya_weather_get_city(province, sizeof(province), city, sizeof(city), area, sizeof(area));

    char location[128] = {0};
    if (city[0] != '\0') {
        if (area[0] != '\0') {
            snprintf(location, sizeof(location), "%s, %s", city, area);
        } else {
            snprintf(location, sizeof(location), "%s", city);
        }
    } else {
        snprintf(location, sizeof(location), "Unknown Location");
    }

    cJSON_AddBoolToObject(json, "success", TRUE);
    cJSON_AddStringToObject(json, "weather_desc", __weather_code_to_desc(cond.weather));
    cJSON_AddNumberToObject(json, "weather_code",  cond.weather);
    cJSON_AddNumberToObject(json, "temperature",   cond.temp);
    cJSON_AddNumberToObject(json, "humidity",       cond.humi);
    cJSON_AddNumberToObject(json, "real_feel",      cond.real_feel);
    cJSON_AddNumberToObject(json, "pressure_mbar",  cond.mbar);
    cJSON_AddNumberToObject(json, "uv_index",       cond.uvi);
    cJSON_AddNumberToObject(json, "high_temp",      high_temp);
    cJSON_AddNumberToObject(json, "low_temp",       low_temp);
    cJSON_AddStringToObject(json, "location",       location);

    ai_mcp_return_value_set_json(ret_val, json);
    PR_DEBUG("[%s] weather query success: %s %d°C", TAG, __weather_code_to_desc(cond.weather), cond.temp);
    return OPRT_OK;
}

/* MQTT-connected event callback: register MCP tool once server is ready */
static OPERATE_RET __weather_mcp_register(void *data)
{
    OPERATE_RET rt = AI_MCP_TOOL_ADD(
        "weather_get_current",
        "Get current weather conditions for the device location.\n"
        "Returns: weather description, temperature (Celsius), humidity (%),\n"
        "real feel temperature, atmospheric pressure (mbar), UV index,\n"
        "today's high/low temperatures, and location name.",
        __weather_get_current,
        NULL
    );
    if (OPRT_OK != rt) {
        PR_ERR("[%s] register MCP tool failed, rt:%d", TAG, rt);
    } else {
        PR_DEBUG("[%s] MCP tool registered: weather_get_current", TAG);
    }
    return rt;
}

OPERATE_RET app_weather_init(void)
{
    return tal_event_subscribe(EVENT_MQTT_CONNECTED, "app_weather_init",
                               __weather_mcp_register, SUBSCRIBE_TYPE_ONETIME);
}
