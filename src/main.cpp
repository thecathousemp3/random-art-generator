#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <cwctype>
#include <vector>

#if defined(RANDOM_ART_GUI) && !defined(_WIN32)
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSpinBox>
#include <QStatusBar>
#include <QPixmap>
#include <QSize>
#include <QFont>
#include <QShortcut>
#include <QKeySequence>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#elif defined(RANDOM_ART_HAS_X11)
#include <X11/Xlib.h>
#endif

#ifdef RANDOM_ART_HAS_JPEG
extern "C" {
#include <jpeglib.h>
}
#endif

#if defined(RANDOM_ART_GUI) && defined(_WIN32)
#include <commdlg.h>
#endif

namespace {

std::uint64_t random_seed();

enum class PaletteStyle {
    Random,
    Warm,
    Cool,
    Earthy,
    Neon,
    Monochrome
};

struct Options {
    int width = 0;
    int height = 0;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    std::uint64_t seed = random_seed();
    std::string output = "random-art.png";
    PaletteStyle palette = PaletteStyle::Random;
    bool help = false;
};

struct Pixel {
    std::uint8_t r, g, b;
};

bool parse_positive(const std::string& value, int& result) {
    try {
        std::size_t consumed = 0;
        const long parsed = std::stol(value, &consumed);
        if (consumed != value.size() || parsed <= 0 ||
            parsed > std::numeric_limits<int>::max()) return false;
        result = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_palette_style(const std::string& value, PaletteStyle& style) {
    if (value == "random") style = PaletteStyle::Random;
    else if (value == "warm") style = PaletteStyle::Warm;
    else if (value == "cool") style = PaletteStyle::Cool;
    else if (value == "earthy") style = PaletteStyle::Earthy;
    else if (value == "neon") style = PaletteStyle::Neon;
    else if (value == "monochrome") style = PaletteStyle::Monochrome;
    else return false;
    return true;
}

bool detect_display_size(int& width, int& height) {
#ifdef _WIN32
    DEVMODE mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &mode) ||
        mode.dmPelsWidth == 0 || mode.dmPelsHeight == 0 ||
        mode.dmPelsWidth > static_cast<DWORD>(std::numeric_limits<int>::max()) ||
        mode.dmPelsHeight > static_cast<DWORD>(std::numeric_limits<int>::max())) {
        return false;
    }
    width = static_cast<int>(mode.dmPelsWidth);
    height = static_cast<int>(mode.dmPelsHeight);
    return true;
#elif defined(__APPLE__)
    const CGDirectDisplayID display = CGMainDisplayID();
    const std::size_t display_width = CGDisplayPixelsWide(display);
    const std::size_t display_height = CGDisplayPixelsHigh(display);
    if (display_width == 0 || display_height == 0 ||
        display_width > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        display_height > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    width = static_cast<int>(display_width);
    height = static_cast<int>(display_height);
    return true;
#elif defined(RANDOM_ART_HAS_X11)
    Display* display = XOpenDisplay(nullptr);
    if (!display) return false;
    const int screen = DefaultScreen(display);
    const int detected_width = DisplayWidth(display, screen);
    const int detected_height = DisplayHeight(display, screen);
    XCloseDisplay(display);
    if (detected_width <= 0 || detected_height <= 0) return false;
    width = detected_width;
    height = detected_height;
    return true;
#else
    (void)width;
    (void)height;
    return false;
#endif
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--width" || arg == "--height" ||
                   arg == "--threads" || arg == "--seed" || arg == "--output" ||
                   arg == "--palette") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << arg << '\n';
                return false;
            }
            const std::string value = argv[++i];
            if (arg == "--width" && !parse_positive(value, options.width)) {
                std::cerr << "Width must be a positive integer.\n"; return false;
            }
            if (arg == "--height" && !parse_positive(value, options.height)) {
                std::cerr << "Height must be a positive integer.\n"; return false;
            }
            if (arg == "--threads") {
                int count = 0;
                if (!parse_positive(value, count) || count > 1024) {
                    std::cerr << "Threads must be an integer from 1 to 1024.\n"; return false;
                }
                options.threads = static_cast<unsigned>(count);
            }
            if (arg == "--seed") {
                try {
                    std::size_t consumed = 0;
                    options.seed = std::stoull(value, &consumed);
                    if (consumed != value.size()) throw std::invalid_argument("seed");
                } catch (const std::exception&) {
                    std::cerr << "Seed must be an unsigned integer.\n"; return false;
                }
            }
            if (arg == "--output") options.output = value;
            if (arg == "--palette" && !parse_palette_style(value, options.palette)) {
                std::cerr << "Palette must be one of: random, warm, cool, earthy, neon, monochrome.\n";
                return false;
            }
        } else {
            std::cerr << "Unknown option: " << arg << '\n';
            return false;
        }
    }
    if (options.help) return true;
    if (options.width == 0 || options.height == 0) {
        int display_width = 0, display_height = 0;
        if (!detect_display_size(display_width, display_height)) {
            std::cerr << "Could not detect a display resolution. Specify both "
                         "--width and --height (headless/mobile systems may not "
                         "provide a desktop display API).\n";
            return false;
        }
        if (options.width == 0) options.width = display_width;
        if (options.height == 0) options.height = display_height;
        std::cout << "Using display resolution " << options.width << "x"
                  << options.height << " pixels.\n";
    }
    const auto pixels = static_cast<std::uint64_t>(options.width) *
                        static_cast<std::uint64_t>(options.height);
    if (pixels > 300000000ULL) {
        std::cerr << "Requested image is too large (maximum 300 million pixels).\n";
        return false;
    }
    return true;
}

std::uint64_t mix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

std::uint64_t random_seed() {
    std::random_device entropy;
    return (static_cast<std::uint64_t>(entropy()) << 32) | entropy();
}

