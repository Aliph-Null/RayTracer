#pragma once

#include <iostream>
#include <vector>
#include "vector3.h"
#include "ray_trace_engine.h"

inline color GammaCorrect2(color rgb, int samples_per_pixel)
{
    // Divide the color by the number of samples and gamma-correct for gamma=2.0.
    double scale = 1.0 / samples_per_pixel;
    double r = sqrt(scale * rgb.x());
    double g = sqrt(scale * rgb.y());
    double b = sqrt(scale * rgb.z());

    return color(r, g, b);
}

inline void write_color(std::ostream& out, const color& pixel_color, int samples_per_pixel)
{
    color rgb = GammaCorrect2(pixel_color, samples_per_pixel);
    
    double r = rgb.x();
    double g = rgb.y();
    double b = rgb.z();


    // Write the translated [0,255] value of each color component.
    out << static_cast<int>(256 * clamp(r, 0.0, 0.999)) << ' '
        << static_cast<int>(256 * clamp(g, 0.0, 0.999)) << ' '
        << static_cast<int>(256 * clamp(b, 0.0, 0.999)) << '\n';
}

inline void write_color(std::vector<unsigned char>& pixels, int pixel, const color& pixel_color, int samples_per_pixel)
{
    color rgb = GammaCorrect2(pixel_color, samples_per_pixel);

    double r = 256 * clamp(rgb.x(), 0.0, 0.999);
    double g = 256 * clamp(rgb.y(), 0.0, 0.999);
    double b = 256 * clamp(rgb.z(), 0.0, 0.999);

    pixels[static_cast<std::vector<unsigned char, std::allocator<unsigned char>>::size_type>(pixel) * 3 + 0] = static_cast<unsigned char>(r);
    pixels[static_cast<std::vector<unsigned char, std::allocator<unsigned char>>::size_type>(pixel) * 3 + 1] = static_cast<unsigned char>(g);
    pixels[static_cast<std::vector<unsigned char, std::allocator<unsigned char>>::size_type>(pixel) * 3 + 2] = static_cast<unsigned char>(b);
}