/*
 * 副控：ESP32-C3-Pro Mini
 *  - 联 Wi-Fi
 *  - NTP 同步时间（北京时间 UTC+8）
 *  - 通过 Serial1 (GPIO4=TX, GPIO5=RX) 以 115200 8N1
 *    每 10 秒发送一帧 "T:YYYY-MM-DD HH:MM:SS\n" 给主控 STM32L443
 *
 * 接线（在原有基础上加 3 根）：
 *   ESP32 GPIO4 (Serial1 TX) ──► STM32 PA3 (USART2_RX)   时间数据，必接
 *   ESP32 GPIO5 (Serial1 RX) ──► STM32 PA2 (USART2_TX)   预留双向，可不接
 *   ESP32 GND                ──► STM32 GND               共地，必接
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
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
static const uint32_t SEND_INTERVAL_MS    = 10 * 1000;        // 10s 一帧时间
static const uint32_t WEATHER_INTERVAL_MS = 60UL * 60 * 1000; // 1h 一次天气

static uint32_t last_send_ms    = 0;
static uint32_t last_weather_ms = 0;
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

/* 把当前 Wi-Fi 状态回给主控：S:W\n 已连 / S:N\n 未连 */
static void sendWifiStatus() {
  const char *msg = (WiFi.status() == WL_CONNECTED) ? "S:W\n" : "S:N\n";
  Serial1.write((const uint8_t *)msg, strlen(msg));
  Serial1.flush();
  Serial.printf("[TX] %s", msg);
}

/* 把 wttr.in 返回的天气描述文本归类为 5 种图标代码：
 *   0=晴  1=多云  2=阴/雾  3=雨  4=雪
 * 区分不了的默认算 2（多云/阴）。*/
static int classifyCondition(const String &cond) {
  String c = cond;
  c.toLowerCase();
  if (c.indexOf("thunder") >= 0 || c.indexOf("rain")   >= 0
   || c.indexOf("drizzle") >= 0 || c.indexOf("shower") >= 0) return 3;
  if (c.indexOf("snow")    >= 0 || c.indexOf("sleet")  >= 0
   || c.indexOf("blizzard")>= 0 || c.indexOf("ice")    >= 0) return 4;
  if (c.indexOf("partly")  >= 0) return 1;
  if (c.indexOf("sunny")   >= 0 || c.indexOf("clear")  >= 0) return 0;
  return 2;  // cloudy / overcast / mist / fog / haze / 未知
}

/* 从 "+18\xc2\xb0C" 这样的串中取出有符号整数。*/
static int parseSignedTemp(const String &s) {
  int sign = 1;
  int i = 0;
  int n = (int)s.length();
  while (i < n && (s[i] == ' ' || s[i] == '+')) i++;
  if (i < n && s[i] == '-') { sign = -1; i++; }
  int v = 0;
  bool got = false;
  while (i < n && isDigit(s[i])) { v = v * 10 + (s[i] - '0'); got = true; i++; }
  return got ? sign * v : 0;
}

/* 拉一次天气，成功后向主控发一帧 "W:<code>,<temp>\n" */
static bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;

  /* 在序事代码中需要 URL-encode 的只有空格，这里简单起见把空格换成 + */
  String city = String(WEATHER_CITY);
  city.replace(" ", "+");

  String url = "http://wttr.in/" + city + "?format=%C|%t&m";

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    Serial.println("[WX] http.begin fail");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[WX] HTTP %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  body.trim();
  Serial.printf("[WX] raw: %s\n", body.c_str());

  int bar = body.indexOf('|');
  if (bar <= 0) {
    Serial.println("[WX] bad format");
    return false;
  }
  String cond = body.substring(0, bar);
  String temp = body.substring(bar + 1);

  int wcode = classifyCondition(cond);
  int wtemp = parseSignedTemp(temp);
  if (wtemp < -99) wtemp = -99;
  if (wtemp >  99) wtemp =  99;

  char out[24];
  int n = snprintf(out, sizeof(out), "W:%d,%d\n", wcode, wtemp);
  Serial1.write((const uint8_t *)out, n);
  Serial1.flush();
  Serial.printf("[TX] %s", out);
  return true;
}

/* 处理主控发来的字节，遇到 '\n' 结束一帧。
 * 目前只识别 1 个命令: "P" (ping) -> 立即回 S:W/S:N */
static void handleRxByte(uint8_t b) {
  static char     line[16];
  static uint8_t  len = 0;

  if (b == '\r') return;
  if (b == '\n') {
    line[len] = '\0';
    if (len == 1 && line[0] == 'P') {
      sendWifiStatus();
    }
    len = 0;
    return;
  }
  if (len < sizeof(line) - 1) {
    line[len++] = (char)b;
  } else {
    len = 0;  // 行过长丢弃
  }
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
  /* 处理主控发过来的 ping，立刻回 Wi-Fi 状态 */
  while (Serial1.available()) {
    handleRxByte((uint8_t)Serial1.read());
  }

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
  /* 每小时拉一次天气，首次同步成功后立刻来一次 */
  if (time_synced && (last_weather_ms == 0
        || (millis() - last_weather_ms) >= WEATHER_INTERVAL_MS)) {
    if (fetchWeather()) {
      last_weather_ms = millis();
    } else {
      /* 失败不记录，1 分钟后重试一次，避免狂重试 */
      last_weather_ms = millis() - (WEATHER_INTERVAL_MS - 60UL * 1000);
    }
  }}
