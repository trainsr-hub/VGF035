// path: src/config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define STBY     33
#define PWM_R    13  
#define IN2_R    12  
#define IN1_R    14  
#define IN1_L    25  
#define IN2_L    26  
#define PWM_L    27  

#define SR05_PIN 32  

#define STEPPER_IN1 15 
#define STEPPER_IN2 2  
#define STEPPER_IN3 4
#define STEPPER_IN4 16

#define JQ8900_TX   17 

#define RC522_SDA   5  
#define RC522_SCK   18 
#define RC522_MISO  19 
#define RC522_MOSI  23 
#define RC522_RST   22 

#define LINE_1   36 
#define LINE_2   39 
#define LINE_3   34 
#define LINE_4   35 
#define LINE_5   21 

/* =========================================================================
   [HIGHLIGHT THAY ĐỔI]: 
   - Đổi PWM_FREQ sang 20000Hz chống nhiễu rít theo Motors.ino.
   - Thêm hằng số DRIVE_MAX_PWM (160) và BASE_PWM (80).
   - Xóa bỏ AGV_SPEED cũ.
   ========================================================================= */
#define PWM_FREQ 20000
#define PWM_RES  8
#define CH_L     0   
#define CH_R     1   

extern const int MAX_PWM_VAL; 
extern int DRIVE_MAX_PWM;              
extern int BASE_PWM;


#define STEPS_PER_ROTATION 2048 
#define WDT_TIMEOUT 3 

enum AGVState {
    ST_TRACKING,
    ST_ERROR_LOST_LINE,
    ST_RFID_PROCESSING
};

#endif