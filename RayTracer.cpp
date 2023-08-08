#include <iostream>
#include <Windows.h>
#include <thread>
#include <mutex>
#include <chrono>

#include <string>

#include <filesystem>
#include <fstream>

#include <png.h>
#include <sstream>
#include <vector>
#include <iomanip>

#include "ray_trace_engine.h"
#include "vector3.h"
#include "color.h"
#include "ray.h"
#include "hittable_list.h"
#include "sphere.h"
#include "camera.h"
#include "hittable.h"
#include "material.h"
#include "image.h"
#include "moving_sphere.h"
#include "aarect.h"
#include "box.h"
#include "constant_medium.h"
#include "bvh.h"

/*
    11 -> 2     := 550% faster without live render (50r 1s 0.5 FHD)
    150 -> 150  := 0% faster without live render (50r 100s 0.5 FHD)
    114 -> 23   := 495.65% faster without live render (8r 1s 4k)
*/

bool LIVE_WINDOW_RENDER = false;
bool CONSOLE_DEBUG = false;
std::vector<float> *thread_finish_percentage;

enum class Scenes {
    Loaded,
    Preloaded
};
std::vector<std::string> default_scenes = {
        "random_scene",
        "random_scene_withMovingSpheres",
        "two_spheres",
        "two_perlin_spheres",
        "earth", 
        "simple_light", 
        "cornell_box",
        "cornell_smoke",
        "all_features_scene"
};

// Define a mutex for synchronizing access to SetPixel function
std::mutex mtx;

HDC globalHDC = nullptr;
HWND globalHWND = nullptr;

#pragma region Window sruff
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}
HWND InitWindow(size_t width, size_t height)
{
    const wchar_t CLASS_NAME[] = L"MainWindow";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    //Create the window
    HWND hwnd = CreateWindowEx(
        0,                              // Optional window styles
        CLASS_NAME,                     // Window class name
        L"RTX - Bodrug Denis",          // Window title
        WS_OVERLAPPEDWINDOW,            // Window style

        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

        NULL,       // Parent window
        NULL,       // Menu
        GetModuleHandle(NULL), // Instance handle
        NULL        // Additional application data
    );

    if (hwnd == NULL)
    {
        std::cerr << "Failed to create window.\n";
        return NULL;
    }

    //Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    SetWindowPos(hwnd, NULL, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    return hwnd;
}
int DrawBufferToWindow(HWND hwnd, HDC hdc, int width, int height, const std::vector<unsigned char>& buffer)
{
    // Create a DIB with the same dimensions as the buffer
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Negative height to specify a top-down bitmap (origin at the top-left corner)
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24; // 3 bytes per pixel (BGR)
    bmi.bmiHeader.biCompression = BI_RGB; // No compression
    bmi.bmiHeader.biSizeImage = 0; // Not used for uncompressed bitmaps

    // Rearrange RGB values to BGR in a separate buffer
    std::vector<unsigned char> buffer_bgr(buffer.size());
    for (size_t i = 0; i < buffer.size(); i += 3)
    {
        buffer_bgr[i] = buffer[i + 2]; // Blue
        buffer_bgr[i + 1] = buffer[i + 1]; // Green
        buffer_bgr[i + 2] = buffer[i]; // Red
    }

    // Use SetDIBitsToDevice to draw the buffer to the window's device context
    int result = SetDIBitsToDevice(
        hdc,                    // Device context handle
        0,                      // Destination X-coordinate
        0,                      // Destination Y-coordinate
        width,                  // Width of the area to be drawn
        height,                 // Height of the area to be drawn
        0,                      // Source X-coordinate (start from the top-left of the bitmap)
        0,                      // Source Y-coordinate
        0,                      // Starting scan line (usually 0 for top-down bitmaps)
        height,                 // Number of scan lines (height of the bitmap)
        buffer_bgr.data(),      // Pointer to the rearranged BGR buffer
        &bmi,                   // Pointer to the BITMAPINFO structure
        DIB_RGB_COLORS          // Color table type (DIB_RGB_COLORS indicates RGB values in buffer)
    );

    //(result will be the number of scan lines copied, or -1 for error)
    return result;
}
#pragma endregion

