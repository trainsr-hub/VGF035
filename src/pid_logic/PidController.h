// path: src/pid_logic/PidController.h
#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include <Arduino.h>

struct MotorSpeed {
    int left;
    int right;
};

/* =========================================================================
   [HIGHLIGHT THAY ĐỔI]: 
   - Thêm tham số `char bias` vào hàm calculate_motor_speed với giá trị 
     mặc định là 'F' (Forward/Cân bằng) để không phá vỡ code ở các hàm cũ.
========================================================================= */
float get_auto_adaptive_gain();
MotorSpeed calculate_motor_speed(uint8_t lineData, float Kp, float Ki, float Kd, float speedFactor, char bias = 'F');
MotorSpeed get_last_inertial_speed(float speedFactor);
void reset_pid();
/* ========================================================================= */

#endif