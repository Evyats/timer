#ifndef TIMER_SONGS_H
#define TIMER_SONGS_H

#include "belle.h"
#include "belle2.h"
#include "ele_hahayim.h"
#include "ele_hahayim_high.h"

const PiezoSong* const TIMER_SONGS[] = {
  &BELLE_SONG,
  &BELLE2_SONG,
  &ELE_HAHAYIM_SONG,
  &ELE_HAHAYIM_HIGH_SONG
};

const uint8_t TIMER_SONG_COUNT = sizeof(TIMER_SONGS) / sizeof(TIMER_SONGS[0]);

#endif
