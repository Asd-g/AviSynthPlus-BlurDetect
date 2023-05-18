#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include "avisynth_c.h"


enum RoundedDirection
{
    DIRECTION_45UP,
    DIRECTION_45DOWN,
    DIRECTION_HORIZONTAL,
    DIRECTION_VERTICAL,
};

struct blurdetect
{
    int low;
    int high;
    int radius;
    int block_pct;
    int block_width;
    int block_height;
    std::array<bool, 4> process;
    int scale_coef;
    int scale_coef1;
    int scale_coef2;
    uint32_t peak;

    void(*gauss_process)(void* __restrict dstp_, const void* srcp_, const ptrdiff_t src_pitch, const ptrdiff_t width, const int height) noexcept;
    void(*sobel_process)(uint32_t* __restrict dst, void* __restrict dir_, const void* srcp_, const blurdetect& d, const ptrdiff_t width, const int height) noexcept;
};


// Internal helper for ff_sobel()
template <typename T>
static const int get_rounded_direction(int gx, int gy, const blurdetect& d)
{
    /* reference angles:
     *   tan( pi/8) = sqrt(2)-1
     *   tan(3pi/8) = sqrt(2)+1
     * Gy/Gx is the tangent of the angle (theta), so Gy/Gx is compared against
     * <ref-angle>, or more simply Gy against <ref-angle>*Gx
     *
     * Gx and Gy bounds = [-1020;1020], using 16-bit arithmetic:
     *   round((sqrt(2)-1) * (1<<16)) =  27146
     *   round((sqrt(2)+1) * (1<<16)) = 158218
     */

    if (gx)
    {
        if (gx < 0)
            gx = -gx, gy = -gy;
        gy *= d.scale_coef;

        const int tanpi8gx{ d.scale_coef1 * gx };
        const int tan3pi8gx{ d.scale_coef2 * gx };

        if (gy > -tan3pi8gx && gy < -tanpi8gx)
            return DIRECTION_45UP;
        if (gy > -tanpi8gx && gy < tanpi8gx)
            return DIRECTION_HORIZONTAL;
        if (gy > tanpi8gx && gy < tan3pi8gx)
            return DIRECTION_45DOWN;
    }

    return DIRECTION_VERTICAL;
}

template <typename T>
static void sobel(uint32_t* __restrict dst, void* __restrict dir_, const void* srcp_, const blurdetect& d, const ptrdiff_t width, const int height) noexcept
{
    const T* srcp{ reinterpret_cast<const T*>(srcp_) };
    T* dir{ reinterpret_cast<T*>(dir_) };

    for (int j{ 1 }; j < height - 1; ++j)
    {
        dst += width;
        dir += width;
        srcp += width;

        for (int i{ 1 }; i < width - 1; ++i)
        {
            const int gx{
                -1 * srcp[-width + (i - 1)] + 1 * srcp[-width + (i + 1)]
                - 2 * srcp[(i - 1)] + 2 * srcp[(i + 1)]
                - 1 * srcp[width + (i - 1)] + 1 * srcp[width + (i + 1)] };
            const int gy{
                -1 * srcp[-width + (i - 1)] + 1 * srcp[width + (i - 1)]
                - 2 * srcp[-width + (i)] + 2 * srcp[width + (i)]
                - 1 * srcp[-width + (i + 1)] + 1 * srcp[width + (i + 1)] };

            dst[i] = std::abs(gx) + std::abs(gy);
            dir[i] = get_rounded_direction<T>(gx, gy, d);
        }
    }
}

