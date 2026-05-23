English | [简体中文](./README_zh.md)

# your_life_companion
[your_life_companion](https://github.com/tuya/TuyaOpen/tree/master/apps/tuya.ai/your_life_companion) is an AI-powered smart life companion based on tuya.ai and the T5-AI Board. Built on top of your_chat_bot, it extends the base with emotion awareness, life reminders, and personalized companionship to create a warm desktop AI companion.

**Note: Switching between TUYA AI V1.0 and V2.0 requires removing the device and clearing the data on the APP before use.**

## Supported Features

1. AI intelligent conversation
2. Button wake-up/Voice wake-up, turn-based dialogue, supports voice interruption (hardware support required)
3. Expression display
4. Supports LCD for displaying real-time chat content and supports viewing chat content in real-time on the APP side
5. Quick Bluetooth network connection to the router
6. Real-time switching of AI entity roles on the APP side


![](../../../docs/images/apps/your_chat_bot.png)

## Hardware Dependencies
1. Audio capture
2. Audio playback

## Supported Hardware
| Model | Description | Reset Method |
| --- | --- | --- |
| TUYA T5AI_Board Development Board | [https://developer.tuya.com/en/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj](https://developer.tuya.com/en/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj) | Reset by restarting 3 times |
| TUYA T5AI_EVB Board | [https://oshwhub.com/flyingcys/t5ai_evb](https://oshwhub.com/flyingcys/t5ai_evb) | Reset by restarting 3 times |

## Compilation
1. Run the `tos.py config choice` command to select the current development board in use.
2. If you need to modify the configuration, run the `tos.py config menu` command to make changes.
3. Run the `tos.py build` command to compile the project.
