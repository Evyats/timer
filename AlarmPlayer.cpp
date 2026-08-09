#include "AlarmPlayer.h"

#include "songs/Songs.h"

#include <esp_system.h>

AlarmPlayer::AlarmPlayer(
  uint8_t activePiezoPin,
  uint8_t activePiezoLedPin,
  uint8_t mainPiezoPin,
  uint8_t harmonyPiezoPin,
  uint8_t activePiezoVoice,
  int selfTestToneHz,
  int selfTestMs,
  uint16_t slideUpdateIntervalMs
) : activePiezoPin_(activePiezoPin),
    activePiezoLedPin_(activePiezoLedPin),
    mainPiezoPin_(mainPiezoPin),
    harmonyPiezoPin_(harmonyPiezoPin),
    activePiezoVoice_(activePiezoVoice),
    selfTestToneHz_(selfTestToneHz),
    selfTestMs_(selfTestMs),
    slideUpdateIntervalMs_(slideUpdateIntervalMs),
    voicesWithActive_{
      { activePiezoPin, 0, true, false },
      { mainPiezoPin, 1, false, false },
      { harmonyPiezoPin, 2, false, false },
    },
    voicesPassiveOnly_{
      { activePiezoPin, 0, true, true },
      { mainPiezoPin, 1, false, false },
      { harmonyPiezoPin, 2, false, false },
    },
    voicesActiveOnly_{
      { activePiezoPin, 0, true, false },
      { mainPiezoPin, 1, false, true },
      { harmonyPiezoPin, 2, false, true },
    },
    voicesMuted_{
      { activePiezoPin, 0, true, true },
      { mainPiezoPin, 1, false, true },
      { harmonyPiezoPin, 2, false, true },
    },
    currentSong_(nullptr),
    musicEnabled_(true),
    active_(false),
    activePiezoEnabled_(false),
    activePiezoLedOn_(false),
    nextActivePiezoLedEvent_(0),
    soundIgnoredUntilSongMs_(0) {
}

void AlarmPlayer::begin() {
  if (activePiezoLedPin_ != activePiezoPin_) {
    pinMode(activePiezoLedPin_, OUTPUT);
    digitalWrite(activePiezoLedPin_, LOW);
  }

  configureVoices(true);
  player_.setSlideUpdateIntervalMs(slideUpdateIntervalMs_);
}

bool AlarmPlayer::start(bool includeActivePiezo) {
  currentSong_ = pickRandomSong();
  if (currentSong_ == nullptr) {
    Serial.println("No songs configured.");
    return false;
  }

  active_ = true;
  activePiezoEnabled_ = includeActivePiezo;
  soundIgnoredUntilSongMs_ = includeActivePiezo ? findLastActivePiezoEventMs() : 0;
  activePiezoLedOn_ = false;
  nextActivePiezoLedEvent_ = 0;
  writeActivePiezoLed(false);
  configureVoices(includeActivePiezo);
  player_.play(*currentSong_, false);

  Serial.println(includeActivePiezo ? "Playing random alarm song." : "Playing random song without active piezo.");
  if (includeActivePiezo) {
    Serial.print("Sound detector ignored until song position ");
    Serial.print(soundIgnoredUntilSongMs_);
    Serial.println(" ms.");
  }

  return true;
}

void AlarmPlayer::stop() {
  player_.stop();
  active_ = false;
  activePiezoEnabled_ = false;
  soundIgnoredUntilSongMs_ = 0;
  activePiezoLedOn_ = false;
  nextActivePiezoLedEvent_ = 0;
  digitalWrite(activePiezoPin_, LOW);
  writeActivePiezoLed(false);
  configureVoices(true);
}

bool AlarmPlayer::update() {
  if (!active_) {
    return false;
  }

  player_.update();

  if (!player_.isPlaying()) {
    return false;
  }

  if (activePiezoEnabled_) {
    updateActivePiezoLed(player_.positionMs());
  }

  return true;
}

bool AlarmPlayer::isActive() const {
  return active_;
}

bool AlarmPlayer::isPlaying() const {
  return player_.isPlaying();
}

bool AlarmPlayer::activePiezoEnabled() const {
  return activePiezoEnabled_;
}

