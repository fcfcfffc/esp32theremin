#include <Arduino.h>
#include "driver/pcnt.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ======= 引脚定义 =======
#define PCNT_INPUT_SIG_IO   1      // 差频输入
#define BUTTON_PIN          19      // 按钮用于设定基准频率
#define PWM_OUT_PIN         2      // 本地PWM输出
#define PCNT_UNIT           PCNT_UNIT_0

// ======= ESP-NOW 接收端 MAC 地址（替换为你的）======
uint8_t peerAddress[] = {0xF0, 0x9E, 0x9E, 0xAD, 0x77, 0x98}; // 替换为接收端 MAC 地址

typedef struct {
  uint8_t pwm;
} pwm_packet_t;

// ======= 中断和状态变量 =======
hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile int pulseCount = 0;
bool buttonPressed = false;

float smoothedFreq = 0.0;
float smoothedBaseFreq = 0.0;
float smoothedDelta = 0.0;
int baseFreq = 0;
bool baseFreqSet = false;

float lastSmoothedFreq = 0.0;  // 用于判断稳定性
const float stableEpsilon = 2;  // Hz，变化阈值，低于认为稳定
int stableCount = 0;
const int stableThreshold = 20;  // 稳定计数达到多少次，认为真的稳定（比如10次循环）

bool autoSetBase = true; // 是否启用自动基准设定

// ======= 定时器中断读取频率 =======
void IRAM_ATTR onTimer() {
  int16_t count;
  pcnt_get_counter_value(PCNT_UNIT, &count);
  portENTER_CRITICAL_ISR(&timerMux);
  pulseCount = count;
  portEXIT_CRITICAL_ISR(&timerMux);
  pcnt_counter_clear(PCNT_UNIT);
}

// ======= 按钮中断 =======
void IRAM_ATTR onButton() {
  buttonPressed = true;
}

// ======= 初始化 PCNT 差频输入 =======
void setupPCNT() {
  pcnt_config_t pcnt_config;
  pcnt_config.pulse_gpio_num = PCNT_INPUT_SIG_IO;
  pcnt_config.ctrl_gpio_num = PCNT_PIN_NOT_USED;
  pcnt_config.unit = PCNT_UNIT;
  pcnt_config.channel = PCNT_CHANNEL_0;
  pcnt_config.pos_mode = PCNT_COUNT_INC;
  pcnt_config.neg_mode = PCNT_COUNT_DIS;
  pcnt_config.lctrl_mode = PCNT_MODE_KEEP;
  pcnt_config.hctrl_mode = PCNT_MODE_KEEP;
  pcnt_config.counter_h_lim = 32767;
  pcnt_config.counter_l_lim = -32767;

  pcnt_unit_config(&pcnt_config);
  pcnt_counter_pause(PCNT_UNIT);
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);
}

// ======= 初始化定时器：200ms =======
void setupTimer() {
  timer = timerBegin(0, 80, true);      // 80 MHz / 80 = 1 MHz
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 50000, true); // 200 ms
  timerAlarmEnable(timer);
}

// ======= 初始化 PWM 输出 =======
void setupPWM() {
  ledcAttachPin(PWM_OUT_PIN, 0);
  ledcSetup(0, 1000, 8);  // 1kHz, 8-bit
  ledcWrite(0, 0);        // 初始为 0
}

// ======= 初始化按钮 =======
void setupButton() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(BUTTON_PIN, onButton, FALLING);
}

// ======= ESP-NOW 发送回调 =======
void onSent(const uint8_t *mac, esp_now_send_status_t status) {
  // 发送状态打印可以取消注释调试
  // Serial.print("📡 发送状态：");
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "成功" : "失败");
}

// ======= 初始化 ESP-NOW =======
void setupESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW 初始化失败");
    return;
  }

  esp_now_register_send_cb(onSent);

esp_now_peer_info_t peerInfo = {};
memcpy(peerInfo.peer_addr, peerAddress, 6);
peerInfo.channel = 1;  // 与接收端一致
peerInfo.encrypt = false;

  if (!esp_now_add_peer(&peerInfo)) {
    // Serial.println("✅ 添加 ESP-NOW 接收端成功");
  }
}

// ======= setup() =======
void setup() {
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  setupPCNT();
  setupTimer();
  setupPWM();
  setupButton();
  setupESPNow();
  // Serial.println("✅ ESP32-S3 Theremin 发送端已启动");
}

// ======= loop() =======
void loop() {
  delay(20); // 增加刷新率

  int currentFreq;
  portENTER_CRITICAL(&timerMux);
  currentFreq = pulseCount;
  portEXIT_CRITICAL(&timerMux);

  // 平滑当前频率
  float alphaFreq = 0.5;
  smoothedFreq = alphaFreq * currentFreq + (1 - alphaFreq) * smoothedFreq;

    // 判断稳定性：比较 smoothedFreq 和上一次的差值，且 smoothedDelta <= 4 时不增加计数
  float freqChange = fabs(smoothedFreq - lastSmoothedFreq);
  bool isStable = false;

  if (smoothedDelta >= 4) {
    // smoothedDelta >= 4 时，不增加 stableCount，重置计数
    stableCount = 0;
  } else if (freqChange < stableEpsilon) {
    stableCount++;
    if (stableCount >= stableThreshold) {
      isStable = true;
    }
  } else {
    stableCount = 0;
  }

  lastSmoothedFreq = smoothedFreq;

  // 按钮按下立即设定基准频率
  if (buttonPressed) {
    baseFreq = smoothedFreq;
    smoothedBaseFreq = smoothedFreq;
    baseFreqSet = true;
    buttonPressed = false;
    stableCount = 0; // 重置稳定计数
   // Serial.printf("🎯 设置基准频率为 %.1f Hz\n", smoothedFreq);
  }

  // 只有当基准已设且稳定计数达到阈值时，才缓慢更新基准频率（避免温度漂移导致偏差）
 // if (baseFreqSet && stableCount >= stableThreshold) {
 //   float alphaBase = 0.01; // 更新速度非常慢
 //   smoothedBaseFreq = alphaBase * smoothedFreq + (1 - alphaBase) * smoothedBaseFreq;
 // }

  // 加上“自动基准设定”功能，如果稳定且启用自动基准设定，则直接设定基准频率为当前值
  if (isStable && autoSetBase) {
    smoothedBaseFreq = smoothedFreq;
  //  Serial.printf("🔄 自动设定新基准为 %.1f Hz\n", smoothedFreq);
  }

  // 计算频率差并平滑
  float delta = smoothedBaseFreq - smoothedFreq;
  if (delta < 0) delta = 0;
  float alphaDelta = 0.6;
  smoothedDelta = alphaDelta * delta + (1 - alphaDelta) * smoothedDelta;

  // 映射到 PWM（0~255）
  int duty = map(smoothedDelta, 4, 20, 0, 255);
  duty = constrain(duty, 0, 255);
  ledcWrite(0, duty); // 本地输出

  // 发送给接收端
  pwm_packet_t packet;
  packet.pwm = duty;
  esp_now_send(peerAddress, (uint8_t *)&packet, sizeof(packet));

  // 串口打印监视
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 100) {
    Serial.printf("Freq: %.1f Hz | Base: %.1f Hz | Δf: %.1f | PWM: %d | StableCnt: %d | AutoSet: %s\n",
                  smoothedFreq, smoothedBaseFreq, smoothedDelta, duty, stableCount, autoSetBase ? "ON" : "OFF");
    lastPrint = millis();
  }
}
