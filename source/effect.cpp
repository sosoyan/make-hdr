#include "effect.h"


template <class ptype>
void Effect<ptype>::changedParam(const OFX::InstanceChangedArgs& args, const std::string& param_name)
{
    if (param_name == "exposure" ||
        param_name == "gamma" ||
        param_name == "highlights" ||
        param_name == "show_samples" ||
        param_name == "log_level")
    {
        _regen_calib = false;
    }
    else
        _regen_calib = true;
}

template <class ptype>
void Effect<ptype>::render(const OFX::RenderArguments& args)
{
    OFX::BitDepthEnum dst_bit_depth = _dst_clip->getPixelDepth();
    OFX::PixelComponentEnum dst_components = _dst_clip->getPixelComponents();

    if (dst_components == OFX::ePixelComponentRGBA)
    {
        switch (dst_bit_depth)
        {
            case OFX::eBitDepthFloat:
            {
                Processor<float> proc(*this, 4);
                process(proc, args);
                break;
            }
            default:
            {
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
            }
        }
    }
    else
        spdlog::error("[{}] destination must have RGBA components!", fx::label);
}

template <class ptype>
void Effect<ptype>::process(Processor<ptype>& processor, const OFX::RenderArguments& args)
{
    std::unique_ptr<OFX::Image> dst_image(_dst_clip->fetchImage(args.time));
    
    if (dst_image.get() != nullptr)
    {
        set_log_level(log_level(args.time));

        OFX::BitDepthEnum dst_bit_depth = dst_image->getPixelDepth();
        OFX::PixelComponentEnum dst_components = dst_image->getPixelComponents();

        for (int i = 0; i < SRC_MAX; ++i)
        {
            OFX::Clip* src_clip = _src_clips[i];

            if (src_clip != nullptr)
            {
                std::shared_ptr<OFX::Image> src_image(src_clip->fetchImage(args.time));
                const float exp_time = (float)_exp_times[i]->getValueAtTime(args.time);

                if (src_image.get() != nullptr)
                {
                    OFX::BitDepthEnum src_bit_depth = src_image->getPixelDepth();
                    OFX::PixelComponentEnum src_components = src_image->getPixelComponents();

                    if (src_bit_depth == dst_bit_depth && src_components == dst_components)
                    {
                        if (exp_time > 0)
                        {
                            if (dst_image->getBounds().x1 == src_image->getBounds().x1 &&
                                dst_image->getBounds().x2 == src_image->getBounds().x2 &&
                                dst_image->getBounds().y1 == src_image->getBounds().y1 &&
                                dst_image->getBounds().x2 == src_image->getBounds().x2)
                            {
                                processor.add_source(src_image);
                                processor.add_exp_time(exp_time);
                            }
                            else
                                spdlog::warn("[{}] source {} bounds does not match the destination skipping!", fx::label, i);
                        }
                        else
                            spdlog::warn("[{}] source {} exposure time is {}, skipping!", fx::label, i, exp_time);
                    }
                    else
                        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
                }
                else
                    spdlog::debug("[{}] source image is empty!", fx::label);
            }
            else
                spdlog::debug("[{}] source clip {} is empty!", fx::label, i);
        }

        spdlog::debug("[{}] processing frame {}, render window ({}, {}, {}, {})", fx::label,
            args.time,
            args.renderWindow.x1,
            args.renderWindow.x2,
            args.renderWindow.y1, 
            args.renderWindow.y2);

        processor.setDstImg(dst_image.get());
        processor.setRenderWindow(args.renderWindow);
        processor.set_resolution(args.renderWindow);
        processor.set_parameters(args.time);
        processor.set_linear_response();
        processor.set_response();
        processor.process();
    }
    else
        spdlog::error("[{}] destination image is empty!", fx::label);
}

template<class ptype>
void Effect<ptype>::set_log_level(int level)
{
    switch (level)
    {
        case 0:
        {
            spdlog::set_level(spdlog::level::off);
            break;
        }
        case 1:
        {
            spdlog::set_level(spdlog::level::err);
            break;
        }
        case 2:
        {
            spdlog::set_level(spdlog::level::warn);
            break;
        }
        case 3:
        {
            spdlog::set_level(spdlog::level::info);
            break;
        }
        case 4:
        {
            spdlog::set_level(spdlog::level::debug);
            break;
        }
    }
}

