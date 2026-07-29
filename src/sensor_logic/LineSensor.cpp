// path: src/sensor_logic/LineSensor.cpp
#include "LineSensor.h"
#include "../config.h"

void line_sensor_init() {
    pinMode(LINE_1, INPUT);
    pinMode(LINE_2, INPUT);
    pinMode(LINE_3, INPUT);
    pinMode(LINE_4, INPUT);
    pinMode(LINE_5, INPUT);
}

uint8_t line_sensor_read() {
    uint8_t data = 0;
    // Dịch các giá trị đọc được vào 5 bit cuối của 1 byte
    data |= (digitalRead(LINE_1) << 4);
    data |= (digitalRead(LINE_2) << 3);
    data |= (digitalRead(LINE_3) << 2);
    data |= (digitalRead(LINE_4) << 1);
    data |= (digitalRead(LINE_5) << 0);
    return data;
}