#pragma region Preloaded worlds
hittable_list two_spheres()
{
    hittable_list objects;

    auto checker = make_shared<checker_texture>(color(0.2, 0.1, 0.05), color(0.9, 0.8, 0.7));

    objects.add(make_shared<sphere>(point3(0, -10, 0), 10, make_shared<lambertian>(checker)));
    objects.add(make_shared<sphere>(point3(0, 10, 0), 10, make_shared<lambertian>(checker)));

    return objects;
}
hittable_list random_scene()
{
    hittable_list world;

    auto checker = make_shared<checker_texture>(color(0.4, 0.4, 0.5), color(0.9, 0.9, 0.9));
    world.add(make_shared<sphere>(point3(0, -1000, 0), 1000, make_shared<lambertian>(checker)));

    for (int a = -11; a < 11; a++)
    {
        for (int b = -11; b < 11; b++)
        {
            auto choose_mat = random_double();
            point3 center(a + 0.9 * random_double(), 0.2, b + 0.9 * random_double());

            if ((center - point3(4, 0.2, 0)).length() > 0.9)
            {
                shared_ptr<material> sphere_material;

                if (choose_mat < 0.8)
                {
                    // diffuse
                    auto albedo = color::random() * color::random();
                    sphere_material = make_shared<lambertian>(albedo);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                }
                else if (choose_mat < 0.95)
                {
                    // metal
                    auto albedo = color::random(0.5, 1);
                    auto fuzz = random_double(0, 0.5);
                    sphere_material = make_shared<metal>(albedo, fuzz);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                }
                else
                {
                    // glass
                    sphere_material = make_shared<dielectric>(1.5);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                }
            }
        }
    }

    auto material1 = make_shared<dielectric>(1.5);
    world.add(make_shared<sphere>(point3(0, 1, 0), 1.0, material1));

    auto material2 = make_shared<normals>(color(0.1, 0.1, 0.1));
    world.add(make_shared<sphere>(point3(-4, 1, 0), 1.0, material2));

    auto material3 = make_shared<metal>(color(0.7, 0.7, 0.7), 0.0);
    world.add(make_shared<sphere>(point3(4, 1, 0), 1.0, material3));

    return world;
}
hittable_list random_scene_withMovingSpheres(){
    hittable_list world;

    auto ground_material = make_shared<lambertian>(color(0.5, 0.5, 0.5));
    world.add(make_shared<sphere>(point3(0, -1000, 0), 1000, ground_material));

    for (int a = -11; a < 11; a++)
    {
        for (int b = -11; b < 11; b++)
        {
            auto choose_mat = random_double();
            point3 center(a + 0.9 * random_double(), 0.2, b + 0.9 * random_double());

            if ((center - point3(4, 0.2, 0)).length() > 0.9)
            {
                shared_ptr<material> sphere_material;

                if (choose_mat < 0.8)
                {
                    // diffuse
                    auto albedo = color::random() * color::random();
                    sphere_material = make_shared<lambertian>(albedo);
                    auto center2 = center + vector3(0, random_double(0, .5), 0);
                    world.add(make_shared<moving_sphere>(
                        center, center2, 0.0, 1.0, 0.2, sphere_material));
                }
                else if (choose_mat < 0.95)
                {
                    // metal
                    auto albedo = color::random(0.5, 1);
                    auto fuzz = random_double(0, 0.5);
                    sphere_material = make_shared<metal>(albedo, fuzz);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                }
                else
                {
                    // glass
                    sphere_material = make_shared<dielectric>(1.5);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                }
            }
        }
    }

    auto material1 = make_shared<dielectric>(1.5);
    world.add(make_shared<sphere>(point3(0, 1, 0), 1.0, material1));

    auto material2 = make_shared<normals>();
    world.add(make_shared<sphere>(point3(-4, 1, 0), 1.0, material2));

    auto material3 = make_shared<metal>(color(0.7, 0.7, 0.7), 0.0);
    world.add(make_shared<sphere>(point3(4, 1, 0), 1.0, material3));

    return world;
}
hittable_list two_perlin_spheres(){
    hittable_list objects;

    auto ground = make_shared<noise_texture>(color(1, 1, 1), 1, 25);
    auto sphere2 = make_shared<noise_texture>(color(0.9, 0.8, 0.9), 1, 10);

    objects.add(make_shared<sphere>(point3(0, -1000, 0), 1000, make_shared<lambertian>(ground)));
    objects.add(make_shared<sphere>(point3(0, 2, 0), 2, make_shared<lambertian>(sphere2)));

    return objects;
}
hittable_list earth(){
    auto earth_texture = make_shared<image_texture>("earthmap.jpg");
    auto earth_surface = make_shared<lambertian>(earth_texture);
    auto globe = make_shared<sphere>(point3(0, 0, 0), 2, earth_surface);

    return hittable_list(globe);
}
hittable_list simple_light(){
    hittable_list objects = two_perlin_spheres();

    auto difflight = make_shared<diffuse_light>(color(4, 4, 4));
    objects.add(make_shared<xy_rect>(3, 5, 1, 3, -2, difflight));

    return objects;
}
hittable_list cornell_box()
{
    hittable_list objects;

    auto red = make_shared<lambertian>(color(.65, .05, .05));
    auto white = make_shared<lambertian>(color(.73, .73, .73));
    auto green = make_shared<lambertian>(color(.12, .45, .15));
    auto light = make_shared<diffuse_light>(color(15, 15, 15));

    objects.add(make_shared<yz_rect>(0, 555, 0, 555, 555, green));
    objects.add(make_shared<yz_rect>(0, 555, 0, 555, 0, red));
    objects.add(make_shared<xz_rect>(213, 343, 227, 332, 554, light));
    objects.add(make_shared<xz_rect>(0, 555, 0, 555, 0, white));
    objects.add(make_shared<xz_rect>(0, 555, 0, 555, 555, white));
    objects.add(make_shared<xy_rect>(0, 555, 0, 555, 555, white));

    shared_ptr<hittable> box1 = make_shared<box>(point3(0, 0, 0), point3(165, 330, 165), white);
    box1 = make_shared<rotate_y>(box1, 15);
    box1 = make_shared<translate>(box1, vector3(265, 0, 295));
    objects.add(box1);

    shared_ptr<hittable> box2 = make_shared<box>(point3(0, 0, 0), point3(165, 165, 165), white);
    box2 = make_shared<rotate_y>(box2, -18);
    box2 = make_shared<translate>(box2, vector3(130, 0, 65));
    objects.add(box2);

    return objects;
}
hittable_list cornell_smoke()
{
    hittable_list objects;

    auto red = make_shared<lambertian>(color(.65, .05, .05));
    auto white = make_shared<lambertian>(color(.73, .73, .73));
    auto green = make_shared<lambertian>(color(.12, .45, .15));
    auto light = make_shared<diffuse_light>(color(7, 7, 7));

    objects.add(make_shared<yz_rect>(0, 555, 0, 555, 555, green));
    objects.add(make_shared<yz_rect>(0, 555, 0, 555, 0, red));
    objects.add(make_shared<xz_rect>(113, 443, 127, 432, 554, light));
    objects.add(make_shared<xz_rect>(0, 555, 0, 555, 555, white));
    objects.add(make_shared<xz_rect>(0, 555, 0, 555, 0, white));
    objects.add(make_shared<xy_rect>(0, 555, 0, 555, 555, white));

    shared_ptr<hittable> box1 = make_shared<box>(point3(0, 0, 0), point3(165, 330, 165), white);
    box1 = make_shared<rotate_y>(box1, 15);
    box1 = make_shared<translate>(box1, vector3(265, 0, 295));

    shared_ptr<hittable> box2 = make_shared<box>(point3(0, 0, 0), point3(165, 165, 165), white);
    box2 = make_shared<rotate_y>(box2, -18);
    box2 = make_shared<translate>(box2, vector3(130, 0, 65));

    objects.add(make_shared<constant_medium>(box1, 0.01, color(0, 0, 0)));
    objects.add(make_shared<constant_medium>(box2, 0.01, color(1, 1, 1)));

    return objects;
}
hittable_list all_features_scene(){
    hittable_list boxes1;
    auto ground = make_shared<lambertian>(color(0.45, 0.8, 1));

    const int boxes_per_side = 20;
    for (int i = 0; i < boxes_per_side; i++)
    {
        for (int j = 0; j < boxes_per_side; j++)
        {
            auto w = 100.0;
            auto x0 = -1000.0 + i * w;
            auto z0 = -1000.0 + j * w;
            auto y0 = 0.0;
            auto x1 = x0 + w;
            auto y1 = random_double(1, 101);
            auto z1 = z0 + w;

            boxes1.add(make_shared<box>(point3(x0, y0, z0), point3(x1, y1, z1), ground));
        }
    }

    hittable_list objects;

    objects.add(make_shared<bvh_node>(boxes1, 0, 1));

    auto light = make_shared<diffuse_light>(color(7, 7, 7));
    objects.add(make_shared<xz_rect>(123, 423, 147, 412, 554, light));

    auto center1 = point3(400, 400, 200);
    auto center2 = center1 + vector3(30, 0, 0);
    auto moving_sphere_material = make_shared<lambertian>(color(0.7, 0.3, 0.1));
    objects.add(make_shared<moving_sphere>(center1, center2, 0, 1, 50, moving_sphere_material));

    objects.add(make_shared<sphere>(point3(260, 150, 45), 50, make_shared<dielectric>(1.5)));
    objects.add(make_shared<sphere>(
        point3(0, 150, 145), 50, make_shared<metal>(color(0.8, 0.8, 0.9), 1.0)
    ));

    auto boundary = make_shared<sphere>(point3(360, 150, 145), 70, make_shared<dielectric>(1.5));
    objects.add(boundary);
    objects.add(make_shared<constant_medium>(boundary, 0.2, color(0.2, 0.4, 0.9)));
    boundary = make_shared<sphere>(point3(0, 0, 0), 5000, make_shared<dielectric>(1.5));
    objects.add(make_shared<constant_medium>(boundary, .0001, color(1, 1, 1)));

    auto emat = make_shared<lambertian>(make_shared<image_texture>("earthmap.jpg"));
    objects.add(make_shared<sphere>(point3(400, 200, 400), 100, emat));
    auto pertext = make_shared<noise_texture>(color(1, 1, 1), 0.1, 1);
    objects.add(make_shared<sphere>(point3(220, 280, 300), 80, make_shared<lambertian>(pertext)));

    hittable_list boxes2;
    auto white = make_shared<lambertian>(color(.73, .73, .73));
    int ns = 1000;
    for (int j = 0; j < ns; j++)
    {
        boxes2.add(make_shared<sphere>(point3::random(0, 165), 10, white));
    }

    objects.add(make_shared<translate>(
        make_shared<rotate_y>(
            make_shared<bvh_node>(boxes2, 0.0, 1.0), 15),
        vector3(-100, 270, 395)
    )
    );

    return objects;
}
#pragma endregion