bool AlarmPlayer::shouldIgnoreSound() const {
  return activePiezoEnabled_ && player_.positionMs() <= soundIgnoredUntilSongMs_;
}

void AlarmPlayer::setMusicEnabled(bool enabled) {
  musicEnabled_ = enabled;
}

bool AlarmPlayer::musicEnabled() const {
  return musicEnabled_;
}

void AlarmPlayer::testOutputs() {
  Serial.print("Testing main passive piezo on GPIO ");
  Serial.print(mainPiezoPin_);
  Serial.println(".");
  ledcWriteTone(mainPiezoPin_, selfTestToneHz_);
  delay(selfTestMs_);
  ledcWriteTone(mainPiezoPin_, 0);
  delay(150);

  Serial.print("Testing second passive piezo on GPIO ");
  Serial.print(harmonyPiezoPin_);
  Serial.println(".");
  ledcWriteTone(harmonyPiezoPin_, selfTestToneHz_);
  delay(selfTestMs_);
  ledcWriteTone(harmonyPiezoPin_, 0);
  delay(150);

  Serial.print("Testing active piezo and LED on GPIO ");
  Serial.print(activePiezoPin_);
  Serial.print(" / GPIO ");
  Serial.print(activePiezoLedPin_);
  Serial.println(".");
  digitalWrite(activePiezoPin_, HIGH);
  writeActivePiezoLed(true);
  delay(selfTestMs_);
  digitalWrite(activePiezoPin_, LOW);
  writeActivePiezoLed(false);
  configureVoices(true);

  Serial.println("Piezo output test done.");
}

void AlarmPlayer::configureVoices(bool includeActivePiezo) {
  if (includeActivePiezo && musicEnabled_) {
    player_.begin(voicesWithActive_, sizeof(voicesWithActive_) / sizeof(voicesWithActive_[0]));
  } else if (musicEnabled_) {
    player_.begin(voicesPassiveOnly_, sizeof(voicesPassiveOnly_) / sizeof(voicesPassiveOnly_[0]));
    digitalWrite(activePiezoPin_, LOW);
  } else if (includeActivePiezo) {
    player_.begin(voicesActiveOnly_, sizeof(voicesActiveOnly_) / sizeof(voicesActiveOnly_[0]));
  } else {
    player_.begin(voicesMuted_, sizeof(voicesMuted_) / sizeof(voicesMuted_[0]));
    digitalWrite(activePiezoPin_, LOW);
  }

  writeActivePiezoLed(false);
}

void AlarmPlayer::writeActivePiezoLed(bool on) {
  if (activePiezoLedPin_ == activePiezoPin_) {
    return;
  }

  digitalWrite(activePiezoLedPin_, on ? HIGH : LOW);
}

const PiezoSong* AlarmPlayer::pickRandomSong() const {
  if (TIMER_SONG_COUNT == 0) {
    return nullptr;
  }

  return TIMER_SONGS[esp_random() % TIMER_SONG_COUNT];
}

uint32_t AlarmPlayer::findLastActivePiezoEventMs() const {
  if (currentSong_ == nullptr) {
    return 0;
  }

  uint32_t lastActiveEventMs = 0;

  for (uint32_t i = 0; i < currentSong_->eventCount; i++) {
    PiezoEvent event;
    memcpy_P(&event, &currentSong_->events[i], sizeof(event));

    if (event.voice == activePiezoVoice_) {
      lastActiveEventMs = event.timeMs;
    }
  }

  return lastActiveEventMs;
}

void AlarmPlayer::updateActivePiezoLed(uint32_t songPositionMs) {
  if (currentSong_ == nullptr) {
    return;
  }

  while (nextActivePiezoLedEvent_ < currentSong_->eventCount) {
    PiezoEvent event;
    memcpy_P(&event, &currentSong_->events[nextActivePiezoLedEvent_], sizeof(event));

    if (event.timeMs > songPositionMs) {
      break;
    }

    if (event.voice == activePiezoVoice_) {
      bool ledOn = event.frequency > 0;
      if (ledOn != activePiezoLedOn_) {
        activePiezoLedOn_ = ledOn;
        writeActivePiezoLed(activePiezoLedOn_);
      }
    }

    nextActivePiezoLedEvent_++;
  }
}