template <typename T>
static void gaussian_blur(void* __restrict dstp_, const void* srcp_, const ptrdiff_t src_pitch, const ptrdiff_t width, const int height) noexcept
{
    const T* srcp{ reinterpret_cast<const T*>(srcp_) };
    T* dstp{ reinterpret_cast<T*>(dstp_) };

    memcpy(dstp, srcp, width * sizeof(T));
    dstp += width;
    srcp += src_pitch;

    memcpy(dstp, srcp, width * sizeof(T));
    dstp += width;
    srcp += src_pitch;

    for (int j{ 2 }; j < height - 2; ++j)
    {
        dstp[0] = srcp[0];
        dstp[1] = srcp[1];

        for (int i{ 2 }; i < width - 2; ++i)
        {
            /* Gaussian mask of size 5x5 with sigma = 1.4 */
            dstp[i] = ((srcp[-2 * src_pitch + (i - 2)] + srcp[2 * src_pitch + (i - 2)]) * 2
                + (srcp[-2 * src_pitch + (i - 1)] + srcp[2 * src_pitch + (i - 1)]) * 4
                + (srcp[-2 * src_pitch + (i)] + srcp[2 * src_pitch + (i)]) * 5
                + (srcp[-2 * src_pitch + (i + 1)] + srcp[2 * src_pitch + (i + 1)]) * 4
                + (srcp[-2 * src_pitch + (i + 2)] + srcp[2 * src_pitch + (i + 2)]) * 2

                + (srcp[-src_pitch + (i - 2)] + srcp[src_pitch + (i - 2)]) * 4
                + (srcp[-src_pitch + (i - 1)] + srcp[src_pitch + (i - 1)]) * 9
                + (srcp[-src_pitch + (i)] + srcp[src_pitch + (i)]) * 12
                + (srcp[-src_pitch + (i + 1)] + srcp[src_pitch + (i + 1)]) * 9
                + (srcp[-src_pitch + (i + 2)] + srcp[src_pitch + (i + 2)]) * 4

                + srcp[(i - 2)] * 5
                + srcp[(i - 1)] * 12
                + srcp[(i)] * 15
                + srcp[(i + 1)] * 12
                + srcp[(i + 2)] * 5) / 159;
        }

        dstp[width - 2] = srcp[(width - 2)];
        dstp[width - 1] = srcp[(width - 1)];

        dstp += width;
        srcp += src_pitch;
    }

    memcpy(dstp, srcp, width * sizeof(T));
    dstp += width;
    srcp += src_pitch;

    memcpy(dstp, srcp, width * sizeof(T));
}

// Filters rounded gradients to drop all non-maxima
// Expects gradients generated by ff_sobel()
// Expects zero's destination buffer
template <typename T>
static void non_maximum_suppression(T* __restrict dst, const T* dir, const uint32_t* src, const ptrdiff_t width, const int height, const uint32_t peak) noexcept
{
    auto COPY_MAXIMA{ [&](const int ay, const int ax, const int by, const int bx, const int i)
        {
            do
            {
                if (src[i] > src[(ay)*width + i + (ax)] &&
                    src[i] > src[(by)*width + i + (bx)])
                    dst[i] = static_cast<T>(std::min(src[i], peak));
            } while (false);
        }
    };

    for (int j{ 1 }; j < height - 1; ++j)
    {
        dst += width;
        dir += width;
        src += width;

        for (int i{ 1 }; i < width - 1; ++i)
        {
            switch (dir[i])
            {
                case DIRECTION_45UP: COPY_MAXIMA(1, -1, -1, 1, i); break;
                case DIRECTION_45DOWN: COPY_MAXIMA(-1, -1, 1, 1, i); break;
                case DIRECTION_HORIZONTAL: COPY_MAXIMA(0, -1, 0, 1, i); break;
                case DIRECTION_VERTICAL: COPY_MAXIMA(-1, 0, 1, 0, i); break;
            }
        }
    }
}

// Filter to keep all pixels > high, and keep all pixels > low where all surrounding pixels > high
template <typename T>
static void double_threshold(T* __restrict dst, const T* src, const blurdetect& d, const ptrdiff_t width, const int height) noexcept
{
    for (int j{ 0 }; j < height; ++j)
    {
        for (int i{ 0 }; i < width; ++i)
        {
            if (src[i] > d.high)
            {
                dst[i] = src[i];
                continue;
            }

            if (!(!i || i == width - 1 || !j || j == height - 1) &&
                src[i] > d.low &&
                (src[-width + i - 1] > d.high ||
                    src[-width + i] > d.high ||
                    src[-width + i + 1] > d.high ||
                    src[i - 1] > d.high ||
                    src[i + 1] > d.high ||
                    src[width + i - 1] > d.high ||
                    src[width + i] > d.high ||
                    src[width + i + 1] > d.high))
                dst[i] = src[i];
            else
                dst[i] = 0;
        }

        dst += width;
        src += width;
    }
}

