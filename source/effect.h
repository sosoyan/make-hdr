//
//  effect.h
//  MakeHDR
//
//  Created by Vahan Sosoyan 2024.
//

#ifndef effect_h
#define effect_h

#include "processor.h"


template <class ptype>
class Effect : public OFX::ImageEffect
{
public:
    Effect(OfxImageEffectHandle handle) : ImageEffect(handle)                            
    {
        for (int i = 0; i < SRC_MAX; ++i)
        {
            std::string src_name = std::to_string(i + 1);

            _src_clips.push_back(fetchClip(src_name));
            _exp_times.push_back(fetchDoubleParam(src_name));
        }

        _dst_clip = fetchClip(kOfxImageEffectOutputClipName);
    }

    ~Effect()
    {
    }

    virtual void changedParam(const OFX::InstanceChangedArgs& args, const std::string& paramName);
    virtual void render(const OFX::RenderArguments& args);

    void process(Processor<ptype>& processor, const OFX::RenderArguments& args);

    void set_log_level(int level);
    
    bool regen_calib() { return _regen_calib; };
    void set_regen_calib(bool regen_calib) { _regen_calib = regen_calib; };

    const std::vector<float>& input_weights() { return _input_weights; }
    
    void set_input_weights(int size);

    double* response(int depth, int channel) { return _response.data() + (depth * channel); }
    void set_response_size(int depth, int channel) { _response.resize(depth * channel); }

    double* response_linear() { return _response_linear.data(); }
    void set_response_linear_size(int depth) { _response_linear.resize(depth); }

    float exposure(const double& time) { return (float)_exposure->getValueAtTime(time); }
    float gamma(const double& time) { return (float)_gamma->getValueAtTime(time); }
    float highlights(const double& time) { return (float)_highlights->getValueAtTime(time); }
    bool calibrate(const double& time) { bool val; _calibrate->getValueAtTime(time, val); return val; }
    bool show_samples(const double& time) { bool val; _show_samples->getValueAtTime(time, val); return val; }
    int samples(const double& time) { return _samples->getValueAtTime(time); }
    float smoothness(const double& time) { return (float)_smoothness->getValueAtTime(time); }
    int input_depth(const double& time) { int depth; _input_depth->getValueAtTime(time, depth); return _input_depths[depth]; }
    int log_level(const double& time) { int level; _log_level->getValueAtTime(time, level); return level; }

protected:
    fx::timer _timer;

    bool _regen_calib = true;
    int _input_depths[3] = { 256, 1024, 4096 };

    std::vector<float> _input_weights;
    std::vector<double> _response;
    std::vector<double> _response_linear;

    OFX::Clip* _dst_clip;
    std::vector<OFX::Clip*> _src_clips;
    std::vector<OFX::DoubleParam*> _exp_times;

    OFX::DoubleParam* _exposure = fetchDoubleParam("exposure");
    OFX::DoubleParam* _gamma = fetchDoubleParam("gamma");
    OFX::DoubleParam* _highlights = fetchDoubleParam("highlights");
    OFX::BooleanParam* _calibrate = fetchBooleanParam("calibrate");
    OFX::BooleanParam* _show_samples = fetchBooleanParam("show_samples");
    OFX::IntParam* _samples = fetchIntParam("samples");
    OFX::DoubleParam* _smoothness = fetchDoubleParam("smoothness");
    OFX::ChoiceParam* _input_depth = fetchChoiceParam("input_depth");
    OFX::ChoiceParam* _log_level = fetchChoiceParam("log_level");   
};

class EffectPluginFactory : public OFX::PluginFactoryHelper<EffectPluginFactory> 
{
public: 
    EffectPluginFactory(const std::string& id, 
                        unsigned int ver_maj, 
                        unsigned int ver_min): OFX::PluginFactoryHelper<EffectPluginFactory>(id, ver_maj, ver_min)
    {
    } 

    ~EffectPluginFactory()
    {
    }

    virtual void describe(OFX::ImageEffectDescriptor& desc); 
    virtual void describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context); 
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
    {
        return new Effect<float>(handle);
    }

};

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray& ids)
        {
            static EffectPluginFactory p("net.sf.openfx.make_hdr", VERSION_MAJOR, VERSION_MINOR);
            ids.push_back(&p);
        }
    }
}

#endif