#pragma region utility functions
void OpenFile(std::string file)
{
    system(("start " + file).c_str());
}

int WaitForUserInput_Start(){
    std::cout << "Commands:\n\tStart\n\tHelp\n\tStop" << std::endl;
    std::cout << "c << ";

    std::string command;
    std::cin >> command;

    //convert to lowercase
    for (char& c : command){
        c = tolower(c);
    }

    if(command == "stop") {
        std::cout << "\rStopping Application";
        return 1;
    }else if (command == "start"){
        //can continue
        return 0;
    }else if(command == "help") {
        std::cout << "Creating and opening help file: " << std::endl;
    }else{
        std::cout << "Unknown command: " << command << "\nAssuming 'help'." << std::endl;
    }

    const std::string help_file = "help.txt";
    std::ofstream file(help_file);

    if (!file){
        std::cerr << "Error creating the file: " << help_file << std::endl;
    }

    file << "The program Ray Tracer by Bodrug Denis:\n"
        "-> uses 3 files\n"
        "    ->config.txt\n"
        "    ->camera.txt\n"
        "    ->scene.scene\n\n"
        "\"config.txt\" is read from the same folder as the .exe program OR you can drag and drop the file onto the .exe program. This way the program will read the other files from the new location, output the image to that location and 'config.txt' can have any other name.\n\n"
        "\"config.txt\" contains:\n"
        "aspect_ratio = 1.777777778\n"
        "image_width = 1920\n"
        "max_color = 255\n"
        "samples_per_pixel = 2048\n"
        "max_depth = 64\n\n"
        "camera_configuration = camera_demo.txt\n"
        "scene_name = demo.scene\n\n"
        "LIVE_WINDOW_RENDER = 0\n"
        "CONSOLE_DEBUG = 0\n\n"
        "Where the basic image properties are defined, as well as the camera configuration file and scene file.\n\n"
        "Note: scene_name has a special configuration:\n"
        "scene_name = preloaded [preloaded scene]\n"
        "where [preloaded scene] are scene hard written into the program, these are:\n\n";

    for (const std::string& scene : default_scenes){
        file << "*" << scene << "*\n";
    }

    file << "\n\"camera.txt\" can have any name if it is said in \"config.txt\" in the same directory, it contains:\n"
        "# Camera settings\n"
        "cam_position = 13 2 3\n"
        "objective_position = 0 0.5 0\n"
        "rotation = 0 1 0\n"
        "vertical_angle = 20\n"
        "aperture = 0.1\n"
        "focus_distance = 10\n"
        "time_start = 0\n"
        "time_end = 0\n\n"
        "And Lastly scene.scene, or any file defined in the configuration\n"
        "You can define \"background_color\" with r, g, b values\n"
        "Every object is defined on a separate line, following this structure:\n"
        "Object (currently only \"sphere\") \"position\" with x, y, z doubles, \"radius\" with double r, \"material\" with these possibilities:\n\n";

    // Assuming you have a vector<string> named possible_materials containing the material names
    std::vector<std::string> possible_materials = {
        "lambertian_checkers color_1            color_2",
        "dielectric          refractive_index",
        "normal              additive_color",
        "metal               color              fuzziness",
        "lambertian_color    color",
        "noise_texture       color              scale               phase",
        "image_texture       image_texture_file",
        "diffuse_light       color"
    };

    for (const std::string& material : possible_materials){
        file << "*" << material << "*\n";
    }

    file << "\nand their properties, being colors (r, g, b), index of refraction (for dielectric (glass) materials) or other material properties defined and shown alongside their name (above).\n\n"
        "[Object] position [x] [y] [z] radius [r] material [material] [r] [g] [b] || [r2] [g2] [b2] || [custom material property]\n\n"
        "example:\n"
        "sphere position 0 -1000 0 radius 1000 material lambertian_checkers 0.4 0.4 0.5 0.9 0.9 0.9\n"
        "sphere position 0 1 0 radius 1 material dielectric 1.5\n"
        "sphere position -4 1 0 radius 1 material normal 0.1 0.1 0.1\n"
        "sphere position 4 1 0 radius 1 material metal 0.7 0.7 0.70\n"
        "sphere position -10.1731 0.2 -3.99209 radius 0.2 material lambertian_color 0.427085 0.228567 0.190677\n\n"
        "For more info refer to: https://aliph-null.github.io/\n";

    file.close();

    OpenFile(help_file);

    return 0;
}