std::array<float, 3> hsv_to_rgb(float hue, float saturation, float value) {
    hue -= std::floor(hue);
    const float chroma = value * saturation;
    const float section = hue * 6.0f;
    const float intermediate = chroma * (1.0f - std::abs(std::fmod(section, 2.0f) - 1.0f));
    std::array<float, 3> rgb{};
    if (section < 1.0f) rgb = {chroma, intermediate, 0.0f};
    else if (section < 2.0f) rgb = {intermediate, chroma, 0.0f};
    else if (section < 3.0f) rgb = {0.0f, chroma, intermediate};
    else if (section < 4.0f) rgb = {0.0f, intermediate, chroma};
    else if (section < 5.0f) rgb = {intermediate, 0.0f, chroma};
    else rgb = {chroma, 0.0f, intermediate};
    const float offset = value - chroma;
    for (auto& channel : rgb) channel += offset;
    return rgb;
}

std::array<std::array<float, 3>, 4> make_palette(
    PaletteStyle style, std::mt19937_64& random) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::array<std::array<float, 3>, 4> palette{};
    if (style == PaletteStyle::Random) {
        for (auto& color : palette) {
            for (auto& channel : color) channel = unit(random);
        }
        return palette;
    }

    if (style == PaletteStyle::Monochrome) {
        for (auto& color : palette) {
            const float value = 0.08f + unit(random) * 0.84f;
            color = {value, value, value};
        }
        return palette;
    }

    float hueMin = 0.0f;
    float hueMax = 1.0f;
    float hueSpread = 0.12f;
    float saturationMin = 0.65f;
    float saturationMax = 1.0f;
    float valueMin = 0.55f;
    float valueMax = 1.0f;
    switch (style) {
    case PaletteStyle::Warm:
        hueMin = 0.01f;
        hueMax = 0.15f;
        hueSpread = 0.075f;
        saturationMin = 0.55f;
        saturationMax = 0.95f;
        valueMin = 0.55f;
        valueMax = 1.0f;
        break;
    case PaletteStyle::Cool:
        hueMin = 0.50f;
        hueMax = 0.76f;
        hueSpread = 0.09f;
        saturationMin = 0.45f;
        saturationMax = 0.9f;
        valueMin = 0.45f;
        valueMax = 0.95f;
        break;
    case PaletteStyle::Earthy:
        hueMin = 0.06f;
        hueMax = 0.23f;
        hueSpread = 0.065f;
        saturationMin = 0.3f;
        saturationMax = 0.75f;
        valueMin = 0.28f;
        valueMax = 0.85f;
        break;
    case PaletteStyle::Neon:
        hueSpread = 0.16f;
        saturationMin = 0.88f;
        saturationMax = 1.0f;
        valueMin = 0.78f;
        valueMax = 1.0f;
        break;
    case PaletteStyle::Random:
    case PaletteStyle::Monochrome:
        break;
    }

    const float baseHue = hueMin + unit(random) * (hueMax - hueMin);
    const float direction = unit(random) < 0.5f ? -1.0f : 1.0f;
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const float position = static_cast<float>(i) / (palette.size() - 1) - 0.5f;
        const float hue = baseHue + direction * position * hueSpread
                        + (unit(random) - 0.5f) * hueSpread * 0.3f;
        const float saturation = saturationMin + unit(random) * (saturationMax - saturationMin);
        const float value = valueMin + unit(random) * (valueMax - valueMin);
        palette[i] = hsv_to_rgb(hue, saturation, value);
    }
    return palette;
}

float value_noise(std::uint64_t seed, float x, float y) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - x0;
    const float fy = y - y0;
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    auto lattice = [seed](int px, int py) {
        const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(px)) << 32)
                       | static_cast<std::uint32_t>(py);
        const auto bits = mix(seed ^ key);
        return static_cast<float>((bits >> 40) & 0xffffU) / 32767.5f - 1.0f;
    };
    const float top = lattice(x0, y0) * (1.0f - sx) + lattice(x0 + 1, y0) * sx;
    const float bottom = lattice(x0, y0 + 1) * (1.0f - sx) + lattice(x0 + 1, y0 + 1) * sx;
    return top * (1.0f - sy) + bottom * sy;
}

