// designElectronics — the logo: a glowing blue 5 mm LED on a dark tile.
#pragma once
#include <SDL3/SDL.h>

#include <string>

#include "geom.h"

class Gfx;

// Draw the logo into a square in pixel coordinates; opacity 0..1 fades it (watermarks).
void drawLogo(Gfx& g, Rect r, float opacity = 1);

// Rasterise the logo into a new RGBA surface (window icon, PNG export).
SDL_Surface* makeLogoSurface(int size);

// SVG source of the logo.
std::string logoSvg();