// edge width is defined as the distance between surrounding maxima of the edge pixel
template <typename T>
static const float edge_width(const T* src, const int i, const int j, const T dir, const ptrdiff_t w, const int h, const int radius) noexcept
{
    float width{ 0.0f };
    int dX;
    int dY;
    int k;

    switch (dir)
    {
        case DIRECTION_HORIZONTAL: dX = 1; dY = 0; break;
        case DIRECTION_VERTICAL: dX = 0; dY = 1; break;
        case DIRECTION_45UP: dX = 1; dY = -1; break;
        case DIRECTION_45DOWN: dX = 1; dY = 1; break;
        default: dX = 1; dY = 1; break;
    }

    // determines if search in direction dX/dY is looking for a maximum or minimum
    const int sign{ src[j * w + i] > src[(j - dY) * w + i - dX] ? 1 : -1 };

    // search in -(dX/dY) direction
    for (k = 0; k < radius; ++k)
    {
        int x{ i - k * dX };
        int y{ j - k * dY };
        const ptrdiff_t p1{ y * w + x };
        x -= dX;
        y -= dY;
        const ptrdiff_t p2{ y * w + x };
        if (x < 0 || x >= w || y < 0 || y >= h)
            return 0;

        const int tmp{ (src[p1] - src[p2]) * sign };

        if (tmp <= 0) // local maximum found
            break;
    }
    width += k;

    // search in +(dX/dY) direction
    for (k = 0; k < radius; ++k)
    {
        int x{ i + k * dX };
        int y{ j + k * dY };
        const ptrdiff_t p1{ y * w + x };
        x += dX;
        y += dY;
        const ptrdiff_t p2{ y * w + x };
        if (x < 0 || x >= w || y < 0 || y >= h)
            return 0;

        const int tmp{ (src[p1] - src[p2]) * sign };

        if (tmp >= 0) // local maximum found
            break;
    }
    width += k;

    // for 45 degree directions approximate edge width in pixel units: 0.7 ~= sqrt(2)/2
    if (dir == DIRECTION_45UP || dir == DIRECTION_45DOWN)
        width *= 0.7f;

    return width;
}

template <typename T>
static float calculate_blur(T* dir, T* dst, T* src, const blurdetect& d, const ptrdiff_t width, const int height) noexcept
{
    int blkcnt{ 0 };

    const ptrdiff_t block_width{ (d.block_width == -1) ? width : d.block_width };
    const int block_height{ (d.block_height == -1) ? height : d.block_height };
    const int brows{ height / block_height };
    const ptrdiff_t bcols{ width / block_width };

    std::unique_ptr<float[]> blks{ std::make_unique<float[]>((width / block_width) * (height / block_height)) };

    for (int blkj{ 0 }; blkj < brows; ++blkj)
    {
        for (int blki{ 0 }; blki < bcols; ++blki)
        {
            double block_total_width{ 0.0 };
            int block_count{ 0 };

            for (int inj{ 0 }; inj < block_height; ++inj)
            {
                for (int ini{ 0 }; ini < block_width; ++ini)
                {
                    const ptrdiff_t i{ blki * block_width + ini };
                    const int j{ blkj * block_height + inj };

                    if (dst[j * width + i] > 0)
                    {
                        const float width_{ edge_width(src, i, j, dir[j * width + i], width, height, d.radius) };

                        if (width_ > 0.001f) // throw away zeros
                        {
                            block_count++;
                            block_total_width += width_;
                        }
                    }
                }
            }
            // if not enough edge pixels in a block, consider it smooth
            if (block_total_width >= 2 && block_count)
            {
                blks[blkcnt] = block_total_width / block_count;
                ++blkcnt;
            }
        }
    }

    // simple block pooling by sorting and keeping the sharper blocks
    std::sort(blks.get(), blks.get() + blkcnt);
    blkcnt = static_cast<int>(::ceilf(blkcnt * (d.block_pct / 100.0f)));

    float total_width{ 0.0f };

    for (int i{ 0 }; i < blkcnt; ++i)
        total_width += blks[i];

    return (blkcnt == 0) ? 0.0f : (total_width / blkcnt);
}

