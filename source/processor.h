//
//  processor.h
//  MakeHDR
//
//  Created by Vahan Sosoyan 2024.
//

#ifndef processor_h
#define processor_h

#include "resources.h"
#include "merge.h"

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.H"

#include "spdlog/spdlog.h"


template <class ptype>
class Effect;

template <class ptype>
class Processor : public OFX::ImageProcessor
{
public:
    Processor(Effect<ptype>& effect,
              const unsigned int components) : OFX::ImageProcessor(effect),
                                               _effect(effect)
    {
        _params.channels = static_cast<int>(components);
    }

    ~Processor()
    {
    }

    virtual void preProcess()
    {
        if (_sources.empty())
        {
            spdlog::debug("[{}] sources are empty!", makehdr::label);
            return;
        }
        if (_effect.abort())
        {
            spdlog::debug("[{}] effect calibrate abort!", makehdr::label);
            return;
        }
        if (!_effect.regen_calib() && !_effect.input_weights().empty())
        {
            spdlog::debug("[{}] calibrate skipped!", makehdr::label);
            return;
        }

        _effect.set_input_weights(_params.depth);
        _params.calibrate ? calibrate() : calibrate_linear();
    }

    virtual void multiThreadProcessImages(OfxRectI proc_window)
    {
        if (_sources.empty()) return;

        makehdr::merge_hdr_region<ptype, OFX::Image, OFX::Image>(
            _sources, _exp_times_log,
            _effect.input_weights(),
            _effect.response(_params.depth, 0),
            _effect.response_linear(),
            _dstImg,
            proc_window.x1, proc_window.y1, proc_window.x2, proc_window.y2,
            _params,
            [this]{ return _effect.abort(); });

        if (_show_samples)
        {
            for (const makehdr::point& p : _effect.sample_points())
            {
                ptype* dp = static_cast<ptype*>(_dstImg->getPixelAddress(p.x, p.y));
                if (dp)
                    dp[static_cast<int>(makehdr::channel::g)] = FLT_MAX;
            }
        }
    }

    virtual void postProcess()
    {
        if (_sources.empty()) return;

        ptype* dst = static_cast<ptype*>(_dstImg->getPixelData());

        makehdr::post_process_buffer<ptype>(
            dst, pixel_size(), _params);

        if (!_effect.abort())
            spdlog::info("[{}] {} sources merged in {}ms", makehdr::label, _sources.size(), _timer.get());
    }

    void set_parameters(const double& time)
    {
        _params.exposure        = _effect.exposure(time);
        _params.gamma           = _effect.gamma(time);
        _params.highlights      = _effect.highlights(time);
        _params.calibrate       = _effect.calibrate(time);
        _params.samples         = _effect.samples(time);
        _params.solver_type     = _effect.solver_type(time);
        _params.smoothness      = _effect.smoothness(time);
        _params.robertson_iters = static_cast<int>(_params.smoothness);
        _params.depth           = _effect.input_depth(time);
        _params.use_middle_gray = _effect.use_middle_gray(time);
        _params.middle_gray     = _effect.middle_gray(time);
        _show_samples           = _effect.show_samples(time);
    }

    void calibrate()
    {
        _effect.set_regen_calib(false);

        if (_params.solver_type == 0)
        {
            _effect.sample_points() = makehdr::build_sample_points(_width, _height, _params.samples);
            const std::array<bool, CMP_MAX> results = makehdr::run_debevec_calibration<ptype, OFX::Image>(
                _sources, _exp_times_log, _params,
                _effect.input_weights(), _effect.sample_points(),
                _effect.response(_params.depth, 0));
            for (int c = 0; c < CMP_MAX; ++c)
                if (!results[c])
                {
                    spdlog::error("{}: Debevec calibration failed on {} channel — "
                                  "check exposure spread and sample count",
                                  makehdr::label, "RGB"[c]);
                    break;
                }
        }
        else if (_params.solver_type == 1)
        {
            _effect.sample_points() = makehdr::build_sample_points(_width, _height, _params.samples * 100);
            const std::array<bool, CMP_MAX> results = makehdr::run_robertson_calibration<ptype, OFX::Image>(
                _sources, _exp_times, _params,
                _effect.input_weights(), _effect.sample_points(),
                _effect.response(_params.depth, 0));
            for (int c = 0; c < CMP_MAX; ++c)
                if (!results[c])
                {
                    spdlog::error("{}: Robertson calibration failed on {} channel — "
                                  "no sample points could be generated",
                                  makehdr::label, "RGB"[c]);
                    break;
                }
        }

        // Rebuild the sample set for O(1) pixel lookup during rendering.
        _effect.sample_set().clear();
        for (const makehdr::point& p : _effect.sample_points())
        {
            _effect.sample_set().insert(p.key());
            spdlog::debug("{}: Getting sample pos({}, {})", makehdr::label, p.x, p.y);
        }
    }

    void calibrate_linear()
    {
        _effect.set_regen_calib(false);
        makehdr::build_linear_response(_params.depth, _effect.response_linear());
    }

    int pixel_size() { return _width * _height * _params.channels; }
    void add_source(std::shared_ptr<OFX::Image> src_image) { _sources.push_back(src_image); }
    void add_exp_time(float val) { _exp_times.push_back(val); _exp_times_log.push_back(std::log(val)); }
    void set_resolution(const OfxRectI& window) { _width = window.x2 - window.x1; _height = window.y2 - window.y1; }
    void set_response() { _effect.set_response_size(CMP_MAX, _params.depth); }
    void set_linear_response() { _effect.set_response_linear_size(_params.depth); }
    
private:
    int _width = 0;
    int _height = 0;

    makehdr::timer _timer;
    makehdr::merge_params _params;

    bool _show_samples = false;

    std::vector<float> _exp_times;
    std::vector<float> _exp_times_log;
    std::vector<std::shared_ptr<OFX::Image>> _sources;

    Effect<ptype>& _effect;
};

#endif
