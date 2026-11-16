#ifndef TIMER_SONGS_H
#define TIMER_SONGS_H

#include "absurd.h"
#include "baat_elay.h"
#include "derech.h"
#include "ele_hahayim.h"
#include "lanahar.h"
#include "lo_levad.h"
#include "mabul.h"
#include "narkis.h"
#include "shkufim.h"

const PiezoSong* const TIMER_SONGS[] = {
  &ABSURD_SONG,
  &BAAT_ELAY_SONG,
  &DERECH_SONG,
  &ELE_HAHAYIM_SONG,
  &LANAHAR_SONG,
  &LO_LEVAD_SONG,
  &MABUL_SONG,
  &NARKIS_SONG,
  &SHKUFIM_SONG
};

const uint8_t TIMER_SONG_COUNT = sizeof(TIMER_SONGS) / sizeof(TIMER_SONGS[0]);

#endif
