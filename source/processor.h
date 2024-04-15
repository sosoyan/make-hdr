//
//  processor.h
//  MakeHDR
//
//  Created by Vahan Sosoyan 2024.
//

#ifndef processor_h
#define processor_h

#include "resources.h"


template <class ptype>
class Effect;

template<typename ptype>
void solver(const int channel,
            const int input_depth,
            const float smoothness,
            const std::vector<std::shared_ptr<OFX::Image>>& sources,
            const std::vector<fx::point>& points,
            const std::vector<float>& exp_times_log,
            const std::vector<float>& input_weights,
            double* response)
{   
    const int sources_size = (int)sources.size();
    const int samples_size = (int)points.size();

    const int m = samples_size * sources_size + input_depth + 1;
    const int n = input_depth + samples_size;

    arma::mat a = arma::mat(m, n).zeros();
    arma::vec b = arma::vec(m).zeros();
    arma::vec s = arma::vec(n).zeros();

    int k = 0;
    for (int i = 0; i < samples_size; ++i)
    {
        for (int j = 0; j < sources_size; ++j)
        {           
            ptype* sample = (ptype*)sources[j]->getPixelAddress(points[i].x, points[i].y);
                     
            float sample_flt = sample == nullptr ? 0 : sample[channel];

            // Clamp 0 to 1
            sample_flt = std::min<ptype>(sample_flt, 1.f);
            sample_flt = std::max<ptype>(sample_flt, 0.f);

            const int sample_int = (int)(sample_flt * (input_depth - 1));

            const float wij = input_weights[sample_int];

            a.at(k, sample_int) = wij;
            a.at(k, input_depth + i) = -wij;
            b.at(k, 0) = wij * exp_times_log[j];
            k++;
        }
    }
    
    // Fix the scaling
    a.at(k, input_depth / 2) = 1;
    k++;

    // Smoothness equations
    const float lambda = smoothness * (input_depth / 256.f);

    for (int i = 0; i < (input_depth - 2); ++i)
    {
        float wi = input_weights[i + 1];

        a.at(k, i) = lambda * wi;
        a.at(k, i + 1) = -2 * lambda * wi;
        a.at(k, i + 2) = lambda * wi;
        k++;
    }

    const bool success = lapack::solve(s.memptr(), a.memptr(), b.memptr(), m, n);

    if (success)
    {
        for (int i = 0; i < input_depth; ++i)
            response[i] = s[i];
    }
    else
        spdlog::error("{}: Solver has faild for channel {}!", fx::label , channel);
}


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
        if (!_sources.empty())
        {
            if (!_effect.abort())
            {
                if (_effect.regen_calib())
                {
                    _effect.set_input_weights(_input_depth);

                    if (_calibrate)
                        calibrate();
                    else
                        calibrate_linear();
                }
                else
                    spdlog::debug("[{}] calibrate skipped!", fx::label);
            }
            else
                spdlog::debug("[{}] effect calibrate abort!", fx::label);
        }
        else
            spdlog::debug("[{}] sources are empty!", fx::label);
    }

    virtual void multiThreadProcessImages(OfxRectI proc_window)
    {
        if (!_sources.empty())
        {
            for (int y = proc_window.y1; y < proc_window.y2; ++y)
            {
                if (_effect.abort())
                    return;

                for (int x = proc_window.x1; x < proc_window.x2; ++x)
                {
                    float weight_sum = 0.f;
                    float response_log[3] = { 0.f, 0.f, 0.f };
                    float result[3] = { 0.f, 0.f, 0.f };

                    ptype* dst = (ptype*)_dstImg->getPixelAddress(x, y);

                    for (int i = 0; i < _sources.size(); ++i)
                    {
                        const ptype* src = (ptype*)_sources[i]->getPixelAddress(x, y);

                        if (src == nullptr)
                            return;

                        float weight_src = 0.f;

                        for (int c = 0; c < CMP_MAX; ++c)
                        {
                            ptype sample = src[c];

                            // Clamp 0 to 1
                            sample = std::min<ptype>(sample, 1.f);
                            sample = std::max<ptype>(sample, 0.f);

                            const int src_int = (int)(sample * (_input_depth - 1));

                            weight_src += _effect.input_weights()[src_int];
                            
                            if(_calibrate)
                                response_log[c] = (float)_effect.response(_input_depth, c)[src_int];
                            else
                                response_log[c] = (float)_effect.response_linear()[src_int];
                        }

                        weight_src /= CMP_MAX;

                        for (int c = 0; c < CMP_MAX; ++c)
                            result[c] += weight_src * (response_log[c] - _exp_times_log[i]);

                        weight_sum += weight_src;
                    }

                    weight_sum = 1.f / weight_sum;

                    for (int c = 0; c < CMP_MAX; ++c)
                    {
                        const float hdr = std::exp(result[c] * weight_sum) / _sources.size();
                        dst[c] = (ptype)pow(hdr * (float)std::pow(2, _exposure), 1.f / _gamma);
                    }

                    if(_show_samples)
                        for (auto point : _sample_points)
                            if (x == point.x && y == point.y)
                                dst[fx::ch::g] = FLT_MAX;

                    dst[fx::ch::a] = 1.0f;
                }
            }
        }
    };

    virtual void postProcess() 
    {
        ptype* dst = (ptype*)_dstImg->getPixelData();

        for (int i = 0; i < pixel_size(); i += _components)
            _luminance_max = max(luminance(dst + i), _luminance_max);

        for (int i = 0; i < pixel_size(); i += _components)
        {
            const float lum = luminance(dst + i);
            const float lum_dif = std::log10(1.f + lum) / std::log10(1.f + _luminance_max);

            for (int c = 0; c < CMP_MAX; ++c)
            {
                const float tone = lum_dif * dst[i + c] / lum;
                dst[i + c] = tone + (dst[i + c] - tone) * _highlights;
            }
        }

        _effect.set_regen_calib(true);

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
        _smoothness = _effect.smoothness(time);
        _input_depth = _effect.input_depth(time);
    }

    void calibrate()
    {   
        const float aspect = (float)_width / (float)_height;
        
        const int x_points = (int)(sqrt(aspect * _samples));
        const int y_points = _samples / x_points;

        const int step_x = _width / x_points;
        const int step_y = _height / y_points;

        for (int i = 0, x = step_x / 2; i < x_points; i++, x += step_x)
        {
            for (int j = 0, y = step_y / 2; j < y_points; j++, y += step_y)
            {
                if (0 <= x && x < _width && 0 <= y && y < _height)
                {
                    _sample_points.push_back(fx::point(x, y));
                    spdlog::debug("{}: Getting sample pos({}, {})", fx::label, x, y);
                }
            }
        }
           
        std::thread* threads = new std::thread[CMP_MAX];

        for (int c = 0; c < CMP_MAX; ++c)
        {
            threads[c] = std::thread(solver<ptype>, c,
                                                    _input_depth,
                                                    _smoothness,
                                                    _sources,
                                                    _sample_points,
                                                    _exp_times_log,
                                                    _effect.input_weights(),
                                                    _effect.response(_input_depth, c));                                               
        }

        for (int c = 0; c < CMP_MAX; ++c)
            threads[c].join();

        delete[] threads;
    }

    void calibrate_linear()
    {
        for (int i = 0; i < _input_depth; ++i)
            _effect.response_linear()[i] = std::log(i * (1.f / _input_depth));

        _effect.response_linear()[0] = _effect.response_linear()[1];
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
    std::vector<fx::point> _sample_points;
    std::vector <std::shared_ptr<OFX::Image>> _sources;

    float _exposure = 0;
    float _gamma = 0;
    float _highlights = 0;
    bool _calibrate = false;
    bool _show_samples = false;
    int _samples = 0;
    float _smoothness = 0;
    int _input_depth = 0;

    float _luminance_max = 0;

    Effect<ptype>& _effect;
};

#endif