void DataToPng(const char* pngFileName, int width, int height, std::vector<unsigned char>& pixels)
{
    std::cout << "Converting to png" << std::endl;
    // Write PNG file
    FILE* pngFile;// = fopen(pngFileName, "wb");
    if (fopen_s(&pngFile, pngFileName, "wb") != 0)
    {
        std::cerr << "Error creating the PNG file." << std::endl;
        return;
    }

    png_structp pngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!pngPtr)
    {
        std::cerr << "Error creating PNG write structure." << std::endl;
        fclose(pngFile);
        return;
    }

    png_infop infoPtr = png_create_info_struct(pngPtr);
    if (!infoPtr)
    {
        std::cerr << "Error creating PNG info structure." << std::endl;
        png_destroy_write_struct(&pngPtr, nullptr);
        fclose(pngFile);
        return;
    }

    png_bytep* rowPointers = new png_bytep[height];
    for (int y = 0; y < height; ++y)
    {
        rowPointers[y] = &pixels[static_cast<std::vector<unsigned char, std::allocator<unsigned char>>::size_type>(y) * width * 3];
    }

    png_init_io(pngPtr, pngFile);
    png_set_IHDR(pngPtr, infoPtr, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(pngPtr, infoPtr);
    png_write_image(pngPtr, rowPointers);
    png_write_end(pngPtr, nullptr);

    png_destroy_write_struct(&pngPtr, &infoPtr);
    fclose(pngFile);
    delete[] rowPointers;

    std::cout << "Conversion successful!" << std::endl;
}

