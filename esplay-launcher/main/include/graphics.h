#pragma once

void renderGraphics(int dx, int dy, int sx, int sy, int sw, int sh);
void drawBattery(int batPercent);
void drawVolume(int volume);
void guiCharging(int almostFull);
void guiFull();
void guiBatEmpty();