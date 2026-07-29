// path: src/rfid_logic/RfidScanner.h
#ifndef RFID_SCANNER_H
#define RFID_SCANNER_H

#include <Arduino.h>

void rfid_init();
String rfid_scan();
bool rfid_check_health();

#endif