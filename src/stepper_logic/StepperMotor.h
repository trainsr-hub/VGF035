// path: src/stepper_logic/StepperMotor.h
#ifndef STEPPER_MOTOR_H
#define STEPPER_MOTOR_H

#include <Arduino.h>

void stepper_init();
void stepper_set_target(int targetPos);
bool stepper_is_reached();
void stepper_run_non_blocking();

#endif