template <typename T>
static AVS_VideoFrame* AVSC_CC get_frame_blurdetect(AVS_FilterInfo* fi, int n)
{
    blurdetect* d{ reinterpret_cast<blurdetect*>(fi->user_data) };

    AVS_VideoFrame* frame{ avs_get_frame(fi->child, n) };
    if (!frame)
        return nullptr;

    avs_make_property_writable(fi->env, &frame);
    AVS_Map* props{ avs_get_frame_props_rw(fi->env, frame) };

    const std::array<std::string, 4> blurriness_y{ "blurriness_y", "blurriness_u", "blurriness_v", "blurriness_a" };
    const std::array<std::string, 4> blurriness_r{ "blurriness_r", "blurriness_g", "blurriness_b", "blurriness_a" };
    constexpr std::array<int, 4> planes_y{ AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V, AVS_PLANAR_A };
    constexpr std::array<int, 4> planes_r{ AVS_PLANAR_R, AVS_PLANAR_G, AVS_PLANAR_B, AVS_PLANAR_A };

    const std::array<std::string, 4>* block;
    const std::array<int, 4>* planes;

    if (avs_is_rgb(&fi->vi))
    {
        block = &blurriness_r;
        planes = &planes_r;
    }
    else
    {
        block = &blurriness_y;
        planes = &planes_y;
    }

    for (int i{ 0 }; i < avs_num_components(&fi->vi); ++i)
    {
        if (d->process[i])
        {
            const size_t width{ avs_get_row_size_p(frame, planes->at(i)) / sizeof(T) };
            const int height{ avs_get_height_p(frame, planes->at(i)) };

            const size_t bufsize{ width * height };
            std::unique_ptr<T[]> filterbuf{ std::make_unique<T[]>(bufsize) };
            std::unique_ptr<uint32_t[]> gradients{ std::make_unique<uint32_t[]>(bufsize) };
            std::unique_ptr<T[]> directions{ std::make_unique<T[]>(bufsize) };
            std::unique_ptr<T[]> tmpbuf{ std::make_unique<T[]>(bufsize) };

            // gaussian filter to reduce noise
            gaussian_blur<T>(filterbuf.get(), reinterpret_cast<const T*>(avs_get_read_ptr_p(frame, planes->at(i))), avs_get_pitch_p(frame, planes->at(i)) / sizeof(T), width, height);

            // compute the 16-bits gradients and directions for the next step
            sobel<T>(gradients.get(), directions.get(), filterbuf.get(), *d, width, height);

            // non_maximum_suppression() will actually keep & clip what's necessary and
            // ignore the rest, so we need a clean output buffer
            non_maximum_suppression<T>(tmpbuf.get(), directions.get(), gradients.get(), width, height, d->peak);

            // keep high values, or low values surrounded by high values
            double_threshold<T>(tmpbuf.get(), tmpbuf.get(), *d, width, height);

            avs_prop_set_float(fi->env, props, block->at(i).c_str(), calculate_blur<T>(directions.get(), tmpbuf.get(), filterbuf.get(), *d, width, height), 0);
        }
    }

    return frame;
}

static void AVSC_CC free_blurdetect(AVS_FilterInfo* fi)
{
    blurdetect* d{ reinterpret_cast<blurdetect*>(fi->user_data) };
    delete d;
}

static int AVSC_CC set_cache_hints_blurdetect(AVS_FilterInfo* fi, int cachehints, int frame_range)
{
    return cachehints == AVS_CACHE_GET_MTMODE ? 1 : 0;
}

