#include "AnimationAssets.h"

#include "animations/AngelAnimationData.h"
#include "animations/CoffeeAnimationData.h"
#include "animations/EmbarrassedAnimationData.h"
#include "animations/LaughingAnimationData.h"
#include "animations/PorridgeAnimationData.h"
#include "animations/SaladAnimationData.h"
#include "animations/SoundAnimationData.h"
#include "animations/TeaAnimationData.h"
#include "animations/YummyAnimationData.h"

namespace {
const AnimationClip SOUND_ANIMATION = {
  AnimationData::SOUND_FRAMES,
  AnimationData::SOUND_FRAME_COUNT,
  AnimationAssets::FRAME_WIDTH,
  AnimationAssets::FRAME_HEIGHT,
  AnimationAssets::DEFAULT_FRAME_DELAY_MS
};

const AnimationClip LOADING_ANIMATIONS[] = {
  { AnimationData::ANGEL_FRAMES, AnimationData::ANGEL_FRAME_COUNT, AnimationAssets::FRAME_WIDTH, AnimationAssets::FRAME_HEIGHT, AnimationAssets::DEFAULT_FRAME_DELAY_MS },
  { AnimationData::COFFEE_FRAMES, AnimationData::COFFEE_FRAME_COUNT, AnimationAssets::FRAME_WIDTH, AnimationAssets::FRAME_HEIGHT, AnimationAssets::DEFAULT_FRAME_DELAY_MS },
  { AnimationData::EMBARRASSED_FRAMES, AnimationData::EMBARRASSED_FRAME_COUNT, AnimationAssets::FRAME_WIDTH, AnimationAssets::FRAME_HEIGHT, AnimationAssets::DEFAULT_FRAME_DELAY_MS },
  { AnimationData::LAUGHING_FRAMES, AnimationData::LAUGHING_FRAME_COUNT, AnimationAssets::FRAME_WIDTH, AnimationAssets::FRAME_HEIGHT, AnimationAssets::DEFAULT_FRAME_DELAY_MS },
  { AnimationData::PORRIDGE_FRAMES, AnimationData::PORRIDGE_FRAME_COUNT, AnimationAssets::FRAME_WIDTH, AnimationAssets::FRAME_HEIGHT, AnimationAssets::DEFAULT_FRAME_DELAY_MS },
  { AnimationData::SALAD_FRAMES, AnimationData::SALAD_FRAME_COUNT, AnimationAssets::FRAME_WIDTH, AnimationAssets::FRAME_HEIGHT, AnimationAssets::DEFAULT_FRAME_DELAY_MS },
  { AnimationData::TEA_FRAMES, AnimationData::TEA_FRAME_COUNT, AnimationAssets::FRAME_WIDTH, AnimationAssets::FRAME_HEIGHT, AnimationAssets::DEFAULT_FRAME_DELAY_MS },
  { AnimationData::YUMMY_FRAMES, AnimationData::YUMMY_FRAME_COUNT, AnimationAssets::FRAME_WIDTH, AnimationAssets::FRAME_HEIGHT, AnimationAssets::DEFAULT_FRAME_DELAY_MS }
};
}

namespace AnimationAssets {

uint8_t loadingAnimationCount() {
  return sizeof(LOADING_ANIMATIONS) / sizeof(LOADING_ANIMATIONS[0]);
}

const AnimationClip& loadingAnimation(uint8_t animationIndex) {
  return LOADING_ANIMATIONS[animationIndex % loadingAnimationCount()];
}

const AnimationClip& soundAnimation() {
  return SOUND_ANIMATION;
}

void drawFrame(Adafruit_SSD1306& display, const AnimationClip& clip, uint8_t frameIndex, int16_t x, int16_t y) {
  display.drawBitmap(
    x,
    y,
    clip.frames[frameIndex % clip.frameCount],
    clip.width,
    clip.height,
    SSD1306_WHITE
  );
}

}