std::vector<Pixel> render(const Options& options) {
    std::mt19937_64 random(options.seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);
    const auto palette = make_palette(options.palette, random);
    const float phaseA = unit(random) * 6.2831853f;
    const float phaseB = unit(random) * 6.2831853f;
    const float frequencyX = 2.0f + unit(random) * 16.0f;
    const float frequencyY = 2.0f + unit(random) * 16.0f;
    const float wavePhaseX = unit(random) * 6.2831853f;
    const float wavePhaseY = unit(random) * 6.2831853f;
    const float waveFrequencyX = 2.0f + unit(random) * 14.0f;
    const float waveFrequencyY = 2.0f + unit(random) * 14.0f;
    const float waveRotation = unit(random) * 6.2831853f;
    const float warpX = unit(random) * 1.1f;
    const float warpY = unit(random) * 1.1f;
    const float rotation = unit(random) * 6.2831853f;
    const float cellFrequency = 4.0f + unit(random) * 20.0f;
    const float cellPhase = unit(random) * 6.2831853f;
    const float turbulenceFrequencyX = 2.0f + unit(random) * 14.0f;
    const float turbulenceFrequencyY = 2.0f + unit(random) * 14.0f;
    const float turbulencePhase = unit(random) * 6.2831853f;
    const float stripeRotation = unit(random) * 6.2831853f;
    const float stripeFrequencyX = 2.0f + unit(random) * 18.0f;
    const float stripeFrequencyY = 2.0f + unit(random) * 18.0f;
    const float stripePhase = unit(random) * 6.2831853f;
    const float centerX = signedUnit(random) * 0.8f;
    const float centerY = signedUnit(random) * 0.8f;
    const int lobes = std::uniform_int_distribution<int>(2, 15)(random);
    const int tileSize = std::uniform_int_distribution<int>(4, 64)(random);

    struct Blob {
        float x, y, radius, weight;
    };
    std::array<Blob, 7> blobs{};
    for (auto& blob : blobs) {
        blob = {signedUnit(random) * 1.2f, signedUnit(random) * 1.2f,
                0.12f + unit(random) * 0.62f, signedUnit(random)};
    }

    struct Cell {
        float x, y;
    };
    std::array<Cell, 9> cells{};
    for (auto& cell : cells) {
        cell = {signedUnit(random) * 1.5f, signedUnit(random) * 1.5f};
    }

    struct Stroke {
        float x1, y1, x2, y2, width, weight;
    };
    std::array<Stroke, 8> strokes{};
    for (auto& stroke : strokes) {
        stroke = {signedUnit(random) * 1.4f, signedUnit(random) * 1.4f,
                  signedUnit(random) * 1.4f, signedUnit(random) * 1.4f,
                  0.015f + unit(random) * 0.12f, signedUnit(random)};
    }

    struct Shape {
        float x, y, radius, aspect, weight, cosine, sine;
        int type;
    };
    std::array<Shape, 6> shapes{};
    for (auto& shape : shapes) {
        const float shapeRotation = unit(random) * 6.2831853f;
        shape = {signedUnit(random) * 1.2f, signedUnit(random) * 1.2f,
                 0.1f + unit(random) * 0.5f, 0.45f + unit(random) * 1.3f,
                 signedUnit(random), std::cos(shapeRotation), std::sin(shapeRotation),
                 std::uniform_int_distribution<int>(0, 2)(random)};
    }

    const float fractalX = signedUnit(random) * 0.8f;
    const float fractalY = signedUnit(random) * 0.8f;
    const float fractalScale = 0.35f + unit(random) * 0.9f;

    const auto count = static_cast<std::size_t>(options.width) * options.height;
    std::vector<Pixel> image(count);
    std::atomic<int> next_row{0};
    std::vector<std::thread> workers;
    workers.reserve(options.threads);
    const float aspect = static_cast<float>(options.width) / options.height;
    const float cs = std::cos(rotation), sn = std::sin(rotation);
    const float waveCs = std::cos(waveRotation), waveSn = std::sin(waveRotation);
    const float stripeCs = std::cos(stripeRotation), stripeSn = std::sin(stripeRotation);
    for (unsigned worker = 0; worker < options.threads; ++worker) {
        workers.emplace_back([&, worker] {
            (void)worker;
            for (;;) {
                const int y = next_row.fetch_add(1, std::memory_order_relaxed);
                if (y >= options.height) break;
                for (int x = 0; x < options.width; ++x) {
                    const float u = (2.0f * x / options.width - 1.0f) * aspect;
                    const float v = 2.0f * y / options.height - 1.0f;
                    const float rotatedX = cs * u - sn * v;
                    const float rotatedY = sn * u + cs * v;
                    const float waveX = waveCs * u - waveSn * v;
                    const float waveY = waveSn * u + waveCs * v;
                    const float rx = rotatedX +
                        std::sin(rotatedY * frequencyY + phaseA) * warpX;
                    const float ry = rotatedY +
                        std::cos(rotatedX * frequencyX + phaseB) * warpY;
                    const float radius = std::sqrt(rx * rx + ry * ry);
                    const float angle = std::atan2(ry - centerY, rx - centerX);
                    const float spiral = 0.55f * std::sin(frequencyX * radius +
                        std::sin(angle * lobes + phaseA) * frequencyY)
                        + 0.45f * std::cos(angle * (lobes + 3) +
                                           radius * frequencyY + phaseB);
                    const float waves =
                        std::sin(waveX * waveFrequencyX +
                                 std::sin(waveY * waveFrequencyY + wavePhaseX)) *
                        std::cos(waveY * waveFrequencyY +
                                 std::sin(waveX * (waveFrequencyX * 0.6f) + wavePhaseY));

                    float blobField = 0.0f;
                    for (const auto& blob : blobs) {
                        const float dx = u - blob.x, dy = v - blob.y;
                        blobField += blob.weight *
                            std::exp(-(dx * dx + dy * dy) / (blob.radius * blob.radius));
                    }
                    blobField = std::tanh(blobField * 1.3f);

                    float nearest = std::numeric_limits<float>::max();
                    float second = nearest;
                    for (const auto& cell : cells) {
                        const float dx = rotatedX - cell.x, dy = rotatedY - cell.y;
                        const float distance = dx * dx + dy * dy;
                        if (distance < nearest) {
                            second = nearest;
                            nearest = distance;
                        } else if (distance < second) {
                            second = distance;
                        }
                    }
                    const float cellular = 0.72f * std::sin(
                        (std::sqrt(second) - std::sqrt(nearest)) * cellFrequency + cellPhase)
                        + 0.28f * std::cos(std::sqrt(nearest) * cellFrequency * 0.65f - cellPhase);
                    const float turbulence =
                        0.62f * std::sin((rx * ry +
                            std::sin(ry * turbulenceFrequencyY + turbulencePhase))
                            * turbulenceFrequencyX * 2.0f)
                        + 0.38f * std::cos((rx * rx - ry * ry) *
                                           turbulenceFrequencyY * 2.0f - turbulencePhase);

                    const auto tileX = static_cast<std::uint64_t>(x / tileSize);
                    const auto tileY = static_cast<std::uint64_t>(y / tileSize);
                    const auto tile = mix(options.seed ^ (tileX << 32) ^ tileY);
                    const float mosaic =
                        static_cast<float>((tile >> 40) & 0xffffU) / 32767.5f - 1.0f;

                    const float across = u * stripeCs + v * stripeSn;
                    const float along = -u * stripeSn + v * stripeCs;
                    const float stripes = std::sin(across * stripeFrequencyX +
                        std::sin(along * stripeFrequencyY + stripePhase) * 2.0f);

                    float strokeField = 0.0f;
                    for (const auto& stroke : strokes) {
                        const float vx = stroke.x2 - stroke.x1;
                        const float vy = stroke.y2 - stroke.y1;
                        const float lengthSquared = std::max(vx * vx + vy * vy, 0.0001f);
                        const float projection = std::clamp(
                            ((u - stroke.x1) * vx + (v - stroke.y1) * vy) / lengthSquared,
                            0.0f, 1.0f);
                        const float dx = u - (stroke.x1 + projection * vx);
                        const float dy = v - (stroke.y1 + projection * vy);
                        strokeField += stroke.weight *
                            std::exp(-(dx * dx + dy * dy) /
                                     (stroke.width * stroke.width));
                    }
                    strokeField = std::tanh(strokeField);

                    float geometryField = 0.0f;
                    for (const auto& shape : shapes) {
                        const float dx = u - shape.x, dy = v - shape.y;
                        const float sx = (shape.cosine * dx - shape.sine * dy) / shape.aspect;
                        const float sy = shape.sine * dx + shape.cosine * dy;
                        float distance = 0.0f;
                        if (shape.type == 0) {
                            distance = std::sqrt(sx * sx + sy * sy) - shape.radius;
                        } else if (shape.type == 1) {
                            const float qx = std::abs(sx) - shape.radius;
                            const float qy = std::abs(sy) - shape.radius * shape.aspect;
                            distance = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                                                 std::max(qy, 0.0f) * std::max(qy, 0.0f))
                                      + std::min(std::max(qx, qy), 0.0f);
                        } else {
                            distance = (std::abs(sx) + std::abs(sy)) * 0.7071f - shape.radius;
                        }
                        geometryField += shape.weight * (distance < 0.0f ? 1.0f : -1.0f);
                    }
                    geometryField = std::tanh(geometryField * 0.65f);

                    float fractalXValue = 0.0f, fractalYValue = 0.0f;
                    const float cx = u * fractalScale + fractalX;
                    const float cy = v * fractalScale + fractalY;
                    int iterations = 0;
                    for (; iterations < 7; ++iterations) {
                        const float nextX = fractalXValue * fractalXValue -
                                            fractalYValue * fractalYValue + cx;
                        fractalYValue = 2.0f * fractalXValue * fractalYValue + cy;
                        fractalXValue = nextX;
                        if (fractalXValue * fractalXValue + fractalYValue * fractalYValue > 4.0f)
                            break;
                    }
                    const float fractal = static_cast<float>(iterations) / 3.5f - 1.0f;

                    const std::array<float, 10> fields = {
                        spiral, waves, blobField, cellular, turbulence, mosaic,
                        stripes, strokeField, geometryField, fractal
                    };
                    float structuredField = 0.0f;
                    for (const float component : fields) {
                        structuredField += component;
                    }
                    structuredField /= static_cast<float>(fields.size());

                    const auto pixel = static_cast<std::uint64_t>(y) * options.width + x;
                    const auto pixelHash = mix(options.seed ^ pixel);
                    const float fineNoise =
                        static_cast<float>(pixelHash & 0xffffU) / 32767.5f - 1.0f;
                    const auto coarseHash = mix(options.seed ^
                        ((static_cast<std::uint64_t>(x / 8) << 32) |
                         static_cast<std::uint64_t>(y / 8)));
                    const float coarseNoise =
                        static_cast<float>((coarseHash >> 16) & 0xffffU) / 32767.5f - 1.0f;
                    const float randomGrain = fineNoise * 0.65f + coarseNoise * 0.35f;
                    const float fractalNoise =
                        value_noise(options.seed ^ 0x243f6a8885a308d3ULL, rx * 1.4f, ry * 1.4f)
                            * 0.57f
                        + value_noise(options.seed ^ 0x13198a2e03707344ULL, rx * 2.8f, ry * 2.8f)
                            * 0.29f
                        + value_noise(options.seed ^ 0xa4093822299f31d0ULL, rx * 5.6f, ry * 5.6f)
                            * 0.14f;
                    const float noiseField = (randomGrain + fractalNoise) * 0.5f;
                    const float field = structuredField * 0.75f + noiseField * 0.25f;
                    float t = std::clamp(
                        (field + 1.0f) * 0.5f,
                        0.0f, 0.9999f) * 3.0f;
                    const int segment = static_cast<int>(t);
                    const float blend = t - segment;
                    const auto& a = palette[segment];
                    const auto& b = palette[(segment + 1) % palette.size()];
                    auto channel = [&](int c) {
                        const float value = a[c] * (1.0f - blend) + b[c] * blend;
                        return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
                    };
                    image[static_cast<std::size_t>(y) * options.width + x] =
                        {channel(0), channel(1), channel(2)};
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    return image;
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xffffffffU;
}

void png_chunk(std::vector<std::uint8_t>& png, const char* type,
               const std::vector<std::uint8_t>& data) {
    append_u32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = png.size();
    for (int i = 0; i < 4; ++i) png.push_back(static_cast<std::uint8_t>(type[i]));
    png.insert(png.end(), data.begin(), data.end());
    append_u32(png, crc32(png.data() + start, png.size() - start));
}

bool save_png(const std::string& path, const std::vector<Pixel>& image, int width, int height) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 3));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            const Pixel p = image[static_cast<std::size_t>(y) * width + x];
            raw.insert(raw.end(), {p.r, p.g, p.b});
        }
    }
    std::vector<std::uint8_t> compressed{0x78, 0x01};
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t block = std::min<std::size_t>(65535, raw.size() - offset);
        compressed.push_back(offset + block == raw.size() ? 1 : 0);
        const auto n = static_cast<std::uint16_t>(block);
        compressed.push_back(static_cast<std::uint8_t>(n));
        compressed.push_back(static_cast<std::uint8_t>(n >> 8));
        const auto inverse = static_cast<std::uint16_t>(~n);
        compressed.push_back(static_cast<std::uint8_t>(inverse));
        compressed.push_back(static_cast<std::uint8_t>(inverse >> 8));
        compressed.insert(compressed.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                          raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
        offset += block;
    }
    std::uint32_t a = 1, b = 0;
    for (const auto byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    append_u32(compressed, (b << 16) | a);

    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> header;
    append_u32(header, static_cast<std::uint32_t>(width));
    append_u32(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0});
    png_chunk(png, "IHDR", header);
    png_chunk(png, "IDAT", compressed);
    png_chunk(png, "IEND", {});
    std::ofstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(png.data()),
               static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(file);
}