static AVS_Value AVSC_CC Create_blurdetect(AVS_ScriptEnvironment* env, AVS_Value args, void* param)
{
    enum { Clip, Low, High, Radius, Block_pct, Block_width, Block_height, Planes };

    blurdetect* d{ new blurdetect() };

    AVS_FilterInfo* fi;
    AVS_Clip* clip{ avs_new_c_filter(env, &fi, avs_array_elt(args, Clip), 1) };

    const auto set_error{ [&](const char* error)
        {
            avs_release_clip(clip);

            return avs_new_value_error(error);
        }
    };

    if (!avs_check_version(env, 9))
    {
        if (avs_check_version(env, 10))
        {
            if (avs_get_env_property(env, AVS_AEP_INTERFACE_BUGFIX) < 2)
                return set_error("BlurDetect: AviSynth+ version must be r3688 or later.");
        }
    }
    else
        return set_error("BlurDetect: AviSynth+ version must be r3688 or later.");

    if (!avs_is_planar(&fi->vi))
        return set_error("BlurDetect: clip must be in planar format.");
    if (avs_component_size(&fi->vi) == 4)
        return set_error("BlurDetect: clip must be 8..16-bit.");

    const float low{ avs_defined(avs_array_elt(args, Low)) ? static_cast<float>(avs_as_float(avs_array_elt(args, Low))) : 0.05882353f };
    const float high{ avs_defined(avs_array_elt(args, High)) ? static_cast<float>(avs_as_float(avs_array_elt(args, High))) : 0.11764706f };
    d->radius = avs_defined(avs_array_elt(args, Radius)) ? avs_as_int(avs_array_elt(args, Radius)) : 50;
    d->block_pct = avs_defined(avs_array_elt(args, Block_pct)) ? avs_as_int(avs_array_elt(args, Block_pct)) : 80;
    d->block_width = avs_defined(avs_array_elt(args, Block_width)) ? avs_as_int(avs_array_elt(args, Block_width)) : -1;
    d->block_height = avs_defined(avs_array_elt(args, Block_height)) ? avs_as_int(avs_array_elt(args, Block_height)) : -1;

    if (low < 0.0f || low > 1.0f)
        return set_error("BlurDetect: low must be between 0.0..1.0.");
    if (high < 0.0f || high > 1.0f)
        return set_error("BlurDetect: high must be between 0.0..1.0.");
    if (low > high)
        return set_error("BlurDetect: low must be less than or equal to high.");
    if (d->radius < 1 || d->radius > 100)
        return set_error("BlurDetect: radius must be between 1..100.");
    if (d->block_pct < 1 || d->block_pct > 100)
        return set_error("BlurDetect: block_pct must be between 1..100.");
    if (d->block_width < -1)
        return set_error("BlurDetect: block_width must be equal to or greater than -1.");
    if (d->block_height < -1)
        return set_error("BlurDetect: block_height must be equal to or greater than -1.");

    const int num_planes{ (avs_defined(avs_array_elt(args, Planes))) ? avs_array_size(avs_array_elt(args, Planes)) : 0 };

    for (int i{ 0 }; i < 4; ++i)
        d->process[i] = (num_planes <= 0);

    for (int i{ 0 }; i < num_planes; ++i)
    {
        const int n{ avs_as_int(*(avs_as_array(avs_array_elt(args, Planes)) + i)) };

        if (n >= avs_num_components(&fi->vi))
            return set_error("BlurDetect: plane index out of range");

        if (d->process[n])
            return set_error("BlurDetect: plane specified twice");

        d->process[n] = true;
    }

    switch (avs_bits_per_component(&fi->vi))
    {
        case 8:
        {
            d->low = static_cast<int>(low * 255 + 0.5f);
            d->high = static_cast<int>(high * 255 + 0.5f);

            d->scale_coef = 1 << 16; // 1 << (16 - (bits_per_component - 8))
            d->scale_coef1 = 27146; //round((sqrt(2) - 1)* (scale_coef))
            d->scale_coef2 = 158218; //round((sqrt(2) + 1) * (scale_coef))
            d->peak = 255;
            break;
        }
        case 10:
        {
            d->low = static_cast<int>(low * 1023 + 0.5f);
            d->high = static_cast<int>(high * 1023 + 0.5f);

            d->scale_coef = 1 << 14;
            d->scale_coef1 = 6786;
            d->scale_coef2 = 39554;
            d->peak = 1023;
            break;
        }
        case 12:
        {
            d->low = static_cast<int>(low * 4095 + 0.5f);
            d->high = static_cast<int>(high * 4095 + 0.5f);

            d->scale_coef = 1 << 12;
            d->scale_coef1 = 1697;
            d->scale_coef2 = 9887;
            d->peak = 4095;
            break;
        }
        case 14:
        {
            d->low = static_cast<int>(low * 16383 + 0.5f);
            d->high = static_cast<int>(high * 16383 + 0.5f);

            d->scale_coef = 1 << 10;
            d->scale_coef1 = 424;
            d->scale_coef2 = 2472;
            d->peak = 16383;
            break;
        }
        default:
        {
            d->low = static_cast<int>(low * 65535 + 0.5f);
            d->high = static_cast<int>(high * 65535 + 0.5f);

            d->scale_coef = 1 << 8;
            d->scale_coef1 = 106;
            d->scale_coef2 = 618;
            d->peak = 65535;
        }
    }

    AVS_Value v{ avs_new_value_clip(clip) };

    fi->user_data = reinterpret_cast<void*>(d);
    fi->get_frame = (avs_component_size(&fi->vi) == 1) ? get_frame_blurdetect<uint8_t> : get_frame_blurdetect<uint16_t>;
    fi->set_cache_hints = set_cache_hints_blurdetect;
    fi->free_filter = free_blurdetect;

    avs_release_clip(clip);

    return v;
}

const char* AVSC_CC avisynth_c_plugin_init(AVS_ScriptEnvironment* env)
{
    avs_add_function(env, "BlurDetect", "c[low]f[high]f[radius]i[block_pct]i[block_width]i[block_height]i[planes]i*", Create_blurdetect, 0);
    return "BurDetect";
}
