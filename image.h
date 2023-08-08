#pragma once

#include "vector3.h"

class image
{
public:
    #pragma region Image
    color background_color = color(0, 0, 0);
    double aspect_ratio = 16.0 / 9.0;
    int image_width = 1920 * 0.5;
    int image_height = static_cast<int>(image_width / aspect_ratio);

    int max_color = 255;

    int samples_per_pixel = 16;
    int max_depth = 16;

    const char* pngImg = "render.png";
    #pragma endregion
};