#ifdef RANDOM_ART_HAS_JPEG
bool save_jpeg(const std::string& path, const std::vector<Pixel>& image,
               int width, int height) {
#ifdef _WIN32
    FILE* file = _wfopen(std::filesystem::u8path(path).c_str(), L"wb");
#else
    FILE* file = std::fopen(path.c_str(), "wb");
#endif
    if (!file) return false;
    jpeg_compress_struct encoder{};
    jpeg_error_mgr errors{};
    encoder.err = jpeg_std_error(&errors);
    jpeg_create_compress(&encoder);
    jpeg_stdio_dest(&encoder, file);
    encoder.image_width = static_cast<JDIMENSION>(width);
    encoder.image_height = static_cast<JDIMENSION>(height);
    encoder.input_components = 3;
    encoder.in_color_space = JCS_RGB;
    jpeg_set_defaults(&encoder);
    jpeg_set_quality(&encoder, 98, TRUE);
    jpeg_start_compress(&encoder, TRUE);
    while (encoder.next_scanline < encoder.image_height) {
        const auto* row = reinterpret_cast<const JSAMPLE*>(
            image.data() + static_cast<std::size_t>(encoder.next_scanline) * width);
        JSAMPROW rows[] = {const_cast<JSAMPROW>(row)};
        jpeg_write_scanlines(&encoder, rows, 1);
    }
    jpeg_finish_compress(&encoder);
    jpeg_destroy_compress(&encoder);
    const bool success = std::fclose(file) == 0;
    return success;
}
#endif

