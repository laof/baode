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
#include "esp_wifi.h"
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

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  /* 降射频功率以省电；信号差时改回 WIFI_POWER_11dBm */
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
  /* 一次性等待，不要在紧循环里反复调用 getLocalTime()
     新版 Arduino-ESP32/LwIP 会触发 "Required to lock TCPIP core functionality" 断言 */
  if (!getLocalTime(&tm_now, NTP_TIMEOUT_MS)) {
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

/* ===== Open-Meteo 天气：今天来自 current，未来 5 天来自 daily[1..5] ===== */

/* WMO weather code -> 5 类：0=晴 1=多云 2=阴/雾 3=雨/雷 4=雪 */
static int mapWmoToCode(int wmo) {
  if (wmo == 0) return 0;
  if (wmo == 1 || wmo == 2) return 1;
  if (wmo == 3 || wmo == 45 || wmo == 48) return 2;
  if ((wmo >= 51 && wmo <= 67)
   || (wmo >= 80 && wmo <= 82)
   || (wmo >= 95 && wmo <= 99)) return 3;
  if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) return 4;
  return 2;
}

static int roundToInt(double x) {
  return (int)((x >= 0) ? (x + 0.5) : (x - 0.5));
}

static int clampTemp(int v) {
  if (v < -99) return -99;
  if (v >  99) return  99;
  return v;
}

static bool httpsGet(const String &url, String &out) {
  /* 改走明文 HTTP，省掉 TLS 握手的 CPU/RF 占用；Open-Meteo 同时支持 http:// 与 https:// */
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[HTTP] %s -> %d\n", url.c_str(), code);
    http.end();
    return false;
  }
  out = http.getString();
  http.end();
  return true;
}

