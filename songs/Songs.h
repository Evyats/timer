#ifndef TIMER_SONGS_H
#define TIMER_SONGS_H

#include "belle2.h"
#include "ele_hahayim.h"
#include "bat_elay.h"

const PiezoSong* const TIMER_SONGS[] = {
  &BELLE2_SONG,
  &ELE_HAHAYIM_SONG,
  &BAT_ELAY_SONG
};

const uint8_t TIMER_SONG_COUNT = sizeof(TIMER_SONGS) / sizeof(TIMER_SONGS[0]);

#endif
