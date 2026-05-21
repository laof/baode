Arduino IDE‌


Connected to ESP32-C3 on COM6:
Chip type:          ESP32-C3 (QFN32) (revision v0.4)
Features:           Wi-Fi, BT 5 (LE), Single Core, 160MHz, Embedded Flash 4MB (XMC)
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                10:00:3b:00:ce:e8

此为副控。主控是STM32L431。副控可以联网查询时间。然后把时间给主控。主控负责刷新屏幕显示时间