/* 在 JSON 文本中查找 "key": 后的下一个数值；不做完整 JSON 解析，仅适用于 Open-Meteo 这种简单结构 */
static bool jsonFindNumber(const String &s, int from, const char *key, double *out) {
  int k = s.indexOf(key, from);
  if (k < 0) return false;
  int colon = s.indexOf(':', k + (int)strlen(key));
  if (colon < 0) return false;
  int i = colon + 1;
  int n = s.length();
  while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
  int start = i;
  if (i < n && (s[i] == '-' || s[i] == '+')) i++;
  while (i < n && (isDigit(s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
  if (i == start) return false;
  *out = atof(s.substring(start, i).c_str());
  return true;
}

/* 解析 "key":[v0,v1,v2,...] 形式的数值数组，最多取 max_n 个 */
static int jsonFindArray(const String &s, int from, const char *key, double *out, int max_n) {
  int k = s.indexOf(key, from);
  if (k < 0) return 0;
  int lb = s.indexOf('[', k);
  if (lb < 0) return 0;
  int rb = s.indexOf(']', lb);
  if (rb < 0) return 0;
  int n = 0;
  int i = lb + 1;
  while (i < rb && n < max_n) {
    while (i < rb && (s[i] == ' ' || s[i] == ',' || s[i] == '\t' || s[i] == '\n')) i++;
    int start = i;
    if (i < rb && (s[i] == '-' || s[i] == '+')) i++;
    while (i < rb && (isDigit(s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
    if (i == start) break;
    out[n++] = atof(s.substring(start, i).c_str());
  }
  return n;
}

/* 地理坐标缓存：跨 deep sleep 保留，避免每次都走 geocoding */
RTC_DATA_ATTR static double s_cached_lat = 0.0;
RTC_DATA_ATTR static double s_cached_lon = 0.0;
RTC_DATA_ATTR static bool   s_cached_geo = false;

static bool resolveCityCoords(double *lat, double *lon) {
  if (s_cached_geo) {
    *lat = s_cached_lat;
    *lon = s_cached_lon;
    return true;
  }
  String city = String(WEATHER_CITY);
  city.replace(" ", "+");
  String url = "http://geocoding-api.open-meteo.com/v1/search?name=" + city
             + "&count=1&language=en&format=json";
  String body;
  if (!httpsGet(url, body)) return false;
  int rk = body.indexOf("\"results\"");
  if (rk < 0) {
    Serial.println("[GEO] no results");
    return false;
  }
  if (!jsonFindNumber(body, rk, "\"latitude\"", lat))  return false;
  if (!jsonFindNumber(body, rk, "\"longitude\"", lon)) return false;
  s_cached_lat = *lat;
  s_cached_lon = *lon;
  s_cached_geo = true;
  Serial.printf("[GEO] %s -> %.4f,%.4f\n", WEATHER_CITY, *lat, *lon);
  return true;
}

/* 拉一次 Open-Meteo：发送 W:<code>,<temp>（今天）和 F:c,t;c,t;c,t;c,t;c,t（未来 5 天）*/
static bool fetchWeather() {
  double lat = 0, lon = 0;
  if (!resolveCityCoords(&lat, &lon)) {
    Serial.println("[WX] geocode FAIL");
    return false;
  }

  char url[256];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=weather_code,temperature_2m"
           "&daily=weather_code,temperature_2m_max"
           "&forecast_days=6&timezone=auto",
           lat, lon);

  String body;
  if (!httpsGet(String(url), body)) {
    Serial.println("[WX] open-meteo FAIL");
    return false;
  }

  /* current */
  int cur_idx = body.indexOf("\"current\"");
  if (cur_idx < 0) { Serial.println("[WX] no current"); return false; }
  double cur_code = 0, cur_temp = 0;
  if (!jsonFindNumber(body, cur_idx, "\"weather_code\"",   &cur_code)) return false;
  if (!jsonFindNumber(body, cur_idx, "\"temperature_2m\"", &cur_temp)) return false;
  int today_code = mapWmoToCode((int)cur_code);
  int today_t    = clampTemp(roundToInt(cur_temp));

  /* daily 数组（包含今天 + 未来 5 天，共 6 项） */
  int daily_idx = body.indexOf("\"daily\"");
  if (daily_idx < 0) { Serial.println("[WX] no daily"); return false; }
  double dcodes[8] = {0}, dtemps[8] = {0};
  int nc = jsonFindArray(body, daily_idx, "\"weather_code\"",       dcodes, 8);
  int nt = jsonFindArray(body, daily_idx, "\"temperature_2m_max\"", dtemps, 8);
  if (nc < 6 || nt < 6) {
    Serial.printf("[WX] daily too short: nc=%d nt=%d\n", nc, nt);
    return false;
  }

  /* 发送 W: 今天（current） */
  {
    char buf[24];
    int n = snprintf(buf, sizeof(buf), "W:%d,%d\n", today_code, today_t);
    Serial1.write((const uint8_t *)buf, n);
    Serial1.flush();
    Serial.printf("[TX] %s", buf);
  }

  /* 发送 F: 未来 5 天（daily[1..5]） */
  {
    char buf[64];
    int p = 0;
    p += snprintf(buf + p, sizeof(buf) - p, "F:");
    for (int i = 1; i <= 5; i++) {
      int c = mapWmoToCode((int)dcodes[i]);
      int t = clampTemp(roundToInt(dtemps[i]));
      p += snprintf(buf + p, sizeof(buf) - p, "%s%d,%d", (i == 1) ? "" : ";", c, t);
    }
    p += snprintf(buf + p, sizeof(buf) - p, "\n");
    Serial1.write((const uint8_t *)buf, p);
    Serial1.flush();
    Serial.printf("[TX] %s", buf);
  }
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
  esp_wifi_stop();                                              /* 彻底关闭射频，避免 sleep 前残留功耗 */
  esp_sleep_enable_timer_wakeup(3ULL * 3600ULL * 1000000ULL);  /* 3h */
  esp_deep_sleep_start();
}

void loop() {
  /* 不会执行到这里：setup 已进入 deep sleep */
}
