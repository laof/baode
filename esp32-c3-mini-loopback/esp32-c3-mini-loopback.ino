/*
 * 临时 UART 回环测试：不联 Wi-Fi、不做 NTP，
 * 每 1 秒发一帧合法格式的"假时间"，让 STM32 直接显示。
 *
 * 发送的内容（秒位每次 +1）:
 *   T:2099-01-01 00:00:00
 *   T:2099-01-01 00:00:01
 *   ...
 *
 * 用年份 2039 作标志：屏上出现 2039-01-01 → UART 链路 OK。
 */

static const int UART_TX_PIN = 4;
static const int UART_RX_PIN = 5;
static const uint32_t UART_BAUD = 115200;

static uint32_t counter = 0;
static uint32_t last_send_ms = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32-C3 UART LOOPBACK TEST ===");

  Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
}

void loop() {
  if (millis() - last_send_ms >= 1000) {
    last_send_ms = millis();

    uint32_t total = counter;
    uint8_t  ss = total % 60;
    uint8_t  mm = (total / 60) % 60;
    uint8_t  hh = (total / 3600) % 24;
    counter++;

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "T:2039-01-01 %02u:%02u:%02u\n",
                     hh, mm, ss);
    Serial1.write((const uint8_t *)buf, n);
    Serial1.flush();

    Serial.printf("[TX] %s", buf);
  }
}