void Render(camera& cam, image& img, hittable_list& world);

hittable_list create_scene_from_file(std::string scene_name, image &img){
    hittable_list world;

    std::ifstream sceneFile(scene_name);

    if (!sceneFile){
        std::cerr << "Error opening scene file: " << scene_name << std::endl;
        return world;
    }

    std::string line;
    unsigned int count = 0;
    while (std::getline(sceneFile, line)){
        // Skip comments and empty lines
        if (line.empty() || line[0] == '/' || line[0] == '\n'){
            continue;
        }

        std::istringstream iss(line);
        std::string token;

        vector3 position;
        double radius;
        std::string materialType;
        std::string image_texture_file;
        std::string object_type = "sphere";

        for (char& c : object_type){
            c = tolower(c);
        }

        color rgb1, rgb2;
        double refractive_index, fuzz, scale, phase;

        iss >> object_type;

        while (iss >> token){
            if(CONSOLE_DEBUG) std::cout << "token: " << token << " ";
            if(token == "background_color") {
                double r, g, b;
                iss >> r >> g >> b;
                img.background_color = color(r, g, b);
            }else if (token == "position"){
                double x, y, z;
                iss >> x >> y >> z;
                position = vector3(x, y, z);
            }else if (token == "radius"){
                iss >> radius;
            }else if (token == "material"){
                iss >> materialType;

                //convert to lowercase
                for (char& c : materialType){
                    c = tolower(c);
                }

                if (materialType == "lambertian_color"){
                    double r, g, b;
                    iss >> r >> g >> b;
                    rgb1 = color(r, g, b);
                }else if (materialType == "lambertian_checkers"){
                    double r1, g1, b1, r2, g2, b2;
                    iss >> r1 >> g1 >> b1 >> r2 >> g2 >> b2;
                    rgb1 = color(r1, g1, b1);
                    rgb2 = color(r2, g2, b2);
                }else if (materialType == "dielectric"){
                    iss >> refractive_index;
                }else if (materialType == "metal"){
                    double r, g, b;
                    iss >> r >> g >> b >> fuzz;
                    rgb1 = color(r, g, b);
                }else if (materialType == "normal"){
                    double r, g, b;
                    iss >> r >> g >> b;
                    rgb1 = color(r, g, b);
                }else if (materialType == "noise_texture"){
                    double r, g, b;
                    iss >> r >> g >> b >> scale >> phase;
                    rgb1 = color(r, g, b);
                }else if (materialType == "image_texture") {
                    iss >> std::ws >> image_texture_file;
                }else if(materialType == "diffuse_light") {
                    double r, g, b;
                    iss >> r >> g >> b;
                    rgb1 = color(r, g, b);
                }
            }
        }

        shared_ptr<material> material;

        if (materialType == "lambertian_color"){
            material = make_shared<lambertian>(rgb1);
        }else if (materialType == "lambertian_checkers"){
            material = make_shared<lambertian>(make_shared<checker_texture>(rgb1, rgb2));
        }else if (materialType == "dielectric"){
            material = make_shared<dielectric>(refractive_index);
        }else if (materialType == "metal"){
            material = make_shared<metal>(rgb1, fuzz);
        }else if (materialType == "normal"){
            material = make_shared<normals>(rgb1);
        }else if (materialType == "noise_texture"){
            material = make_shared<lambertian>(make_shared<noise_texture>(rgb1, scale, phase));
        }else if (materialType == "image_texture"){
            material = make_shared<lambertian>(make_shared<image_texture>(image_texture_file.c_str()));
        }else if (materialType == "diffuse_light") {
            material = make_shared<diffuse_light>(rgb1);
        }

        world.add(make_shared<sphere>(position, radius, material));
        count++;
    }

    if (CONSOLE_DEBUG) std::cout << "Scene: Loaded " << world.objects.size() << " / " << count << " objects." << std::endl;
    return world;
}
camera create_camera_from_file(std::string &cam_config_file, double aspect_ratio){
    std::ifstream configFile(cam_config_file);

    if (configFile.is_open()){
        std::string line;

        point3 cam_position = point3(0, 1, 0);
        point3 objective_position = point3(1, 1, 1);
        vector3 rotation = vector3(0, 1, 0);
        double vertical_angle = 90;
        double aperture = 0;
        double focus_distance = 10;
        double time_start = 0;
        double time_end = 0;

        while (std::getline(configFile, line))
        {
            if (line.find("cam_position") != std::string::npos){
                double x, y, z;
                std::stringstream ss(line.substr(line.find('=') + 1));
                ss >> x >> y >> z;
                cam_position = point3(x, y, z);
            }else if (line.find("objective_position") != std::string::npos){
                double x, y, z;
                std::stringstream ss(line.substr(line.find('=') + 1));
                ss >> x >> y >> z;
                objective_position = point3(x, y, z);
            }else if (line.find("rotation") != std::string::npos){
                double x, y, z;
                std::stringstream ss(line.substr(line.find('=') + 1));
                ss >> x >> y >> z;
                rotation = vector3(x, y, z);
            }else if (line.find("vertical_angle") != std::string::npos){
                vertical_angle = std::stod(line.substr(line.find('=') + 1));
            }else if (line.find("aperture") != std::string::npos){
                aperture = std::stod(line.substr(line.find('=') + 1));
            }else if (line.find("focus_distance") != std::string::npos){
                focus_distance = std::stod(line.substr(line.find('=') + 1));
            }else if (line.find("time_start") != std::string::npos){
                time_start = std::stod(line.substr(line.find('=') + 1));
            }else if (line.find("time_end") != std::string::npos){
                time_end = std::stod(line.substr(line.find('=') + 1));
            }
        }

        configFile.close();

        camera cam(cam_position, objective_position, rotation, vertical_angle, aspect_ratio, aperture, focus_distance, time_start, time_end);
        return cam;
    }

    std::cout << "Error: Unable to open " << cam_config_file << std::endl;
    camera def_cam(point3(13, 2, 3), point3(0, 0.5, 0), vector3(0, 1, 0), 20, aspect_ratio, 0.1, 10, 0, 0);
    return def_cam;
}

