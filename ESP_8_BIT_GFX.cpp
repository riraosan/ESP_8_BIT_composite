/*

Adafruit_GFX for ESP_8_BIT color composite video.

NOT AN OFFICIAL ADAFRUIT GRAPHICS LIBRARY.

Allows ESP32 Arduino sketches to draw to a composite video device using
Adafruit's graphics API.

NOTE RE:COLOR

Adafruit GFX is designed for 16-bit (RGB565) color, but ESP_8_BIT video
only handles 8-bit (RGB332) color. There are two ways to handle this,
specified by passsing "8" or "16" into the constructor:

8  = Truncate the 16-bit color values and use the lower 8 bits directly as
     RGB332 color. This is faster, but caller needs to know to use 8-bit
     color values. A good choice when writing new code using this library.
16 = Automatically extract the most significant 3 red, 3 green, and 2 blue
     bits from a 16-bit RGB565 color value to generate a RGB332 color.
     Performing this conversion slows down the code, but the caller does not
     need to know about the limitations. A good choice when reusing existing
     Adafruit GFX code that works in 16-bit color space.

An utility function RGB565toRGB332 is available to perform this conversion.

NOTE RE:ASPECT RATIO

Adafruit GFX assumes pixels are square, but this is not true of ESP_8_BIT
which has nonsquare pixels. (4:3 aspect ratio in a 256x240 frame buffer.)
Circles will look squashed as wide ovals, etc. This version of the API does
not offer any way to compensate, the caller has to deal with it.



Copyright (c) Roger Cheng

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

modified @riraosan

*/

#include "ESP_8_BIT_GFX.h"

ESP_8_BIT_GFX::ESP_8_BIT_GFX(bool ntsc) {
  _pVideo = new ESP_8_BIT_composite(ntsc);
  if (NULL == _pVideo) {
    log_e("Video signal generator allocation failed");
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // Default behavior is not to copy buffer upon swap
  _copyAfterSwap = false;

  // Initialize performance tracking state
  _perfStart = 0;
  _perfEnd   = 0;
  _waitTally = 0;
}

/*
 * @brief Call once to set up the API with self-allocated frame buffer.
 */
void ESP_8_BIT_GFX::begin(bool isDoubleBuffer) {
  _pVideo->begin(isDoubleBuffer);
}

/*
 * @brief Calculate performance metrics, output as INFO log.
 * @return Number range from 0 to 10000. Higher values indicate more time
 * has been spent waiting for buffer swap, implying the rest of the code
 * ran faster and completed more quickly.
 */
uint32_t ESP_8_BIT_GFX::perfData() {
  uint32_t fraction = getWaitFraction();

  if (_perfEnd < _perfStart) {
    log_e("Performance end time is earlier than start time.");
  } else {
    uint32_t duration = _perfEnd - _perfStart;
    if (duration < _waitTally) {
      log_e("Overall time duration is less than tally of wait times.");
    } else {
      uint32_t frames         = _pVideo->getRenderedFrameCount() - _frameStart;
      uint32_t swaps          = _pVideo->getBufferSwapCount() - _swapStart;
      uint32_t wholePercent   = fraction / 100;
      uint32_t decimalPercent = fraction % 100;
      log_i("Waited %d.%d%%, missed %d of %d frames", wholePercent, decimalPercent, frames - swaps, frames);
    }
  }
  _perfStart = 0;
  _perfEnd   = 0;
  _waitTally = 0;

  return fraction;
}

/*
 * @brief Wait for swap of front and back buffer. Gathers performance
 * metrics while waiting.
 */
void ESP_8_BIT_GFX::waitForFrame() {
  // Track the old lines array in case we need to copy after swap
  uint8_t** oldLineArray = _pVideo->getFrameBufferLines();
  // Values to track time spent waiting for swap
  uint32_t waitStart = xthal_get_ccount();
  uint32_t waitEnd;

  if (waitStart < _perfEnd) {
    // CCount overflowed since last call, conclude this session.
    perfData();
  }
  if (0 == _waitTally) {
    // No wait tally signifies start of new session.
    _perfStart  = waitStart;
    _frameStart = _pVideo->getRenderedFrameCount();
    _swapStart  = _pVideo->getBufferSwapCount();
  }

  // Wait for swap of front and back buffer
  _pVideo->waitForFrame();

  if (_copyAfterSwap) {
    uint8_t** newLineArray = _pVideo->getFrameBufferLines();

    // This must be kept in sync with how frame buffer memory
    // is allocated in ESP_8_BIT_composite::frameBufferAlloc()
    for (uint8_t chunk = 0; chunk < 15; chunk++) {
      memcpy(newLineArray[chunk * 16], oldLineArray[chunk * 16], 256 * 16);
    }
  }

  // Core clock count after we've finished waiting
  waitEnd = xthal_get_ccount();
  if (waitEnd < waitStart) {
    // CCount overflowed while we were waiting, perform calculation
    // ignoring the time spent waiting.
    _perfEnd = waitStart;
    perfData();
  } else {
    // Increase tally of time we spent waiting for buffer swap
    _waitTally += waitEnd - waitStart;
    _perfEnd = waitEnd;
  }
}

/*
 * @brief Fraction of time in waitForFrame() in percent of percent.
 * @return Number range from 0 to 10000. Higher values indicate more time
 * has been spent waiting for buffer swap, implying the rest of the code
 * ran faster and completed more quickly.
 */
uint32_t ESP_8_BIT_GFX::getWaitFraction() {
  if (_perfEnd > _perfStart + 10000) {
    return _waitTally / ((_perfEnd - _perfStart) / 10000);
  } else {
    return 10000;
  }
}

/*
 * @brief Ends the current performance tracking session and start a new
 * one. Useful for isolating sections of code for measurement.
 * @note Sessions are still terminated whenever CPU clock counter
 * overflows (every ~18 seconds @ 240MHz) so some data may still be lost.
 * @return Number range from 0 to 10000. Higher values indicate more time
 * has been spent waiting for buffer swap, implying the rest of the code
 * ran faster and completed more quickly.
 */
uint32_t ESP_8_BIT_GFX::newPerformanceTrackingSession() {
  return perfData();
}
