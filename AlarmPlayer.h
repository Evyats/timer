#ifndef ALARM_PLAYER_H
#define ALARM_PLAYER_H

#include <Arduino.h>
#include <PiezoMidiPlayer.h>

class AlarmPlayer {
public:
  AlarmPlayer(
    uint8_t activePiezoPin,
    uint8_t activePiezoLedPin,
    uint8_t mainPiezoPin,
    uint8_t harmonyPiezoPin,
    uint8_t activePiezoVoice,
    int selfTestToneHz,
    int selfTestMs,
    uint16_t slideUpdateIntervalMs
  );

  void begin();
  bool start(bool includeActivePiezo);
  void stop();
  bool update();
  bool isActive() const;
  bool isPlaying() const;
  bool activePiezoEnabled() const;
  bool shouldIgnoreSound() const;
  void setMusicEnabled(bool enabled);
  bool musicEnabled() const;
  void testOutputs();

private:
  void configureVoices(bool includeActivePiezo);
  void writeActivePiezoLed(bool on);
  const PiezoSong* pickRandomSong() const;
  uint32_t findLastActivePiezoEventMs() const;
  void updateActivePiezoLed(uint32_t songPositionMs);

  uint8_t activePiezoPin_;
  uint8_t activePiezoLedPin_;
  uint8_t mainPiezoPin_;
  uint8_t harmonyPiezoPin_;
  uint8_t activePiezoVoice_;
  int selfTestToneHz_;
  int selfTestMs_;
  uint16_t slideUpdateIntervalMs_;

  PiezoPlayer player_;
  PiezoVoice voicesWithActive_[3];
  PiezoVoice voicesPassiveOnly_[3];
  PiezoVoice voicesActiveOnly_[3];
  PiezoVoice voicesMuted_[3];
  const PiezoSong* currentSong_;
  bool musicEnabled_;
  bool active_;
  bool activePiezoEnabled_;
  bool activePiezoLedOn_;
  uint32_t nextActivePiezoLedEvent_;
  uint32_t soundIgnoredUntilSongMs_;
};

#endif