int LoadScene(Scenes scene, hittable_list &world, std::string scene_file, camera &cam, std::string camera_config_file, image &img){
    std::cout << "Loading Scene\n";
    color default_background_color = color(0.7, 0.8, 1);
    img.background_color = default_background_color;

    //preloaded scenes
    if(scene == Scenes::Preloaded){
        std::cout << "Opening preloaded scene: " << scene_file << std::endl;
        if (scene_file == default_scenes[0]) {
            world = random_scene();
            cam = camera(point3(13, 2, 3), point3(0, 0.5, 0), vector3(0, 1, 0), 20, img.aspect_ratio, 0.1, 10, 0, 0);
            return 0;
        }else if (scene_file == default_scenes[1]) {
            world = random_scene_withMovingSpheres();
            cam = camera(point3(13, 2, 3), point3(0, 0.5, 0), vector3(0, 1, 0), 20, img.aspect_ratio, 0.1, 10, 0, 1);
            return 0;
        }else if (scene_file == default_scenes[2]){
            world = two_spheres();
            cam = camera(point3(13, 2, 3), point3(0, 0, 0), vector3(0, 1, 0), 20, img.aspect_ratio, 0.0, 10, 0, 0);
            return 0;
        }else if (scene_file == default_scenes[3]){
            world = two_perlin_spheres();
            cam = camera(point3(26, 3, 6), point3(0, 2, 0), vector3(0, 1, 0), 20, img.aspect_ratio, 0.0, 10, 0, 0);
            return 0;
        }else if (scene_file == default_scenes[4]){
            world = earth();
            cam = camera(point3(13, 2, 3), point3(0, 0, 0), vector3(0, 1, 0), 20, img.aspect_ratio, 0.0, 10, 0, 0);
            return 0;
        }else if (scene_file == default_scenes[5]) {
            world = simple_light();
            cam = camera(point3(26, 3, 6), point3(0, 2, 0), vector3(0, 1, 0), 20, img.aspect_ratio, 0.0, 10, 0, 0);
            img.samples_per_pixel = 512;
            img.max_depth = 128;
            img.background_color = color(0, 0, 0);
            return 0;
        }else if(scene_file == default_scenes[6]) {
            world = cornell_box();

            img.aspect_ratio = 1;
            img.samples_per_pixel = 256;
            img.max_depth = 512;
            img.image_width = 512;
            img.image_height = 512;

            point3 lookfrom = point3(278, 278, -800);
            point3 lookat = point3(278, 278, 0);

            cam = camera(lookfrom, lookat, vector3(0, 1, 0), 40, 1, 0.0, 10, 0, 0);
            img.background_color = color(0, 0, 0);
            return 0;
        }else if (scene_file == default_scenes[7]) {
            world = cornell_smoke();

            img.aspect_ratio = 1;
            img.samples_per_pixel = 256;
            img.max_depth = 512;
            img.image_width = 512;
            img.image_height = 512;

            point3 lookfrom = point3(278, 278, -800);
            point3 lookat = point3(278, 278, 0);

            cam = camera(lookfrom, lookat, vector3(0, 1, 0), 40, 1, 0.0, 10, 0, 0);
            img.background_color = color(0.2, 0.2, 0.2);
            return 0;
        }else if (scene_file == default_scenes[8]) {
            world = all_features_scene();

            img.aspect_ratio = 1;
            img.samples_per_pixel = 10240;
            img.max_depth = 512;
            img.image_width = 800;
            img.image_height = 800;

            point3 lookfrom = point3(478, 278, -600);
            point3 lookat = point3(278, 278, 0);

            cam = camera(lookfrom, lookat, vector3(0, 1, 0), 40, 1, 0.0, 10, 0, 0);
            img.background_color = color(0, 0, 0);
            return 0;
        }else{
            return 1;
        }
    }

    //Load Scene
    world = create_scene_from_file(scene_file, img);
    cam = create_camera_from_file(camera_config_file, img.aspect_ratio);
    return 0;
}
#pragma endregion