void print_help() {
    std::cout << "Random Art Generator - deterministic, multithreaded abstract art\n"
              << "Usage: random_art_generator [options]\n"
              << "  --width N     Image width (default: detected display width)\n"
              << "  --height N    Image height (default: detected display height)\n"
              << "  --output FILE Output .png or .jpg file\n"
              << "  --seed N      Reproduce a particular image\n"
              << "  --threads N   CPU worker count (default: detected cores)\n"
              << "  --palette NAME Palette style: random, warm, cool, earthy, neon, monochrome\n"
              << "  --help        Show this help\n"
              << "  Missing dimensions default to the detected primary display size.\n"
              << "Images are limited to 300 million pixels to protect memory.\n";
}

#if defined(RANDOM_ART_GUI) && defined(_WIN32)
constexpr UINT kRenderFinished = WM_APP + 1;
constexpr int kWidthControl = 101;
constexpr int kHeightControl = 102;
constexpr int kPaletteControl = 103;
constexpr int kGenerateControl = 104;
constexpr int kSaveControl = 105;

struct RenderResult {
    std::vector<Pixel> pixels;
    int width = 0;
    int height = 0;
    std::string error;
};

HWND g_window = nullptr;
HWND g_widthEdit = nullptr;
HWND g_heightEdit = nullptr;
HWND g_paletteCombo = nullptr;
HWND g_generateButton = nullptr;
HWND g_saveButton = nullptr;
HWND g_status = nullptr;
HBRUSH g_blackBrush = nullptr;
HFONT g_pixelFont = nullptr;
std::thread g_renderThread;
std::mutex g_resultMutex;
std::unique_ptr<RenderResult> g_renderResult;
std::vector<Pixel> g_pixels;
std::vector<std::uint8_t> g_bgrPixels;
int g_imageWidth = 0;
int g_imageHeight = 0;
int g_bitmapStride = 0;
int g_nativeWidth = 0;
int g_nativeHeight = 0;

void set_status(const wchar_t* text) {
    SetWindowTextW(g_status, text);
}

