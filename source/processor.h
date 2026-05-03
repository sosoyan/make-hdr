//
//  processor.h
//  MakeHDR
//
//  Created by Vahan Sosoyan 2024.
//

#ifndef processor_h
#define processor_h

#include "resources.h"
#include "solver.h"


template <class ptype>
class Effect;

template <class ptype>
class Processor : public OFX::ImageProcessor
{
public:
    Processor(Effect<ptype>& effect,
              const unsigned int components) : OFX::ImageProcessor(effect),
                                               _effect(effect),
                                               _components(components)
    {
    }

    ~Processor()
    {
    }

    virtual void preProcess()
    {
        if (_sources.empty())
        {
            spdlog::debug("[{}] sources are empty!", fx::label);
            return;
        }
        if (_effect.abort())
        {
            spdlog::debug("[{}] effect calibrate abort!", fx::label);
            return;
        }
        if (!_effect.regen_calib() && !_effect.input_weights().empty())
        {
            spdlog::debug("[{}] calibrate skipped!", fx::label);
            return;
        }

        _effect.set_input_weights(_input_depth);
        _calibrate ? calibrate() : calibrate_linear();
    }

    virtual void multiThreadProcessImages(OfxRectI proc_window)
    {
        if (_sources.empty()) return;


        for (int y = proc_window.y1; y < proc_window.y2; ++y)
        {
            if (_effect.abort()) return;

            for (int x = proc_window.x1; x < proc_window.x2; ++x)
            {
                float weight_sum = 0.f;
                float response_log[3] = { 0.f, 0.f, 0.f };
                float result[3] = { 0.f, 0.f, 0.f };
                float fallback_log[3] = { 0.f, 0.f, 0.f };
                float min_exp_log = FLT_MAX;

                ptype* dst = (ptype*)_dstImg->getPixelAddress(x, y);

                for (int i = 0; i < _sources.size(); ++i)
                {
                    const ptype* src = (ptype*)_sources[i]->getPixelAddress(x, y);

                    if (src == nullptr) return;

                    float weight_src = 0.f;

                    for (int c = 0; c < CMP_MAX; ++c)
                    {
                        const ptype sample = std::min<ptype>(std::max<ptype>(src[c], 0.f), 1.f);
                        const int bin = (int)(sample * (_input_depth - 1));
                        weight_src += _effect.input_weights()[bin];
                        response_log[c] = lookup_response(bin, c);
                    }

                    weight_src /= CMP_MAX;

                    /// Track the darkest source as fallback for fully-clipped pixels.
                    /// Use raw unclamped value when > 1.0 (genuine HDR in linear float),
                    /// otherwise use the response curve at bin 255 (clipped at camera max).
                    if (_exp_times_log[i] < min_exp_log)
                    {
                        min_exp_log = _exp_times_log[i];
                        for (int c = 0; c < CMP_MAX; ++c)
                        {
                            const float raw = (float)src[c];
                            fallback_log[c] = raw > 1.0f
                                ? std::log(raw) - _exp_times_log[i]
                                : response_log[c] - _exp_times_log[i];
                        }
                    }

                    for (int c = 0; c < CMP_MAX; ++c)
                        result[c] += weight_src * (response_log[c] - _exp_times_log[i]);

                    weight_sum += weight_src;
                }

                for (int c = 0; c < CMP_MAX; ++c)
                {
                    const float log_hdr = weight_sum > 0.f
                        ? result[c] / weight_sum
                        : fallback_log[c];
                    const float hdr = std::exp(log_hdr);
                    dst[c] = (ptype)pow(hdr, 1.f / _gamma);
                }

                if(_show_samples && _effect.sample_set().count(fx::point(x, y).key()))
                    dst[fx::ch::g] = FLT_MAX;

                dst[fx::ch::a] = 1.0f;
            }
        }
    }