#pragma region ray
color ray_color(const ray& r, const color& background, const hittable& world, int depth)
{
    hit_record rec;

    // If we've exceeded the ray bounce limit, no more light is gathered.
    if (depth <= 0)
        return color(0, 0, 0);

    // If the ray hits nothing, return the background color.
    if (!world.hit(r, 0.001, infinity, rec))
        return background;

    ray scattered;
    color attenuation;
    color emitted = rec.mat_ptr->emitted(rec.u, rec.v, rec.p);

    if (!rec.mat_ptr->scatter(r, rec, attenuation, scattered))
        return emitted;

    return emitted + attenuation * ray_color(scattered, background, world, depth - 1);
}
#pragma endregion

#pragma region Thread stuff
void ThreadRender(int start, int end, std::vector<unsigned char>& image_buffer, image &img, camera &cam, hittable_list &world, std::vector<float> &finish_percentage, int id){
    for (int i = start; i < end; i++){
        for (int j = 0; j < img.image_width; j++){
            //Buffer Coordinate
            int pixel = i * img.image_width + j;

            //UV coordinates
            int uv_x = j;
            int uv_y = img.image_height - 1 - i;

            //Rendering
            // UV's
            //ray r(origin, lower_left_corner + u * horizontal + v * vertical - origin);
            //color pixel_color = ray_color(r, world);
            //write_color(image, pixel, pixel_color);

            color pixel_color(0, 0, 0);
            for (int s = 0; s < img.samples_per_pixel; ++s){
                auto u = double(uv_x + random_double()) / (img.image_width - 1);
                auto v = double(uv_y + random_double()) / (img.image_height - 1);
                ray r = cam.get_ray(u, v);
                pixel_color += ray_color(r, img.background_color, world, img.max_depth);
                
            }
            write_color(image_buffer, pixel, pixel_color, img.samples_per_pixel);

            if (LIVE_WINDOW_RENDER){
                mtx.lock();
                COLORREF win_color = RGB(image_buffer[pixel * 3], image_buffer[pixel * 3 + 1], image_buffer[pixel * 3 + 2]);
                // Set the color of the pixel at the specified coordinates
                SetPixel(globalHDC, j, i, win_color);
                //pixel++;
                mtx.unlock();
            }
        }

        if (i % (id + 1) != 0) continue;

        finish_percentage[id] = (float)(i - start) / (float)(end - start);
        float median = 0;
        for(int i = 0; i < finish_percentage.size(); i++)
        {
            median += finish_percentage[i];
        }
        median /= finish_percentage.size();
        median *= 100;
        mtx.lock();
        std::cout << "\r                         " << std::flush;
        std::cout << "\rRendering: "<< std::setprecision(5) << median << "%" << std::flush;
        mtx.unlock();
    }
}
#pragma endregion

