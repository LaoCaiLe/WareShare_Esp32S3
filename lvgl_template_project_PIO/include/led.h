#ifndef LED_H
#define LED_H
#include <FastLED.h>

#define RGB_LED_NUM 1
#define RGB_LED_PIN 14

class Pixel
{
private:
	CRGB color_buffers[RGB_LED_NUM];

public:
	void init();

	Pixel &setRGB(int id, int r, int g, int b);
	Pixel &setBrightness(float duty);
};

#endif // !LED_H