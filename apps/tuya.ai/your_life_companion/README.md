English | [简体中文](./README_zh.md)

# your_life_companion

[your_life_companion](https://github.com/tuya/TuyaOpen/tree/master/apps/tuya.ai/your_life_companion) is an AI-powered smart life companion based on tuya.ai and the T5-AI Board. Built on top of your_desk_emoji, it combines AI conversation, emotion awareness, weather/clock display, and emoji expressions to create a warm and interactive desktop companion.

## Supported Features

1. **AI Intelligent Conversation**
   - Multiple conversation modes: button press, voice wake-up, free conversation
   - Support for different wake-up keywords
   - Real-time chat display on LCD screen

2. **Gesture Recognition**
   - Based on PAJ7620 sensor
   - Supports directional gestures (up, down, left, right, forward, backward)
   - Gesture-triggered emoji expressions

3. **Servo Motor Control**
   - Dual servo control (vertical and horizontal movement)
   - Smooth movement animations
   - Interactive actions triggered by gestures and conversations

4. **Emoji Display System**
   - Multiple emoji expressions with animations
   - Blink animations for enhanced interaction
   - Real-time emoji switching based on user interactions

5. **Weather Clock Display**
   - Real-time weather information from Tuya Cloud
   - Time synchronization and display
   - Date and weather information on LCD

6. **Display Interface**
   - Multiple UI styles: WeChat-like, Chatbot, OLED
   - Real-time chat content display
   - Weather and time information display

7. **Network Configuration**
   - Bluetooth network configuration
   - WiFi connection management
   - Network status monitoring

## Hardware Dependencies

1. **Audio System**
   - Audio capture capability
   - Audio playback capability
   - Speaker control

2. **Sensor Interface**
   - I2C interface for PAJ7620 gesture sensor
   - GPIO for gesture sensor interrupt

3. **Motor Control**
   - PWM interface for servo motor control
   - Dual servo support (head and body movement)

4. **Display Interface**
   - LCD display for emoji animations and information
   - Support for different screen sizes and types

5. **User Interface**
   - Button for conversation control
   - LED indicator for status display

## Supported Hardware

| Model | Config File | Description | Reset Method |
| --- | --- | --- | --- |
| T5AI Mini (ST7735S) | app_default.config | T5AI Mini with ST7735S color LCD display | Restart 3 times (press RST button) |

## Compilation

1. **Select Configuration**: Choose the appropriate config file based on your hardware.

2. **Apply Configuration**: Run `tos.py config choice` command to select the current development board.

3. **Customize Settings**: If you need to modify the configuration, run `tos.py config menu` command to make changes.

4. **Build Project**: Run `tos.py build` command to compile the project.

## Configuration

### Default Configuration
- Free conversation mode, AEC disabled, no interruption support
- Wake-up keyword: "你好涂鸦" (Hello Tuya)

### General Configuration

#### Conversation Modes

- **Press and Hold Mode**
  - `ENABLE_CHAT_MODE_KEY_PRESS_HOLD_SINGEL`: Press and hold button to speak, release after finishing

- **Button Trigger Mode**
  - `ENABLE_CHAT_MODE_KEY_TRIG_VAD_FREE`: Press button to enter/exit listening state with VAD detection

- **Wake-up Single Mode**
  - `ENABLE_CHAT_MODE_ASR_WAKEUP_SINGEL`: Say wake-up word to start single conversation

- **Wake-up Free Mode**
  - `ENABLE_CHAT_MODE_ASR_WAKEUP_FREE`: Say wake-up word for continuous conversation (30s timeout)

#### Wake-up Keywords

| Macro | Type | Description |
| --- | --- | --- |
| `ENABLE_WAKEUP_KEYWORD_NIHAO_TUYA` | Boolean | Wake-up word: "你好涂鸦" |
| `ENABLE_WAKEUP_KEYWORD_NIHAO_XIAOZHI` | Boolean | Wake-up word: "你好小智" |
| `ENABLE_WAKEUP_KEYWORD_XIAOZHI_TONGXUE` | Boolean | Wake-up word: "小智同学" |
| `ENABLE_WAKEUP_KEYWORD_XIAOZHI_GUANJIA` | Boolean | Wake-up word: "小智管家" |

## Project Structure

```
src/
├── app_chat_bot.c          # AI conversation management
├── app_gesture.c           # PAJ7620 gesture recognition
├── app_servo.c             # Servo motor control
├── tuya_main.c             # Main application entry
└── ui/
    ├── src/
    │   ├── app_ui_main.c   # UI main controller
    │   ├── ui_clock.c      # Clock display
    │   └── ui_emoji.c      # Emoji display
    └── image/
        ├── emmo/           # Emoji image resources
        └── weather/        # Weather icon resources
```

## Usage

1. **Initial Setup**: Configure the development board and compile the project
2. **Network Configuration**: Use Bluetooth or button reset for WiFi setup
3. **Interaction**: Use voice wake-up or button press to start conversations
4. **Gesture Control**: Wave hands in different directions to trigger actions
5. **Weather Display**: View real-time weather and time information on the display