int main(int argc, char** argv){
    image img;
    camera cam(point3(0, 0, 0), point3(0, 0, 0), vector3(0, 1, 0), 90, img.aspect_ratio, 0.1, 10, 0, 0);
    hittable_list world;
    Scenes scene = Scenes::Preloaded;

    std::string config_file_name = "config.txt";
    
    //Check if there is a given config file
    if (argc > 1){
        config_file_name = argv[1];
    }
    
    std::string scene_name = default_scenes[default_scenes.size() - 1];
    std::string cam_config = "";

    std::ifstream configFile(config_file_name);
    
    #pragma region reading configuration file
    if (configFile.is_open()){
        std::string line;
        while (std::getline(configFile, line)){
            // Search for the variable names and extract their corresponding values
            if (line.find("aspect_ratio") != std::string::npos){
                img.aspect_ratio = std::stod(line.substr(line.find('=') + 1));
            }else if (line.find("image_width") != std::string::npos){
                img.image_width = std::stoi(line.substr(line.find('=') + 1));
            }else if (line.find("max_color") != std::string::npos){
                img.max_color = std::stoi(line.substr(line.find('=') + 1));
            }else if (line.find("samples_per_pixel") != std::string::npos){
                img.samples_per_pixel = std::stoi(line.substr(line.find('=') + 1));
            }else if (line.find("max_depth") != std::string::npos){
                img.max_depth = std::stoi(line.substr(line.find('=') + 1));
            }else if (line.find("LIVE_WINDOW_RENDER") != std::string::npos){
                LIVE_WINDOW_RENDER = std::stoi(line.substr(line.find('=') + 1));
            }else if (line.find("scene_name") != std::string::npos){
                std::stringstream ss(line.substr(line.find('=') + 1));
                ss >> std::ws >> scene_name;
                scene = Scenes::Loaded;
                if (scene_name == "preloaded") {
                    ss >> std::ws >> scene_name;
                    scene = Scenes::Preloaded;
                }
            }else if (line.find("camera_configuration") != std::string::npos){
                std::stringstream ss(line.substr(line.find('=') + 1));
                ss >> std::ws >> cam_config;
            }else if (line.find("CONSOLE_DEBUG") != std::string::npos){
                std::stringstream ss(line.substr(line.find('=') + 1));
                ss >> CONSOLE_DEBUG;
            }
        }
        configFile.close();
        img.image_height = static_cast<int>(img.image_width / img.aspect_ratio);
    }else{
        std::cout << "Error: Unable to open " << config_file_name << std::endl;
    }
    #pragma endregion

    if(LoadScene(scene, world, scene_name, cam, cam_config, img) > 0){
        std::cerr << "Error loading scenes" << std::flush;
        return 1;
    }

    if (WaitForUserInput_Start() > 0) return 1;
    if (CONSOLE_DEBUG){
        std::cout << "Image configuration:" << std::endl;
        std::cout << "Size: " << img.image_height << "x" << img.image_width << std::endl;
        std::cout << "Ray bounce: " << img.max_depth << " Samples: " << img.samples_per_pixel << std::endl;
        std::cout << "Scene: " << scene_name << std::endl;
    }

    Render(cam, img, world);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)){
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout << "Program ended!" << std::endl;
    system("pause");
    return 0;
}

void Render(camera &cam, image &img, hittable_list &world){
    #pragma region Create Window
    if (LIVE_WINDOW_RENDER)
    {
        HWND hwnd = InitWindow(img.image_width, img.image_height);
        HDC hdc = GetDC(hwnd);
        globalHWND = hwnd;
        globalHDC = hdc;
    }
    #pragma endregion

    std::cerr << "\n\rStarting..." << std::flush;

    //Image buffer / data
    std::vector<unsigned char> image(img.image_width * img.image_height * 3);

    auto start_time = std::chrono::high_resolution_clock::now();

    #pragma region MultiThread
    const int numThreads = std::thread::hardware_concurrency() - 1;
    std::vector<std::thread> threads;
    std::vector<float> percentages(numThreads);

    // Calculate the workload for each thread
    int workload = img.image_height / numThreads;
    int start = 0;
    int end = 0;

    // Create and launch the threads
    for (int i = 0; i < numThreads; i++){
        start = i * workload;
        end = (i == numThreads - 1) ? img.image_height : (i + 1) * workload;

        threads.emplace_back(ThreadRender, start, end, std::ref(image), std::ref(img), std::ref(cam), std::ref(world), std::ref(percentages), i);
    }

    // Wait for all threads to finish
    for (std::thread& t : threads){
        t.join();
    }

    #pragma endregion

    auto end_time = std::chrono::high_resolution_clock::now();

    std::cout << "\r                         " << std::flush;
    std::cout << "\rRendering: 100%" << std::flush;

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.0;
    // Display the time taken
    std::cout << "\nTime taken: " << duration << " seconds" << std::endl;

    DrawBufferToWindow(globalHWND, globalHDC, img.image_width, img.image_height, image);

    //Create PNG
    DataToPng(img.pngImg, img.image_width, img.image_height, image);
    //Open PNG file
    OpenFile(img.pngImg);

    if (LIVE_WINDOW_RENDER){
        return;
    }

    HWND hwnd = InitWindow(img.image_width, img.image_height);
    HDC hdc = GetDC(hwnd);
    globalHWND = hwnd;
    globalHDC = hdc;

    if (DrawBufferToWindow(hwnd, hdc, img.image_width, img.image_height, image) == img.image_height) return;

    for (int i = 0; i < img.image_height; i++){
        for (int j = 0; j < img.image_width; j++){
            int pixel = i * img.image_width + j;
            COLORREF color = RGB(image[pixel * 3], image[pixel * 3 + 1], image[pixel * 3 + 2]);
            SetPixel(hdc, j, i, color);
        }
    }
}