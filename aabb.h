#pragma once

#include "ray_trace_engine.h"

class aabb {
public:
    aabb() {}
    aabb(const point3& a, const point3& b) { minimum = a; maximum = b; }

    point3 minim() const { return minimum; }
    point3 maxim() const { return maximum; }

    bool hit(const ray& r, double t_min, double t_max) const
    {
        for (int a = 0; a < 3; a++)
        {
            auto t0 = fmin((minimum[a] - r.origin()[a]) / r.direction()[a],
                (maximum[a] - r.origin()[a]) / r.direction()[a]);
            auto t1 = fmax((minimum[a] - r.origin()[a]) / r.direction()[a],
                (maximum[a] - r.origin()[a]) / r.direction()[a]);
            t_min = fmax(t0, t_min);
            t_max = fmin(t1, t_max);
            if (t_max <= t_min)
                return false;
        }
        return true;
    }

    double area() const
    {
        auto a = maximum.x() - minimum.x();
        auto b = maximum.y() - minimum.y();
        auto c = maximum.z() - minimum.z();
        return 2 * (a * b + b * c + c * a);
    }

    int longest_axis() const
    {
        auto a = maximum.x() - minimum.x();
        auto b = maximum.y() - minimum.y();
        auto c = maximum.z() - minimum.z();
        if (a > b && a > c)
            return 0;
        else if (b > c)
            return 1;
        else
            return 2;
    }

public:
    point3 minimum;
    point3 maximum;
};

inline aabb surrounding_box(aabb box0, aabb box1)
{
    vector3 smol( fmin(box0.minim().x(), box1.minim().x()), fmin(box0.minim().y(), box1.minim().y()), fmin(box0.minim().z(), box1.minim().z()));

    vector3 big(   fmax(box0.maxim().x(), box1.maxim().x()), fmax(box0.maxim().y(), box1.maxim().y()), fmax(box0.maxim().z(), box1.maxim().z()));

    return aabb(smol, big);
}
