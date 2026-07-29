// path: src/audio_logic/Jq8900Player.h
#ifndef JQ8900_PLAYER_H
#define JQ8900_PLAYER_H

#include <Arduino.h>

void audio_init();
void audio_play_track(uint16_t trackNumber);
void audio_stop();

#endif