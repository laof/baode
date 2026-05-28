/*
 * 副控：ESP32-C3-Pro Mini  (单次上电工作版)
 *
 * 工作模型（配合主控 STM32L443 的电源管理）：
 *   1. 主控通过三极管把 ESP32 模组上电
 *   2. ESP32 开机 -> 连 Wi-Fi -> 上报 Wi-Fi 状态 (S:W / S:N)
 *   3. 成功联网 -> NTP 校时 -> 发一帧 T:YYYY-MM-DD HH:MM:SS
 *   4. 拉一次天气 -> 发一帧 W:<code>,<temp>
 *   5. 不论成败，最后发 "D\n" 表示本轮会话结束
 *   6. 进入 deep sleep / 空转，等待主控断电
 *
 * 接线：
 *   ESP32 GPIO4 (Serial1 TX) ──► STM32 PA3 (USART2_RX)   必接
 *   ESP32 GPIO5 (Serial1 RX) ──► STM32 PA2 (USART2_TX)   可选，本固件不用
 *   ESP32 GND                ──► STM32 GND               共地，必接
 *   ESP32 模组 VCC           ──► 高边开关（PNP/PMOS）由 STM32 PB1 控制
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <time.h>
#include "esp_sleep.h"
#include "secrets.h"

static const char *NTP_1 = "ntp.aliyun.com";
static const char *NTP_2 = "cn.pool.ntp.org";
static const char *NTP_3 = "pool.ntp.org";

static const long GMT_OFFSET_SEC      = 8 * 3600;
static const int  DAYLIGHT_OFFSET_SEC = 0;

static const int UART_TX_PIN = 4;
static const int UART_RX_PIN = 5;
static const uint32_t UART_BAUD = 115200;

/* 单次会话各阶段的最长等待 */
static const uint32_t WIFI_TIMEOUT_MS = 20000;
static const uint32_t NTP_TIMEOUT_MS  = 15000;

static void sendLine(const char *s) {
  Serial1.write((const uint8_t *)s, strlen(s));
  Serial1.flush();
  Serial.print(s);
}

static bool connectWiFi() {
  Serial.printf("[WiFi] connecting to %s ...\n", WIFI_SSID);

  /* 先扫描可见的 SSID，方便诊断 */
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.printf("[Scan] %d networks found:\n", n);
  bool ssid_seen = false;
  for (int i = 0; i < n; i++) {
    int rssi = WiFi.RSSI(i);
    Serial.printf("  %2d) %-32s  RSSI=%d  ch=%d  enc=%d\n",
                  i, WiFi.SSID(i).c_str(), rssi,
                  WiFi.channel(i), WiFi.encryptionType(i));
    if (WiFi.SSID(i) == WIFI_SSID) ssid_seen = true;
  }
  Serial.printf("[Scan] target SSID '%s' %s\n",
                WIFI_SSID, ssid_seen ? "VISIBLE" : "NOT FOUND");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  wl_status_t st = WL_DISCONNECTED;
  while ((st = WiFi.status()) != WL_CONNECTED && (millis() - t0) < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.printf("  [.] status=%d  elapsed=%lums\n", (int)st, millis() - t0);
  }
  bool ok = (WiFi.status() == WL_CONNECTED);
  if (ok) {
    Serial.printf("[WiFi] OK  IP=%s  RSSI=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.printf("[WiFi] FAIL  last_status=%d\n", (int)WiFi.status());
  }
  sendLine(ok ? "S:W\n" : "S:N\n");
  return ok;
}

static bool syncAndSendTime() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_1, NTP_2, NTP_3);
  struct tm tm_now;
  uint32_t t0 = millis();
  while (!getLocalTime(&tm_now, 200) && (millis() - t0) < NTP_TIMEOUT_MS) {}
  if (!getLocalTime(&tm_now, 200)) {
    Serial.println("[NTP] FAIL");
    return false;
  }
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "T:%04d-%02d-%02d %02d:%02d:%02d\n",
                   tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                   tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
  Serial1.write((const uint8_t *)buf, n);
  Serial1.flush();
  Serial.printf("[TX] %s", buf);
  return true;
}

static int classifyCondition(const String &cond) {
  String c = cond; c.toLowerCase();
  if (c.indexOf("thunder") >= 0 || c.indexOf("rain")   >= 0
   || c.indexOf("drizzle") >= 0 || c.indexOf("shower") >= 0) return 3;
  if (c.indexOf("snow")    >= 0 || c.indexOf("sleet")  >= 0
   || c.indexOf("blizzard")>= 0 || c.indexOf("ice")    >= 0) return 4;
  if (c.indexOf("partly")  >= 0) return 1;
  if (c.indexOf("sunny")   >= 0 || c.indexOf("clear")  >= 0) return 0;
  return 2;
}

static int parseSignedTemp(const String &s) {
  int sign = 1, i = 0, n = s.length();
  while (i < n && (s[i] == ' ' || s[i] == '+')) i++;
  if (i < n && s[i] == '-') { sign = -1; i++; }
  int v = 0; bool got = false;
  while (i < n && isDigit(s[i])) { v = v * 10 + (s[i] - '0'); got = true; i++; }
  return got ? sign * v : 0;
}

static bool fetchWeather() {
  String city = String(WEATHER_CITY);
  city.replace(" ", "+");
  String url = "http://wttr.in/" + city + "?format=%C|%t&m";

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  String body = http.getString();
  http.end();
  body.trim();
  Serial.printf("[WX] %s\n", body.c_str());

  int bar = body.indexOf('|');
  if (bar <= 0) return false;
  int wcode = classifyCondition(body.substring(0, bar));
  int wtemp = parseSignedTemp(body.substring(bar + 1));
  if (wtemp < -99) wtemp = -99;
  if (wtemp >  99) wtemp =  99;

  char out[24];
  int n = snprintf(out, sizeof(out), "W:%d,%d\n", wcode, wtemp);
  Serial1.write((const uint8_t *)out, n);
  Serial1.flush();
  Serial.printf("[TX] %s", out);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32-C3 one-shot session ===");

  Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  if (connectWiFi()) {
    syncAndSendTime();
    fetchWeather();
  }

  /* 通知主控本次会话结束 */
  sendLine("D\n");

  /* 进入深度睡眠：当前无硬件电源开关，靠定时唤醒做 3 小时自循环。
   * 以后若接上主控控制的电源开关（方案 A），把 esp_sleep_enable_timer_wakeup
   * 这行删掉即可改为"永远睡，靠主控切电唤醒"。 */
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup(3ULL * 3600ULL * 1000000ULL);  /* 3h */
  esp_deep_sleep_start();
}

void loop() {
  /* 不会执行到这里：setup 已进入 deep sleep */
}