template<class ptype>
void Effect<ptype>::set_input_weights(int size)
{
    if (size != _input_weights.size())
    {
        _input_weights.resize(size);

        const int half_depth = size / 2;

        for (int i = 0; i < size; ++i)
            _input_weights[i] = i < half_depth ? i + 1.f : size - i;
    }
}

void EffectPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
    desc.setLabels(fx::label, fx::label, fx::label);
    desc.setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_FIX, 0, "");
    desc.setPluginDescription(fx::description);
    desc.setPluginGrouping("OFX");

    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(true);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(false);
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}

void EffectPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // Setup parameters
    OFX::GroupParamDescriptor* exposure_times_group = desc.defineGroupParam("exposure_times");
    OFX::GroupParamDescriptor* tone_mapping_group = desc.defineGroupParam("tone_mapping");
    OFX::GroupParamDescriptor* advanced_group = desc.defineGroupParam("advanced");

    OFX::BooleanParamDescriptor* calibrate_param = desc.defineBooleanParam("calibrate");
    OFX::DoubleParamDescriptor* exposure_param = desc.defineDoubleParam("exposure");
    OFX::DoubleParamDescriptor* gamma_param = desc.defineDoubleParam("gamma");
    OFX::DoubleParamDescriptor* highlights_param = desc.defineDoubleParam("highlights");
    OFX::BooleanParamDescriptor* show_samples_param = desc.defineBooleanParam("show_samples");
    OFX::IntParamDescriptor* samples_param = desc.defineIntParam("samples");
    OFX::DoubleParamDescriptor* smoothness_param = desc.defineDoubleParam("smoothness");
    OFX::ChoiceParamDescriptor* input_depth_param = desc.defineChoiceParam("input_depth");
    OFX::ChoiceParamDescriptor* log_level_param = desc.defineChoiceParam("log_level");

    exposure_times_group->setLabel("exposure times");
    tone_mapping_group->setLabel("tone mapping");

    calibrate_param->setDefault(true);
    calibrate_param->setLabel("calibrate response");
    calibrate_param->setParent(*exposure_times_group);

    exposure_param->setDefault(0);
    exposure_param->setDisplayRange(-16, 16);
    exposure_param->setParent(*tone_mapping_group);

    gamma_param->setDefault(1.0);
    gamma_param->setDisplayRange(0, 2);
    gamma_param->setParent(*tone_mapping_group);

    highlights_param->setDefault(1.0);
    highlights_param->setDisplayRange(0, 1);
    highlights_param->setParent(*tone_mapping_group);
    
    show_samples_param->setDefault(false);
    show_samples_param->setParent(*advanced_group);
    show_samples_param->setLabel("show samples");

    input_depth_param->appendOption("8 bit");
    input_depth_param->appendOption("10 bit");
    input_depth_param->appendOption("12 bit");
    input_depth_param->setParent(*advanced_group);
    input_depth_param->setLabel("input depth");

    samples_param->setDefault(100);
    samples_param->setDisplayRange(1, 100);
    samples_param->setParent(*advanced_group);

    smoothness_param->setDefault(50);
    smoothness_param->setDisplayRange(1, 100);
    smoothness_param->setParent(*advanced_group);

    log_level_param->appendOption("off");
    log_level_param->appendOption("error");
    log_level_param->appendOption("warn");
    log_level_param->appendOption("info");
    log_level_param->appendOption("debug");
    log_level_param->setDefault(2);
    log_level_param->setParent(*advanced_group);
    log_level_param->setLabel("log level");

    advanced_group->setOpen(false);

    // Setup inputs
    for (int i = 0; i < SRC_MAX; ++i)
    {
        const std::string src_name = std::to_string(i + 1);

        OFX::ClipDescriptor* src_clip = desc.defineClip(src_name);
        src_clip->addSupportedComponent(OFX::ePixelComponentRGBA);

        if (i > 0)
            src_clip->setOptional(true);

        OFX::DoubleParamDescriptor* exp_times_param = desc.defineDoubleParam(src_name);
        exp_times_param->setDisplayRange(0, 1);
        exp_times_param->setHint("Shutter speed of corresponding source in seconds");
        exp_times_param->setParent(*exposure_times_group);
    }

    OFX::ClipDescriptor* dst_clip = desc.defineClip(kOfxImageEffectOutputClipName);
    dst_clip->addSupportedComponent(OFX::ePixelComponentRGBA);

}