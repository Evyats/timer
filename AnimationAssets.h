#ifndef ANIMATION_ASSETS_H
#define ANIMATION_ASSETS_H

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

struct AnimationClip {
  const uint8_t (*frames)[128];
  uint8_t frameCount;
  uint8_t width;
  uint8_t height;
  uint16_t frameDelayMs;
};

namespace AnimationAssets {
const uint16_t DEFAULT_FRAME_DELAY_MS = 42;
const uint8_t FRAME_WIDTH = 32;
const uint8_t FRAME_HEIGHT = 32;

uint8_t loadingAnimationCount();
const AnimationClip& loadingAnimation(uint8_t animationIndex);
const AnimationClip& soundAnimation();
void drawFrame(Adafruit_SSD1306& display, const AnimationClip& clip, uint8_t frameIndex, int16_t x, int16_t y);
}

#endif
