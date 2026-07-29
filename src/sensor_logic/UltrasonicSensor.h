// path: src/sensor_logic/UltrasonicSensor.h
#ifndef ULTRASONIC_SENSOR_H
#define ULTRASONIC_SENSOR_H

#include <Arduino.h>

void ultrasonic_init();
float ultrasonic_get_distance();

#endif