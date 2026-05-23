[English](./README.md) | 简体中文

# your_life_companion

[your_life_companion](https://github.com/tuya/TuyaOpen/tree/master/apps/tuya.ai/your_life_companion) 是基于 tuya.ai 和 T5-AI Board 的智能生活伴侣。在 your_desk_emoji 基础上扩展，结合 AI 对话、情绪感知、天气/日历提醒、表情显示，打造一个有温度、有陪伴感的桌面生活伴侣。

## 支持功能

1. **AI 智能对话**
   - 多种对话模式：按键对话、语音唤醒、自由对话
   - 支持不同唤醒词
   - LCD 屏幕实时聊天内容显示

2. **手势识别**
   - 基于 PAJ7620 传感器
   - 支持方向手势（上、下、左、右、前、后）
   - 手势触发表情变化

3. **舵机电机控制**
   - 双舵机控制（垂直和水平运动）
   - 平滑运动动画
   - 基于手势和对话的互动动作

4. **表情显示系统**
   - 多种表情动画
   - 眨眼动画增强互动效果
   - 基于用户交互的实时表情切换

5. **天气时钟显示**
   - 从涂鸦云获取实时天气信息
   - 时间同步和显示
   - LCD 显示日期和天气信息

6. **显示界面**
   - 多种 UI 风格：微信式、聊天机器人式、OLED
   - 实时聊天内容显示
   - 天气和时间信息显示

7. **网络配置**
   - 蓝牙网络配置
   - WiFi 连接管理
   - 网络状态监控

## 依赖硬件能力

1. **音频系统**
   - 音频采集能力
   - 音频播放能力
   - 喇叭控制

2. **传感器接口**
   - I2C 接口用于 PAJ7620 手势传感器
   - GPIO 用于手势传感器中断

3. **电机控制**
   - PWM 接口用于舵机电机控制
   - 双舵机支持（头部和身体运动）

4. **显示接口**
   - LCD 显示用于表情动画和信息
   - 支持不同屏幕尺寸和类型

5. **用户界面**
   - 按键用于对话控制
   - LED 指示灯用于状态显示

## 已支持硬件

| 型号 | 配置文件 | 说明 | 重置方式 |
| --- | --- | --- | ----- |
| T5AI Mini (ST7735S) | app_default.config | T5AI Mini 开发板，配备 ST7735S 彩色 LCD 显示屏 | 重启(按 RST 按钮) 3 次重置 |

## 编译

1. **选择配置**：根据您的硬件选择合适的配置文件。

2. **应用配置**：运行 `tos.py config choice` 命令，选择当前运行的开发板。

3. **自定义设置**：如需修改配置，请先运行 `tos.py config menu` 命令修改配置。

4. **编译工程**：运行 `tos.py build` 命令，编译工程。

## 配置说明

### 默认配置
- 随意对话模式，未开启 AEC，不支持打断
- 唤醒词：你好涂鸦

### 通用配置

#### 对话模式选择

- **长按对话模式**
  - `ENABLE_CHAT_MODE_KEY_PRESS_HOLD_SINGEL`：按住按键后说话，一句话说完后松开按键

- **按键对话模式**
  - `ENABLE_CHAT_MODE_KEY_TRIG_VAD_FREE`：按一下按键，设备会进入/退出聆听状态，支持 VAD 检测

- **唤醒对话模式**
  - `ENABLE_CHAT_MODE_ASR_WAKEUP_SINGEL`：需要说出唤醒词才能唤醒设备，进行单轮对话

- **随意对话模式**
  - `ENABLE_CHAT_MODE_ASR_WAKEUP_FREE`：需要说出唤醒词才能唤醒设备，支持连续对话（30秒超时）

#### 唤醒词选择

| 宏 | 类型 | 说明 |
| --- | --- | --- |
| `ENABLE_WAKEUP_KEYWORD_NIHAO_TUYA` | 布尔 | 唤醒词是 "你好涂鸦" |
| `ENABLE_WAKEUP_KEYWORD_NIHAO_XIAOZHI` | 布尔 | 唤醒词是 "你好小智" |
| `ENABLE_WAKEUP_KEYWORD_XIAOZHI_TONGXUE` | 布尔 | 唤醒词是 "小智同学" |
| `ENABLE_WAKEUP_KEYWORD_XIAOZHI_GUANJIA` | 布尔 | 唤醒词是 "小智管家" |

## 项目结构

```
src/
├── app_chat_bot.c          # AI 对话管理
├── app_gesture.c           # PAJ7620 手势识别
├── app_servo.c             # 舵机电机控制
├── tuya_main.c             # 主应用程序入口
└── ui/
    ├── src/
    │   ├── app_ui_main.c   # UI 主控
    │   ├── ui_clock.c      # 时钟显示
    │   └── ui_emoji.c      # 表情显示
    └── image/
        ├── emmo/           # 表情图像资源
        └── weather/        # 天气图标资源
```

## 使用方法

1. **初始设置**：配置开发板并编译项目
2. **网络配置**：使用蓝牙或按键重置进行 WiFi 设置
3. **交互使用**：使用语音唤醒或按键按压开始对话
4. **手势控制**：向不同方向挥手触发动作
5. **天气显示**：在显示屏上查看实时天气和时间信息
