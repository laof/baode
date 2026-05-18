按你板子标注核对一遍：

屏幕	板子	检查点
VCC	3V3	万用表测屏幕背面 VCC 是否 3.3V
GND	G	共地
SCL	A5	SPI1 SCK
SDA	A7	SPI1 MOSI
CS	A4	
DC	A6	
RES	B0	这条必须接，不接屏不会初始化
BLC	3V3	这块反射屏 BLC 可以不接，但不要悬空到负电平