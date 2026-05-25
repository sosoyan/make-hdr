//
//  merge.h
//  MakeHDR
//
//  Merge and tone-mapping utilities.
//

#ifndef merge_h
#define merge_h

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

#include "resources.h"
#include "solver.h"


namespace makehdr {

struct merge_params {
    // Calibration
    int   depth             = 256;
    int   solver_type       = 0;
    float smoothness        = 10.f;  // Debevec: lambda weight
    int   robertson_iters   = 10;    // Robertson: iteration count
    int   samples           = 200;
    // Merge
    bool  calibrate         = true;
    float gamma             = 1.f;
    int   channels          = 4;
    // Post-process
    float exposure          = 0.f;
    float highlights        = 1.f;
    bool  use_middle_gray   = false;
    float middle_gray       = 0.18f;
};

inline std::vector<float> build_weights(int depth)
{
    std::vector<float> w(depth);
    for (int i = 0; i < depth; ++i)
        w[i] = static_cast<float>(std::min(i, depth - 1 - i));
    return w;
}

inline std::vector<point> build_sample_points(int width, int height, int samples)
{
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const int x_pts    = std::max(1, static_cast<int>(std::sqrt(aspect * samples)));
    const int y_pts    = std::max(1, samples / x_pts);
    const int step_x   = std::max(1, width  / x_pts);
    const int step_y   = std::max(1, height / y_pts);

    std::vector<point> pts;
    pts.reserve(x_pts * y_pts);
    for (int i = 0, x = step_x / 2; i < x_pts; ++i, x += step_x)
        for (int j = 0, y = step_y / 2; j < y_pts; ++j, y += step_y)
            if (x < width && y < height)
                pts.push_back(point(x, y));
    return pts;
}

inline void build_linear_response(int depth, double* out)
{
    for (int i = 0; i < depth; ++i)
        out[i] = std::log(i * (1.f / depth));
    out[0] = out[1];
}

template<typename PixelType, typename ImageType>
std::array<bool, CMP_MAX> run_debevec_calibration(
    const std::vector<std::shared_ptr<ImageType>>& sources,
    const std::vector<float>& exp_times_log,
    const merge_params& p,
    const std::vector<float>& weights,
    const std::vector<point>& sample_points,
    double* response)
{
    std::array<bool, CMP_MAX> results = {};
    std::thread threads[CMP_MAX];

    for (int c = 0; c < CMP_MAX; ++c)
    {
        threads[c] = std::thread([&, c, response]()
        {
            results[c] = debevec_solver<PixelType, ImageType>(
                c, p.depth, p.smoothness,
                sources, sample_points,
                exp_times_log, weights,
                response + p.depth * c);
        });
    }

    for (int c = 0; c < CMP_MAX; ++c)
        threads[c].join();

    return results;
}

inline void average_robertson_curves(double* response, int depth)
{
    /// Robertson runs per-channel independently, producing divergent curve shapes
    /// on sparse linear data. Average them into one shared curve to eliminate tints,
    /// then smooth to remove kinks from sparsely-sampled bins near highlights.
    /// 3 passes of box-filter smoothing approximates a Gaussian kernel,
    /// handling broader plateau-type banding from sparsely-sampled highlight bins.
    for (int m = 0; m < depth; ++m)
    {
        double avg = 0.0;
        for (int c = 0; c < CMP_MAX; ++c)
            avg += response[depth * c + m];
        avg /= CMP_MAX;
        for (int c = 0; c < CMP_MAX; ++c)
            response[depth * c + m] = avg;
    }

    const int radius = 4;
    const int passes = 3;
    std::vector<double> smoothed(depth);
    for (int pass = 0; pass < passes; ++pass)
    {
        const double* curve = response;
        for (int m = 0; m < depth; ++m)
        {
            double sum = 0.0;
            int count  = 0;
            for (int k = std::max(0, m - radius); k <= std::min(depth - 1, m + radius); ++k)
            {
                sum += curve[k];
                ++count;
            }
            smoothed[m] = sum / count;
        }
        for (int m = 0; m < depth; ++m)
            for (int c = 0; c < CMP_MAX; ++c)
                response[depth * c + m] = smoothed[m];
    }
}

template<typename PixelType, typename ImageType>
std::array<bool, CMP_MAX> run_robertson_calibration(
    const std::vector<std::shared_ptr<ImageType>>& sources,
    const std::vector<float>& exp_times,
    const merge_params& p,
    const std::vector<float>& weights,
    const std::vector<point>& sample_points,
    double* response)
{
    std::array<bool, CMP_MAX> results = {};
    std::thread threads[CMP_MAX];

    for (int c = 0; c < CMP_MAX; ++c)
    {
        threads[c] = std::thread([&, c, response]()
        {
            results[c] = robertson_solver<PixelType, ImageType>(
                c, p.depth, p.robertson_iters,
                sources, sample_points,
                exp_times, weights,
                response + p.depth * c);
        });
    }

    for (int c = 0; c < CMP_MAX; ++c)
        threads[c].join();

    average_robertson_curves(response, p.depth);

    return results;
}

inline float luminance(const float* rgb)
{
    return 0.212671f * rgb[0] + 0.71516f * rgb[1] + 0.072169f * rgb[2];
}

/// Bundles all inputs needed to merge a single pixel — source images, exposure times,
/// weight table, response curves and parameters. Constructed once per region in
/// merge_hdr_region and forwarded by const-ref to every merge_pixel call.
template<typename SrcImageType>
struct merge_context {
    const std::vector<std::shared_ptr<SrcImageType>>& sources;
    const std::vector<float>& exp_times_log;
    const std::vector<float>& weights;
    const double* response;
    const double* response_linear;
    const merge_params& p;
};

/// Accumulates all source contributions for a single pixel and writes the HDR result.
/// Returns false if a null source is encountered, signalling the caller to halt region processing.
template<typename PixelType, typename SrcImageType>
bool merge_pixel(int x, int y, PixelType* dp, const merge_context<SrcImageType>& ctx)
{
    float weight_sum            = 0.f;
    float response_log[CMP_MAX] = {};
    float result[CMP_MAX]       = {};
    float fallback_log[CMP_MAX] = {};
    float min_exp_log           = FLT_MAX;

    for (int i = 0; i < static_cast<int>(ctx.sources.size()); ++i)
    {
        const PixelType* src = static_cast<const PixelType*>(ctx.sources[i]->getPixelAddress(x, y));
        if (!src) return false;

        float weight_src = 0.f;
        const float exp_log = ctx.exp_times_log[i];
        for (int c = 0; c < CMP_MAX; ++c)
        {
            const float s = std::min(std::max(static_cast<float>(src[c]), 0.f), 1.f);
            const int bin = static_cast<int>(s * (ctx.p.depth - 1));
            weight_src += ctx.weights[bin];
            response_log[c] = ctx.p.calibrate
                ? static_cast<float>(ctx.response[ctx.p.depth * c + bin])
                : static_cast<float>(ctx.response_linear[bin]);
        }
        weight_src /= CMP_MAX;

        /// Track the darkest source as fallback for fully-clipped pixels.
        /// Use raw unclamped value when > 1.0 (genuine HDR in linear float),
        /// otherwise use the response curve at bin 255 (clipped at camera max).
        if (exp_log < min_exp_log)
        {
            min_exp_log = exp_log;
            for (int c = 0; c < CMP_MAX; ++c)
            {
                const float raw = static_cast<float>(src[c]);
                fallback_log[c] = raw > 1.f
                    ? std::log(raw) - exp_log
                    : response_log[c] - exp_log;
            }
        }

        for (int c = 0; c < CMP_MAX; ++c)
            result[c] += weight_src * (response_log[c] - exp_log);

        weight_sum += weight_src;
    }

    for (int c = 0; c < CMP_MAX; ++c)
    {
        const float log_hdr = weight_sum > 0.f
            ? result[c] / weight_sum
            : fallback_log[c];
        dp[c] = static_cast<PixelType>(std::pow(std::exp(log_hdr), 1.f / ctx.p.gamma));
    }

    if (ctx.p.channels > 3)
        dp[static_cast<int>(channel::a)] = 1.0f;

    return true;
}

/// Merges all source images into dst over the rectangle [x1,y1)–[x2,y2).
/// Checks abort_fn once per scanline and returns early if it fires.
template<typename PixelType, typename SrcImageType, typename DstImageType, typename AbortFn>
void merge_hdr_region(
    const std::vector<std::shared_ptr<SrcImageType>>& sources,
    const std::vector<float>& exp_times_log,
    const std::vector<float>& weights,
    const double* response,
    const double* response_linear,
    DstImageType* dst,
    int x1, int y1, int x2, int y2,
    const merge_params& p,
    AbortFn&& abort_fn)
{
    const merge_context<SrcImageType> ctx { sources, exp_times_log, weights, response, response_linear, p };

    for (int y = y1; y < y2; ++y)
    {
        if (abort_fn()) return;

        for (int x = x1; x < x2; ++x)
        {
            PixelType* dp = static_cast<PixelType*>(dst->getPixelAddress(x, y));
            if (!merge_pixel<PixelType>(x, y, dp, ctx))
                return;
        }
    }
}

template<typename PixelType>
void post_process_buffer(PixelType* dst, int buf_size, const merge_params& p)
{
    float  lum_max  = 0.f;
    double log_sum  = 0.0;
    int    px_count = 0;

    /// Pass 1: scene maximum (always needed for Reinhard) and, when middle gray is enabled,
    /// log-average of linear luminance for normalisation.
    ///   L_avg = exp(mean(log(ε + L_linear_i))) [Reinhard 2002, eq. 1]
    ///
    /// dst currently holds hdr^(1/gamma) (gamma-encoded, no exposure).
    /// For the geometric mean, linear luminance is recovered per channel before weighting:
    ///   lum_linear = 0.212671*R^gamma + 0.71516*G^gamma + 0.072169*B^gamma
    for (int i = 0; i < buf_size; i += p.channels)
    {
        const float px[3] = {
            static_cast<float>(dst[i + 0]),
            static_cast<float>(dst[i + 1]),
            static_cast<float>(dst[i + 2])
        };

        const float lum = luminance(px);
        lum_max = std::max(lum_max, lum);

        if (p.use_middle_gray)
        {
            const float r_lin = std::pow(std::max(0.f, px[0]), p.gamma);
            const float g_lin = std::pow(std::max(0.f, px[1]), p.gamma);
            const float b_lin = std::pow(std::max(0.f, px[2]), p.gamma);
            const float l_lin = 0.212671f * r_lin + 0.71516f * g_lin + 0.072169f * b_lin;
            if (l_lin > 0.f)
            {
                log_sum += std::log(1e-6f + l_lin);
                ++px_count;
            }
        }
    }

    /// Pre-scaling: exposure only, or combined middle-gray normalisation + exposure.
    ///
    /// When middle gray is OFF (backwards-compatible mode):
    ///   pixel_scale = pow(2^exposure, 1/gamma)
    ///   result = pow(hdr * 2^exposure, 1/gamma)  -- original formula.
    ///
    /// When middle gray is enabled:
    ///   pixel_scale = pow(middle_gray * 2^exposure / lum_linear_avg, 1/gamma)
    ///   result = pow(hdr * middle_gray / lum_linear_avg * 2^exposure, 1/gamma)
    float pixel_scale;
    if (p.use_middle_gray && p.middle_gray > 0.f)
    {
        const float l_avg = px_count > 0 ? std::exp(static_cast<float>(log_sum / px_count)) : 1.f;
        pixel_scale = l_avg > 0.f
            ? std::pow(p.middle_gray * std::pow(2.f, p.exposure) / l_avg, 1.f / p.gamma)
            : 1.f;
    }
    else
    {
        pixel_scale = std::pow(std::pow(2.f, p.exposure), 1.f / p.gamma);
    }

    const float scaled_max  = lum_max * pixel_scale;
    const float log_lum_max = std::log10(1.f + scaled_max);

    /// Pass 2: Reinhard global tone mapping.
    /// highlights blends between fully tone-mapped (0) and linear (1).
    ///   L_d = log10(1 + L_scaled) / log10(1 + L_max_scaled)  [display luminance]
    ///   C_d = L_d * C / L  [per-channel, preserves hue]
    for (int i = 0; i < buf_size; i += p.channels)
    {
        float px[3] = {
            static_cast<float>(dst[i + 0]) * pixel_scale,
            static_cast<float>(dst[i + 1]) * pixel_scale,
            static_cast<float>(dst[i + 2]) * pixel_scale
        };

        const float lum = luminance(px);
        if (lum == 0.f || scaled_max == 0.f)
        {
            dst[i + 0] = static_cast<PixelType>(px[0]);
            dst[i + 1] = static_cast<PixelType>(px[1]);
            dst[i + 2] = static_cast<PixelType>(px[2]);
            continue;
        }

        const float lum_dif = std::log10(1.f + lum) / log_lum_max;
        for (int c = 0; c < CMP_MAX; ++c)
        {
            const float tone = lum_dif * px[c] / lum;
            px[c] = tone + (px[c] - tone) * p.highlights;
        }

        dst[i + 0] = static_cast<PixelType>(px[0]);
        dst[i + 1] = static_cast<PixelType>(px[1]);
        dst[i + 2] = static_cast<PixelType>(px[2]);
    }
}

} // namespace makehdr

#endif  // merge_h
