// path: src/sensor_logic/UltrasonicSensor.cpp
#include "UltrasonicSensor.h"
#include "../config.h"

// Các biến nội bộ chỉ dùng cho ngắt phần cứng của SRF05
volatile unsigned long sr05_echo_start = 0;
volatile unsigned long sr05_echo_end = 0;
volatile bool sr05_new_data = false;
volatile bool sr05_measuring = false;

// Hàm ngắt (ISR) bắt buộc phải nằm trong IRAM
void IRAM_ATTR sr05_isr() {
    if (digitalRead(SR05_PIN) == HIGH) {
        sr05_echo_start = micros();
    } else {
        sr05_echo_end = micros();
        sr05_new_data = true;
        sr05_measuring = false;
    }
}

void ultrasonic_init() {
    // Khởi tạo trạng thái ban đầu an toàn cho chân SRF05
    pinMode(SR05_PIN, INPUT);
}

// Logic bóp cò nội bộ (Không phơi bày ra Header)
void trigger_ultrasonic_internal() {
    if (sr05_measuring) {
        // Tránh kẹt logic nếu bị lỡ xung FALLING
        if (micros() - sr05_echo_start > 30000) { 
            sr05_measuring = false;
        } else {
            return;
        }
    }
    
    detachInterrupt(digitalPinToInterrupt(SR05_PIN));
    pinMode(SR05_PIN, OUTPUT);
    digitalWrite(SR05_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(SR05_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SR05_PIN, LOW);
    
    pinMode(SR05_PIN, INPUT);
    sr05_measuring = true;
    attachInterrupt(digitalPinToInterrupt(SR05_PIN), sr05_isr, CHANGE);
}

float ultrasonic_get_distance() {
    static float samples[3] = {999.0, 999.0, 999.0};
    static int index = 0;
    static unsigned long last_trigger = 0;
    
    // Tự động trigger mỗi 30ms
    if (millis() - last_trigger >= 30) {
        trigger_ultrasonic_internal();
        last_trigger = millis();
    }

    // Tính toán nếu có data từ ngắt trả về
    if (sr05_new_data) {
        long duration = sr05_echo_end - sr05_echo_start;
        float dist = (duration * 0.0343) / 2.0;
        
        if (dist > 0 && dist < 400.0) {
            samples[index] = dist;
            index = (index + 1) % 3;
        }
        sr05_new_data = false;
    }

    // Lọc trung vị
    float sorted[3];
    for(int i = 0; i < 3; i++) sorted[i] = samples[i];
    for (int i = 0; i < 2; i++) {
        for (int j = i + 1; j < 3; j++) {
            if (sorted[i] > sorted[j]) {
                float temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }

    return sorted[1];
}