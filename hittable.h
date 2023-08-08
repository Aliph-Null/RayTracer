#pragma once
#include "ray.h"
#include "ray_trace_engine.h"
#include "aabb.h"

class material;

struct hit_record {
    point3 p;
    vector3 normal = vector3(0, 0, 0);
    shared_ptr<material> mat_ptr;
    double t = 0;
    bool front_face = true;
    double u;
    double v;

    inline void set_face_normal(const ray& r, const vector3& outward_normal)
    {
        front_face = dot(r.direction(), outward_normal) < 0;
        normal = front_face ? outward_normal : -outward_normal;
    }
};

class hittable {
public:
    virtual bool hit(const ray& r, double t_min, double t_max, hit_record& rec) const = 0;
    virtual bool bounding_box(double time0, double time1, aabb& output_box) const = 0;
};

class translate : public hittable {
public:
    translate(shared_ptr<hittable> p, const vector3& displacement)
        : ptr(p), offset(displacement)
    {
    }

    virtual bool hit(
        const ray& r, double t_min, double t_max, hit_record& rec) const override;

    virtual bool bounding_box(double time0, double time1, aabb& output_box) const override;

public:
    shared_ptr<hittable> ptr;
    vector3 offset;
};

inline bool translate::hit(const ray& r, double t_min, double t_max, hit_record& rec) const
{
    ray moved_r(r.origin() - offset, r.direction(), r.time());
    if (!ptr->hit(moved_r, t_min, t_max, rec))
        return false;

    rec.p += offset;
    rec.set_face_normal(moved_r, rec.normal);

    return true;
}

inline bool translate::bounding_box(double time0, double time1, aabb& output_box) const
{
    if (!ptr->bounding_box(time0, time1, output_box))
        return false;

    output_box = aabb(
        output_box.minim() + offset,
        output_box.maxim() + offset);

    return true;
}

class rotate_y : public hittable {
public:
    rotate_y(shared_ptr<hittable> p, double angle);

    virtual bool hit(
        const ray& r, double t_min, double t_max, hit_record& rec) const override;

    virtual bool bounding_box(double time0, double time1, aabb& output_box) const override
    {
        output_box = bbox;
        return hasbox;
    }

public:
    shared_ptr<hittable> ptr;
    double sin_theta;
    double cos_theta;
    bool hasbox;
    aabb bbox;
};

inline rotate_y::rotate_y(shared_ptr<hittable> p, double angle) : ptr(p)
{
    auto radians = degrees_to_radians(angle);
    sin_theta = sin(radians);
    cos_theta = cos(radians);
    hasbox = ptr->bounding_box(0, 1, bbox);

    point3 minim(infinity, infinity, infinity);
    point3 maxim(-infinity, -infinity, -infinity);

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 2; k++)
            {
                auto x = i * bbox.maxim().x() + (1 - i) * bbox.minim().x();
                auto y = j * bbox.maxim().y() + (1 - j) * bbox.minim().y();
                auto z = k * bbox.maxim().z() + (1 - k) * bbox.minim().z();

                auto newx = cos_theta * x + sin_theta * z;
                auto newz = -sin_theta * x + cos_theta * z;

                vector3 tester(newx, y, newz);

                for (int c = 0; c < 3; c++)
                {
                    minim[c] = fmin(minim[c], tester[c]);
                    maxim[c] = fmax(maxim[c], tester[c]);
                }
            }
        }
    }

    bbox = aabb(minim, maxim);
}

inline bool rotate_y::hit(const ray& r, double t_min, double t_max, hit_record& rec) const
{
    auto origin = r.origin();
    auto direction = r.direction();

    origin[0] = cos_theta * r.origin()[0] - sin_theta * r.origin()[2];
    origin[2] = sin_theta * r.origin()[0] + cos_theta * r.origin()[2];

    direction[0] = cos_theta * r.direction()[0] - sin_theta * r.direction()[2];
    direction[2] = sin_theta * r.direction()[0] + cos_theta * r.direction()[2];

    ray rotated_r(origin, direction, r.time());

    if (!ptr->hit(rotated_r, t_min, t_max, rec))
        return false;

    auto p = rec.p;
    auto normal = rec.normal;

    p[0] = cos_theta * rec.p[0] + sin_theta * rec.p[2];
    p[2] = -sin_theta * rec.p[0] + cos_theta * rec.p[2];

    normal[0] = cos_theta * rec.normal[0] + sin_theta * rec.normal[2];
    normal[2] = -sin_theta * rec.normal[0] + cos_theta * rec.normal[2];

    rec.p = p;
    rec.set_face_normal(rotated_r, normal);

    return true;
}