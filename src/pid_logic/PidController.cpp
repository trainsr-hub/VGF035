// path: src/pid_logic/PidController.cpp
#include "PidController.h"
#include "../config.h"

static float last_error = 0;
static float integral = 0;
static float last_pid_value = 0;

static float auto_adaptive_gain = 1.0;
static int sluggish_counter = 0;
static int oscillation_counter = 0;
static unsigned long last_cross_time = 0;

float get_auto_adaptive_gain() {
    return auto_adaptive_gain;
}

MotorSpeed calculate_motor_speed(uint8_t lineData, float Kp, float Ki, float Kd, float speedFactor, char bias) {
    bool s1 = !((lineData >> 4) & 1); // Trái ngoài
    bool s2 = !((lineData >> 3) & 1); // Trái trong
    bool s3 = !((lineData >> 2) & 1); // Giữa
    bool s4 = !((lineData >> 1) & 1); // Phải trong
    bool s5 = !((lineData >> 0) & 1); // Phải ngoài

    /* =========================================================================
       [HIGHLIGHT THAY ĐỔI]: 
       - Xóa bỏ logic thay đổi trọng số w1..w5 rườm rà.
       - Áp dụng "Boolean Masking" (Che mắt).
       - Nếu nhận lệnh R và nhánh phải bắt đầu xuất hiện (s4 hoặc s5 có vạch), 
         lập tức chọc mù mắt trái (s1, s2) và mắt giữa (s3). Xe sẽ bị ép bẻ gắt 
         sang phải để sửa "ảo giác" lỗi này.
       - Tương tự với lệnh L.
    ========================================================================= */
    if (bias == 'R') {
        if (s4 || s5) {
            s1 = false;
            s2 = false;
            s3 = false;
        }
    } 
    else if (bias == 'L') {
        if (s1 || s2) {
            s3 = false;
            s4 = false;
            s5 = false;
        }
    }
    /* ========================================================================= */

    float v1 = s1 ? 1.0 : -1.0;
    float v2 = s2 ? 1.0 : -1.0;
    float v3 = s3 ? 1.0 : -1.0;
    float v4 = s4 ? 1.0 : -1.0;
    float v5 = s5 ? 1.0 : -1.0;

    // Giữ nguyên trọng số đối xứng chuẩn
    float w1 = -2.0, w2 = -1.0, w3 = 0.0, w4 = 1.0, w5 = 2.0;

    float raw_error = (w1 * v1) + (w2 * v2) + (w3 * v3) + (w4 * v4) + (w5 * v5);
    
    if (!s3) { 
        raw_error *= 1.5; 
    }

    float error = raw_error / 9.0;
    
    unsigned long now = millis();

    if (abs(error) > 0.1) { 
        if (abs(error - last_error) < 0.01) {
            sluggish_counter++;
            if (sluggish_counter > 15) { 
                auto_adaptive_gain += 0.05; 
                sluggish_counter = 0;
            }
        }
    } else {
        sluggish_counter = 0;
    }

    if ((last_error > 0 && error < 0) || (last_error < 0 && error > 0)) {
        if (now - last_cross_time < 120) { 
            oscillation_counter++;
            if (oscillation_counter > 3) {
                auto_adaptive_gain -= 0.05; 
                oscillation_counter = 0;
            }
        } else {
            if (oscillation_counter > 0) oscillation_counter--;
        }
        last_cross_time = now;
    }

    auto_adaptive_gain = constrain(auto_adaptive_gain, 0.6, 2.2);

    integral += error;
    integral = constrain(integral, -2.0, 2.0); 
    
    float derivative = error - last_error;
    
    float current_pid = ((Kp * auto_adaptive_gain) * error) + (Ki * integral) + ((Kd * auto_adaptive_gain) * derivative);
    
    last_error = error;
    last_pid_value = current_pid;

    float obstacle_gain = 1.0;
    if (speedFactor < 1.0 && speedFactor > 0.0) {
        obstacle_gain = 1.0 + (1.0 - speedFactor); 
    }

    float pwm_offset = last_pid_value * (DRIVE_MAX_PWM - BASE_PWM) * obstacle_gain;
    int active_base = (BASE_PWM * speedFactor) * (1.0 + (auto_adaptive_gain - 1.0) * 0.3);

    MotorSpeed speed;
    speed.left  = constrain(active_base + pwm_offset, -DRIVE_MAX_PWM, DRIVE_MAX_PWM);
    speed.right = constrain(active_base - pwm_offset, -DRIVE_MAX_PWM, DRIVE_MAX_PWM);

    return speed;
}

MotorSpeed get_last_inertial_speed(float speedFactor) {
    float obstacle_gain = 1.0;
    if (speedFactor < 1.0 && speedFactor > 0.0) {
        obstacle_gain = 1.0 + (1.0 - speedFactor); 
    }
    
    float pwm_offset = last_pid_value * (DRIVE_MAX_PWM - BASE_PWM) * obstacle_gain;
    int active_base = (BASE_PWM * speedFactor) * (1.0 + (auto_adaptive_gain - 1.0) * 0.3);
    
    MotorSpeed speed;
    speed.left  = constrain(active_base + pwm_offset, -DRIVE_MAX_PWM, DRIVE_MAX_PWM);
    speed.right = constrain(active_base - pwm_offset, -DRIVE_MAX_PWM, DRIVE_MAX_PWM);
    return speed;
}

void reset_pid() {
    integral = 0;
    last_error = 0;
    last_pid_value = 0;
    auto_adaptive_gain = 1.0;
    sluggish_counter = 0;
    oscillation_counter = 0;
}