void start_win32_generation() {
    wchar_t text[64]{};
    GetWindowTextW(g_widthEdit, text, 64);
    const unsigned long long parsedWidth = _wcstoui64(text, nullptr, 10);
    GetWindowTextW(g_heightEdit, text, 64);
    const unsigned long long parsedHeight = _wcstoui64(text, nullptr, 10);
    if (parsedWidth == 0 || parsedHeight == 0 ||
        parsedWidth > static_cast<unsigned long long>(std::numeric_limits<int>::max()) ||
        parsedHeight > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
        MessageBoxW(g_window, L"Enter positive integer dimensions.", L"Invalid dimensions",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    const int width = static_cast<int>(parsedWidth);
    const int height = static_cast<int>(parsedHeight);
    if (static_cast<std::uint64_t>(width) * height > 300000000ULL) {
        MessageBoxW(g_window, L"Choose dimensions with at most 300 million pixels.",
                    L"Image too large", MB_OK | MB_ICONWARNING);
        return;
    }
    const LRESULT paletteIndex = SendMessageW(g_paletteCombo, CB_GETCURSEL, 0, 0);
    if (paletteIndex < 0 ||
        paletteIndex > static_cast<LRESULT>(PaletteStyle::Monochrome)) {
        MessageBoxW(g_window, L"Choose a palette style.", L"Invalid palette",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    const auto seed = random_seed();

    if (g_renderThread.joinable()) g_renderThread.join();
    Options options;
    options.width = width;
    options.height = height;
    options.seed = seed;
    options.palette = static_cast<PaletteStyle>(paletteIndex);
    options.threads = std::max(1u, std::thread::hardware_concurrency());
    EnableWindow(g_generateButton, FALSE);
    EnableWindow(g_saveButton, FALSE);
    set_status(L"Blending all art algorithms...");
    InvalidateRect(g_window, nullptr, TRUE);

    g_renderThread = std::thread([options] {
        auto result = std::make_unique<RenderResult>();
        result->width = options.width;
        result->height = options.height;
        try {
            result->pixels = render(options);
        } catch (const std::exception& error) {
            result->error = error.what();
        }
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            g_renderResult = std::move(result);
        }
        PostMessageW(g_window, kRenderFinished, 0, 0);
    });
}

void save_win32_image() {
    std::vector<wchar_t> filename(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"PNG image (*.png)\0*.png\0JPEG image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0";
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return;

    std::wstring extension = std::filesystem::path(filename.data()).extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    const bool jpeg = extension == L".jpg" || extension == L".jpeg";
#ifndef RANDOM_ART_HAS_JPEG
    if (jpeg) {
        MessageBoxW(g_window, L"This build lacks JPEG support. Save as PNG instead.",
                    L"JPEG unavailable", MB_OK | MB_ICONWARNING);
        return;
    }
#endif

    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, filename.data(), -1,
                                         nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        MessageBoxW(g_window, L"Could not read the selected file path.", L"Save failed",
                    MB_OK | MB_ICONERROR);
        return;
    }
    std::string path(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, filename.data(), -1,
                        path.data(), bytes, nullptr, nullptr);
    path.pop_back();
    bool saved = false;
    if (jpeg) {
#ifdef RANDOM_ART_HAS_JPEG
        saved = save_jpeg(path, g_pixels, g_imageWidth, g_imageHeight);
#endif
    } else {
        saved = save_png(path, g_pixels, g_imageWidth, g_imageHeight);
    }
    if (!saved) {
        MessageBoxW(g_window, L"Could not save the image to the selected file.",
                    L"Save failed", MB_OK | MB_ICONERROR);
        return;
    }
    set_status(L"Image saved.");
}

LRESULT CALLBACK random_art_window_proc(HWND window, UINT message,
                                        WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        auto make = [&](const wchar_t* type, const wchar_t* label, DWORD style,
                        int x, int y, int width, int height, int id) {
            HWND control = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style,
                                           x, y, width, height, window,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                           instance, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_pixelFont), TRUE);
            return control;
        };
        make(L"STATIC", L"Width", 0, 18, 19, 42, 24, 0);
        g_widthEdit = make(L"EDIT", L"", WS_BORDER | ES_NUMBER, 62, 16, 95, 28,
                           kWidthControl);
        make(L"STATIC", L"Height", 0, 170, 19, 48, 24, 0);
        g_heightEdit = make(L"EDIT", L"", WS_BORDER | ES_NUMBER, 220, 16, 95, 28,
                            kHeightControl);
        make(L"STATIC", L"Palette", 0, 328, 19, 48, 24, 0);
        g_paletteCombo = make(L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST |
                              WS_VSCROLL, 382, 16, 138, 160, kPaletteControl);
        const wchar_t* paletteNames[] = {
            L"Random", L"Warm", L"Cool", L"Earthy", L"Neon", L"Monochrome"
        };
        for (const auto* paletteName : paletteNames) {
            SendMessageW(g_paletteCombo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(paletteName));
        }
        SendMessageW(g_paletteCombo, CB_SETCURSEL,
                     static_cast<WPARAM>(PaletteStyle::Random), 0);
        g_generateButton = make(L"BUTTON", L"Generate art", BS_OWNERDRAW,
                                540, 16, 130, 28, kGenerateControl);
        g_saveButton = make(L"BUTTON", L"Save image...", BS_OWNERDRAW,
                            686, 16, 140, 28, kSaveControl);
        g_status = make(L"STATIC", L"Ready", 0, 18, 52, 820, 22, 0);
        wchar_t value[32]{};
        _itow_s(g_nativeWidth, value, _countof(value), 10);
        SetWindowTextW(g_widthEdit, value);
        _itow_s(g_nativeHeight, value, _countof(value), 10);
        SetWindowTextW(g_heightEdit, value);
        EnableWindow(g_saveButton, FALSE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kGenerateControl:
            start_win32_generation();
            return 0;
        case kSaveControl:
            save_win32_image();
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkColor(dc, RGB(0, 0, 0));
        return reinterpret_cast<LRESULT>(g_blackBrush);
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        FillRect(item->hDC, &item->rcItem, g_blackBrush);
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        const auto oldPen = SelectObject(item->hDC, pen);
        const auto oldBrush = SelectObject(item->hDC, GetStockObject(HOLLOW_BRUSH));
        Rectangle(item->hDC, item->rcItem.left + 1, item->rcItem.top + 1,
                  item->rcItem.right - 1, item->rcItem.bottom - 1);
        wchar_t label[64]{};
        GetWindowTextW(item->hwndItem, label, _countof(label));
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, RGB(255, 255, 255));
        DrawTextW(item->hDC, label, -1, const_cast<RECT*>(&item->rcItem),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);
        DeleteObject(pen);
        return TRUE;
    }
    case WM_ERASEBKGND: {
        RECT area{};
        GetClientRect(window, &area);
        FillRect(reinterpret_cast<HDC>(wParam), &area, g_blackBrush);
        return TRUE;
    }
    case kRenderFinished: {
        if (g_renderThread.joinable()) g_renderThread.join();
        std::unique_ptr<RenderResult> result;
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            result = std::move(g_renderResult);
        }
        EnableWindow(g_generateButton, TRUE);
        if (!result) {
            set_status(L"Generation failed.");
            return 0;
        }
        if (!result->error.empty()) {
            MessageBoxA(window, result->error.c_str(), "Generation failed",
                        MB_OK | MB_ICONERROR);
            set_status(L"Generation failed.");
            return 0;
        }
        g_imageWidth = result->width;
        g_imageHeight = result->height;
        g_pixels = std::move(result->pixels);
        g_bitmapStride = (g_imageWidth * 3 + 3) & ~3;
        g_bgrPixels.assign(static_cast<std::size_t>(g_bitmapStride) * g_imageHeight, 0);
        for (int y = 0; y < g_imageHeight; ++y) {
            for (int x = 0; x < g_imageWidth; ++x) {
                const auto& pixel = g_pixels[static_cast<std::size_t>(y) * g_imageWidth + x];
                const auto offset = static_cast<std::size_t>(y) * g_bitmapStride + x * 3;
                g_bgrPixels[offset] = pixel.b;
                g_bgrPixels[offset + 1] = pixel.g;
                g_bgrPixels[offset + 2] = pixel.r;
            }
        }
        EnableWindow(g_saveButton, TRUE);
        const std::wstring ready = L"All algorithms blended | Use Save image to export PNG or JPEG.";
        set_status(ready.c_str());
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT area{};
        GetClientRect(window, &area);
        area.top = 82;
        FillRect(dc, &area, g_blackBrush);
        if (!g_pixels.empty()) {
            const int availableWidth = area.right - area.left;
            const int availableHeight = area.bottom - area.top;
            const double scale = std::max(
                static_cast<double>(availableWidth) / g_imageWidth,
                static_cast<double>(availableHeight) / g_imageHeight);
            const int drawWidth = static_cast<int>(g_imageWidth * scale);
            const int drawHeight = static_cast<int>(g_imageHeight * scale);
            const int x = (area.left + area.right - drawWidth) / 2;
            const int y = (area.top + area.bottom - drawHeight) / 2;
            BITMAPINFO bitmap{};
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap.bmiHeader.biWidth = g_imageWidth;
            bitmap.bmiHeader.biHeight = -g_imageHeight;
            bitmap.bmiHeader.biPlanes = 1;
            bitmap.bmiHeader.biBitCount = 24;
            bitmap.bmiHeader.biCompression = BI_RGB;
            bitmap.bmiHeader.biSizeImage =
                static_cast<DWORD>(g_bitmapStride * g_imageHeight);
            SetStretchBltMode(dc, HALFTONE);
            const int savedDc = SaveDC(dc);
            IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
            StretchDIBits(dc, x, y, drawWidth, drawHeight, 0, 0,
                          g_imageWidth, g_imageHeight, g_bgrPixels.data(),
                          &bitmap, DIB_RGB_COLORS, SRCCOPY);
            RestoreDC(dc, savedDc);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        if (g_renderThread.joinable()) g_renderThread.join();
        if (g_blackBrush) DeleteObject(g_blackBrush);
        if (g_pixelFont) DeleteObject(g_pixelFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
#elif defined(RANDOM_ART_GUI)
class ImagePreview final : public QLabel {
public:
    explicit ImagePreview(QWidget* parent = nullptr) : QLabel(parent) {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(320, 240);
        setStyleSheet("QLabel { background: #000000; color: #ffffff; border: none; }");
        setText("Your generated artwork will appear here");
    }

    void setImage(const QImage& image) {
        image_ = image;
        updatePreview();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        updatePreview();
    }

private:
    void updatePreview() {
        if (!image_.isNull()) {
            setPixmap(QPixmap::fromImage(image_).scaled(
                size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        }
    }

    QImage image_;
};

class ArtWindow final : public QMainWindow {
public:
    ArtWindow(int nativeWidth, int nativeHeight) {
        setWindowTitle("Random Art Generator");
        resize(1100, 760);
        setMinimumSize(640, 480);

        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* controls = new QHBoxLayout();

        width_ = new QSpinBox(central);
        width_->setRange(1, 100000);
        width_->setValue(nativeWidth);
        width_->setPrefix("Width ");
        height_ = new QSpinBox(central);
        height_->setRange(1, 100000);
        height_->setValue(nativeHeight);
        height_->setPrefix("Height ");
        palette_ = new QComboBox(central);
        palette_->addItem("Random", static_cast<int>(PaletteStyle::Random));
        palette_->addItem("Warm", static_cast<int>(PaletteStyle::Warm));
        palette_->addItem("Cool", static_cast<int>(PaletteStyle::Cool));
        palette_->addItem("Earthy", static_cast<int>(PaletteStyle::Earthy));
        palette_->addItem("Neon", static_cast<int>(PaletteStyle::Neon));
        palette_->addItem("Monochrome", static_cast<int>(PaletteStyle::Monochrome));

        auto* generate = new QPushButton("Generate art", central);
        save_ = new QPushButton("Save image…", central);
        save_->setEnabled(false);
        controls->addWidget(width_);
        controls->addWidget(height_);
        controls->addWidget(new QLabel("Palette", central));
        controls->addWidget(palette_);
        controls->addStretch();
        controls->addWidget(generate);
        controls->addWidget(save_);

        preview_ = new ImagePreview(central);
        layout->addLayout(controls);
        layout->addWidget(preview_, 1);
        setCentralWidget(central);
        statusBar()->showMessage(QString("Native display: %1 × %2 pixels")
                                     .arg(nativeWidth).arg(nativeHeight));

        connect(generate, &QPushButton::clicked, this, [this, generate] {
            startGeneration(generate);
        });
        auto* generateShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
        connect(generateShortcut, &QShortcut::activated, this, [this, generate] {
            if (generate->isEnabled()) startGeneration(generate);
        });
        connect(save_, &QPushButton::clicked, this, [this] { saveImage(); });
        connect(&watcher_, &QFutureWatcher<std::vector<Pixel>>::finished,
                this, [this, generate] {
            generate->setEnabled(true);
            try {
                pixels_ = watcher_.result();
                image_ = QImage(reinterpret_cast<const uchar*>(pixels_.data()),
                                width_->value(), height_->value(),
                                width_->value() * 3, QImage::Format_RGB888).copy();
                preview_->setImage(image_);
                save_->setEnabled(true);
                statusBar()->showMessage(
                    QString("Generated %1 × %2 | %3 palette | all algorithms blended")
                        .arg(width_->value()).arg(palette_->currentText()));
            } catch (const std::exception& error) {
                QMessageBox::critical(this, "Generation failed", error.what());
                statusBar()->showMessage("Generation failed");
            }
        });
        startGeneration(generate);
    }

private:
    void startGeneration(QPushButton* generate) {
        const int width = width_->value();
        const int height = height_->value();
        const auto count = static_cast<std::uint64_t>(width) *
                           static_cast<std::uint64_t>(height);
        if (count > 300000000ULL) {
            QMessageBox::warning(this, "Image too large",
                                 "Choose dimensions with at most 300 million pixels.");
            return;
        }
        Options options;
        options.width = width;
        options.height = height;
        options.seed = random_seed();
        options.palette = static_cast<PaletteStyle>(palette_->currentData().toInt());
        options.threads = std::max(1u, std::thread::hardware_concurrency());
        generate->setEnabled(false);
        save_->setEnabled(false);
        statusBar()->showMessage(QString("Generating %1 × %2 artwork…")
                                     .arg(width).arg(height));
        watcher_.setFuture(QtConcurrent::run([options] { return render(options); }));
    }

    void saveImage() {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save artwork", "random-art.png",
            "PNG image (*.png);;JPEG image (*.jpg *.jpeg)");
        if (path.isEmpty()) return;
        const QByteArray extension = QFileInfo(path).suffix().toLower().toLatin1();
        const bool jpeg = extension == "jpg" || extension == "jpeg";
        const char* format = jpeg ? "JPEG" : "PNG";
        if (!image_.save(path, format, jpeg ? 98 : -1)) {
            QMessageBox::critical(this, "Save failed",
                                  "Could not save the image to the selected file.");
            return;
        }
        statusBar()->showMessage("Saved " + path);
    }

    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
    QComboBox* palette_ = nullptr;
    QPushButton* save_ = nullptr;
    ImagePreview* preview_ = nullptr;
    QFutureWatcher<std::vector<Pixel>> watcher_;
    std::vector<Pixel> pixels_;
    QImage image_;
};
#endif

} // namespace

#if defined(RANDOM_ART_GUI) && defined(_WIN32)
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!detect_display_size(g_nativeWidth, g_nativeHeight)) {
        MessageBoxW(nullptr, L"Could not detect the primary display resolution.",
                    L"Random Art Generator", MB_OK | MB_ICONERROR);
        return 1;
    }

    const wchar_t className[] = L"RandomArtGeneratorWindow";
    g_blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    g_pixelFont = CreateFontW(-19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = random_art_window_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = g_blackBrush;
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass)) {
        MessageBoxW(nullptr, L"Could not register the application window.",
                    L"Random Art Generator", MB_OK | MB_ICONERROR);
        DeleteObject(g_blackBrush);
        DeleteObject(g_pixelFont);
        return 1;
    }

    g_window = CreateWindowExW(0, className, L"Random Art Generator",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               920, 680, nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        MessageBoxW(nullptr, L"Could not create the application window.",
                    L"Random Art Generator", MB_OK | MB_ICONERROR);
        DeleteObject(g_blackBrush);
        DeleteObject(g_pixelFont);
        return 1;
    }
    ShowWindow(g_window, SW_MAXIMIZE);
    UpdateWindow(g_window);
    PostMessageW(g_window, WM_COMMAND,
                 MAKEWPARAM(kGenerateControl, BN_CLICKED), 0);

    ACCEL spacebar{FVIRTKEY, VK_SPACE, static_cast<WORD>(kGenerateControl)};
    HACCEL accelerators = CreateAcceleratorTableW(&spacebar, 1);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(g_window, accelerators, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (accelerators) DestroyAcceleratorTable(accelerators);
    return static_cast<int>(message.wParam);
}
#elif defined(RANDOM_ART_GUI)
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setFont(QFont("Consolas", 10, QFont::Bold));
    application.setStyleSheet(
        "QWidget { background: #000000; color: #ffffff; }"
        "QComboBox, QSpinBox { background: #000000; color: #ffffff; "
        "border: 2px solid #ffffff; padding: 4px; border-radius: 0px; }"
        "QPushButton { background: #000000; color: #ffffff; "
        "border: 2px solid #ffffff; padding: 8px; border-radius: 0px; }"
        "QPushButton:hover { background: #333333; }"
        "QPushButton:disabled { color: #888888; border-color: #888888; }"
        "QStatusBar { border-top: 2px solid #ffffff; }");
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QMessageBox::critical(nullptr, "No display detected",
                              "Could not detect a display. The desktop GUI requires a display.");
        return 1;
    }
    const QSize nativeSize = screen->size() * screen->devicePixelRatio();
    const int width = std::max(1, nativeSize.width());
    const int height = std::max(1, nativeSize.height());
    ArtWindow window(width, height);
    window.showMaximized();
    return application.exec();
}
#else
int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) return 2;
    if (options.help) { print_help(); return 0; }
    const std::string extension = options.output.size() >= 4
        ? options.output.substr(options.output.find_last_of('.') == std::string::npos
                                    ? options.output.size()
                                    : options.output.find_last_of('.'))
        : "";
    const auto image = render(options);
    bool success = false;
    if (extension == ".png" || extension == ".PNG") {
        success = save_png(options.output, image, options.width, options.height);
    } else if (extension == ".jpg" || extension == ".jpeg" ||
               extension == ".JPG" || extension == ".JPEG") {
#ifdef RANDOM_ART_HAS_JPEG
        success = save_jpeg(options.output, image, options.width, options.height);
#else
        std::cerr << "JPEG output requires libjpeg at build time; choose a .png filename.\n";
        return 2;
#endif
    } else {
        std::cerr << "Output extension must be .png, .jpg, or .jpeg.\n";
        return 2;
    }
    if (!success) {
        std::cerr << "Could not write output file: " << options.output << '\n';
        return 1;
    }
    std::cout << "Saved " << options.width << "x" << options.height << " image to "
              << options.output << " (seed " << options.seed << ", "
              << options.threads << " CPU threads)\n";
    return 0;
}
#endif