    virtual void postProcess() 
    {
        if (_sources.empty()) return;

        ptype* dst = (ptype*)_dstImg->getPixelData();

        /// Pass 1: scene maximum (always needed for Reinhard) and, when middle gray is enabled, 
        /// log-average of linear luminance for normalisation.
        ///   L_avg = exp(mean(log(ε + L_linear_i))) [Reinhard 2002, eq. 1]
        ///
        /// dst currently holds hdr^(1/gamma) (gamma-encoded, no exposure).
        /// For the geometric mean, linear luminance is recovered per channel before weighting:
        ///   lum_linear = 0.212671*R^gamma + 0.71516*G^gamma + 0.072169*B^gamma
        double log_sum = 0.0;
        int pixel_count = 0;
        for (int i = 0; i < pixel_size(); i += _components)
        {
            const float lum = luminance(dst + i);
            _luminance_max = std::max(lum, _luminance_max);

            if (_use_middle_gray)
            {
                const float r_lin = std::pow(std::max(0.f, dst[i + fx::ch::r]), _gamma);
                const float g_lin = std::pow(std::max(0.f, dst[i + fx::ch::g]), _gamma);
                const float b_lin = std::pow(std::max(0.f, dst[i + fx::ch::b]), _gamma);
                const float lum_lin = 0.212671f * r_lin + 0.71516f * g_lin + 0.072169f * b_lin;
                if (lum_lin > 0.f)
                {
                    log_sum += std::log(1e-6f + lum_lin);
                    ++pixel_count;
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
        if (_use_middle_gray && _middle_gray > 0.f)
        {
            const float lum_linear_avg = pixel_count > 0 ? std::exp((float)(log_sum / pixel_count)) : 1.f;
            pixel_scale = lum_linear_avg > 0.f
                ? std::pow(_middle_gray * std::pow(2.f, _exposure) / lum_linear_avg, 1.f / _gamma)
                : 1.f;
        }
        else
        {
            pixel_scale = std::pow(std::pow(2.f, _exposure), 1.f / _gamma);
        }
        const float scaled_lum_max = _luminance_max * pixel_scale;
        const float log_lum_max = std::log10(1.f + scaled_lum_max);

        /// Pass 2: Reinhard global tone mapping
        /// highlights blends between fully tone-mapped (0) and linear (1)
        ///   L_d = log10(1 + L_scaled) / log10(1 + L_max_scaled)  [display luminance]
        ///   C_d = L_d * C / L  [per-channel, preserves hue]
        for (int i = 0; i < pixel_size(); i += _components)
        {
            for (int c = 0; c < CMP_MAX; ++c)
                dst[i + c] *= pixel_scale;

            const float lum = luminance(dst + i);
            if (lum == 0.f || scaled_lum_max == 0.f)
                continue;

            const float lum_dif = std::log10(1.f + lum) / log_lum_max;

            for (int c = 0; c < CMP_MAX; ++c)
            {
                const float tone = lum_dif * dst[i + c] / lum;
                dst[i + c] = tone + (dst[i + c] - tone) * _highlights;
            }
        }

        if(!_effect.abort() && !_sources.empty())
            spdlog::info("[{}] {} sources merged in {}ms", fx::label, _sources.size(), _timer.get());
    }

    void set_parameters(const double& time)
    {
        _exposure = _effect.exposure(time);
        _gamma = _effect.gamma(time);
        _highlights = _effect.highlights(time);
        _calibrate = _effect.calibrate(time);
        _show_samples = _effect.show_samples(time);
        _samples = _effect.samples(time);
        _solver_type = _effect.solver_type(time);
        _smoothness = _effect.smoothness(time);
        _input_depth = _effect.input_depth(time);
        _use_middle_gray = _effect.use_middle_gray(time);
        _middle_gray = _effect.middle_gray(time);
    }

    void calibrate()
    {   
        _effect.set_regen_calib(false);
        _effect.sample_points().clear();
        _effect.sample_set().clear();

        const float aspect = (float)_width / (float)_height;
        
        const int actual_samples = (_solver_type == 0) ? _samples : _samples * 100;
        const int x_points = std::max(1, (int)(sqrt(aspect * actual_samples)));
        const int y_points = std::max(1, actual_samples / x_points);

        const int step_x = std::max(1, _width / x_points);
        const int step_y = std::max(1, _height / y_points);

        for (int i = 0, x = step_x / 2; i < x_points; i++, x += step_x)
        {
            for (int j = 0, y = step_y / 2; j < y_points; j++, y += step_y)
            {
                if (0 <= x && x < _width && 0 <= y && y < _height)
                {
                    _effect.sample_points().push_back(fx::point(x, y));
                    _effect.sample_set().insert(fx::point(x, y).key());
                    spdlog::debug("{}: Getting sample pos({}, {})", fx::label, x, y);
                }
            }
        }
        
        std::thread threads[CMP_MAX];

        for (int c = 0; c < CMP_MAX; ++c)
        {
            if (_solver_type == 0)
            {
                threads[c] = std::thread(debevec_solver<ptype, OFX::Image>, c,
                                                        _input_depth,
                                                        _smoothness,
                                                        _sources,
                                                        _effect.sample_points(),
                                                        _exp_times_log,
                                                        _effect.input_weights(),
                                                        _effect.response(_input_depth, c));
            }
            else if (_solver_type == 1)
            {
                threads[c] = std::thread(robertson_solver<ptype, OFX::Image>, c,
                                                        _input_depth,
                                                        (int)_smoothness,
                                                        _sources,
                                                        _effect.sample_points(),
                                                        _exp_times,
                                                        _effect.input_weights(),
                                                        _effect.response(_input_depth, c));
            }
        }

        for (int c = 0; c < CMP_MAX; ++c)
            threads[c].join();

        if (_solver_type == 1)
            average_robertson_curves();
    }

    void calibrate_linear()
    {
        _effect.set_regen_calib(false);

        for (int i = 0; i < _input_depth; ++i)
            _effect.response_linear()[i] = std::log(i * (1.f / _input_depth));

        _effect.response_linear()[0] = _effect.response_linear()[1];
    }

    void average_robertson_curves()
    {
        /// Robertson runs per-channel independently, producing divergent curve shapes
        /// on sparse linear data. Average them into one shared curve to eliminate tints,
        /// then smooth to remove kinks from sparsely-sampled bins near highlights.
        for (int m = 0; m < _input_depth; ++m)
        {
            double avg = 0.0;
            for (int c = 0; c < CMP_MAX; ++c)
                avg += _effect.response(_input_depth, c)[m];
            avg /= CMP_MAX;
            for (int c = 0; c < CMP_MAX; ++c)
                _effect.response(_input_depth, c)[m] = avg;
        }

        /// 3 passes of box-filter smoothing approximates a Gaussian kernel,
        /// handling broader plateau-type banding from sparsely-sampled highlight bins.
        const int radius = 4;
        const int passes = 3;
        std::vector<double> smoothed(_input_depth);
        for (int pass = 0; pass < passes; ++pass)
        {
            double* curve = _effect.response(_input_depth, 0);
            for (int m = 0; m < _input_depth; ++m)
            {
                double sum = 0.0;
                int count = 0;
                for (int k = std::max(0, m - radius); k <= std::min(_input_depth - 1, m + radius); ++k)
                {
                    sum += curve[k];
                    ++count;
                }
                smoothed[m] = sum / count;
            }
            for (int m = 0; m < _input_depth; ++m)
                for (int c = 0; c < CMP_MAX; ++c)
                    _effect.response(_input_depth, c)[m] = smoothed[m];
        }
    }

    inline float lookup_response(int bin, int channel) const
    {
        return _calibrate
            ? (float)_effect.response(_input_depth, channel)[bin]
            : (float)_effect.response_linear()[bin];
    }

    inline float luminance(float* rgb)
    {
        return 0.212671f * rgb[fx::ch::r] + 0.71516f * rgb[fx::ch::g] + 0.072169f * rgb[fx::ch::b];
    }

    int pixel_size() { return _width * _height * _components; }
    void add_source(std::shared_ptr<OFX::Image> src_image) { _sources.push_back(src_image); }
    void add_exp_time(float val) { _exp_times.push_back(val); _exp_times_log.push_back(std::log(val)); }
    void set_resolution(const OfxRectI& window) { _width = window.x2 - window.x1; _height = window.y2 - window.y1; }
    void set_response() { _effect.set_response_size(CMP_MAX, _input_depth); }
    void set_linear_response() { _effect.set_response_linear_size(_input_depth); }
    
private:
    int _width = 0;
    int _height = 0;
    int _components = 0;

    fx::timer _timer;

    std::vector<float> _exp_times;
    std::vector<float> _exp_times_log;
    std::vector<std::shared_ptr<OFX::Image>> _sources;

    float _exposure = 0;
    float _gamma = 0;
    float _highlights = 0;
    bool _calibrate = false;
    bool _show_samples = false;
    int _samples = 0;
    int _solver_type = 0;
    float _smoothness = 0;
    int _input_depth = 0;

    float _luminance_max = 0;
    bool _use_middle_gray = false;
    float _middle_gray = 0;

    Effect<ptype>& _effect;
};

#endif