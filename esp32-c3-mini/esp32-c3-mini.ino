/*
 * 副控：ESP32-C3-Pro Mini
 *  - 联 Wi-Fi
 *  - NTP 同步时间（北京时间 UTC+8）
 *  - 通过 Serial1 (GPIO4=TX, GPIO5=RX) 以 115200 8N1
 *    每 30 秒发送一帧 "T:HH:MM\n" 给主控 STM32L443
 *
 * 接线：
 *   ESP32 GPIO4 (TX) ──► STM32 PA3 (USART2_RX)
 *   ESP32 GPIO5 (RX) ──► STM32 PA2 (USART2_TX)   (预留)
 *   ESP32 GND        ──► STM32 GND
 */

#include <WiFi.h>
#include <time.h>
#include "secrets.h"   // Wi-Fi 凭据，仅本地，.gitignore 已排除

/* NTP 服务器（国内推荐）*/
static const char *NTP_1 = "ntp.aliyun.com";
static const char *NTP_2 = "cn.pool.ntp.org";
static const char *NTP_3 = "pool.ntp.org";

/* 时区：北京时间 UTC+8 */
static const long  GMT_OFFSET_SEC      = 8 * 3600;
static const int   DAYLIGHT_OFFSET_SEC = 0;

/* UART1 引脚（连 STM32 USART2）*/
static const int UART_TX_PIN = 4;   // GPIO4 -> STM32 PA3 (RX)
static const int UART_RX_PIN = 5;   // GPIO5 <- STM32 PA2 (TX)
static const uint32_t UART_BAUD = 115200;

/* 发送周期 */
static const uint32_t SEND_INTERVAL_MS = 10 * 1000;  // 10s 一帧，期间 STM32 本地按秒自走

static uint32_t last_send_ms = 0;
static bool     time_synced  = false;

static void connectWiFi() {
  Serial.printf("[WiFi] connecting to %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 30000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] OK, IP=%s, RSSI=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[WiFi] FAIL, will retry in loop()");
  }
}

static void syncNTP() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_1, NTP_2, NTP_3);
  Serial.println("[NTP] syncing...");

  struct tm tm_now;
  uint32_t t0 = millis();
  while (!getLocalTime(&tm_now, 200) && (millis() - t0) < 15000) {
    Serial.print('.');
  }
  Serial.println();

  if (getLocalTime(&tm_now, 200)) {
    time_synced = true;
    Serial.printf("[NTP] OK, %04d-%02d-%02d %02d:%02d:%02d\n",
                  tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                  tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
  } else {
    Serial.println("[NTP] FAIL");
  }
}

/* 发送一帧 "T:YYYY-MM-DD HH:MM:SS\n" */
static void sendTime() {
  struct tm tm_now;
  if (!getLocalTime(&tm_now, 50)) {
    Serial.println("[TX] no time yet");
    return;
  }

  char buf[32];
  int n = snprintf(buf, sizeof(buf), "T:%04d-%02d-%02d %02d:%02d:%02d\n",
                   tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                   tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
  Serial1.write((const uint8_t *)buf, n);
  Serial1.flush();

  /* 把同一帧也打到 USB 串口，便于电脑端肉眼观察 */
  Serial.printf("[TX] %s", buf);
}

void setup() {
  Serial.begin(115200);          // USB-CDC 用于调试日志
  delay(300);
  Serial.println("\n=== ESP32-C3 Time Bridge ===");

  Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    syncNTP();
  }
}

void loop() {
  /* Wi-Fi 掉线自动重连 */
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t last_retry = 0;
    if (millis() - last_retry > 10000) {
      last_retry = millis();
      Serial.println("[WiFi] reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }

  /* 首次未同步成功的兜底重试 */
  if (!time_synced) {
    syncNTP();
  }

  /* 定时发送 */
  if (time_synced && (millis() - last_send_ms >= SEND_INTERVAL_MS || last_send_ms == 0)) {
    last_send_ms = millis();
    sendTime();
  }
}
