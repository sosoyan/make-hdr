#include "effect.h"


template <class ptype>
void Effect<ptype>::changedParam(const OFX::InstanceChangedArgs& args, const std::string& param_name)
{
    if (param_name != "exposure" &&
        param_name != "gamma" &&
        param_name != "highlights" &&
        param_name != "use_middle_gray" &&
        param_name != "middle_gray" &&
        param_name != "show_samples" &&
        param_name != "log_level")
    {
        _regen_calib = true;
    }

    if (param_name == "use_middle_gray")
    {
        bool use;
        _use_middle_gray->getValue(use);
        _middle_gray->setEnabled(use);
    }
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

            if (src_clip != nullptr && src_clip->isConnected())
            {
                const float exp_time = (float)_exp_times[i]->getValueAtTime(args.time);

                if (exp_time > 0)
                {
                    std::shared_ptr<OFX::Image> src_image(src_clip->fetchImage(args.time));

                    if (src_image.get() != nullptr)
                    {
                        OFX::BitDepthEnum src_bit_depth = src_image->getPixelDepth();
                        OFX::PixelComponentEnum src_components = src_image->getPixelComponents();

                        if (src_bit_depth == dst_bit_depth && src_components == dst_components)
                        {
                            if (dst_image->getBounds().x1 == src_image->getBounds().x1 &&
                                dst_image->getBounds().x2 == src_image->getBounds().x2 &&
                                dst_image->getBounds().y1 == src_image->getBounds().y1 &&
                                dst_image->getBounds().y2 == src_image->getBounds().y2)
                            {
                                processor.add_source(src_image);
                                processor.add_exp_time(exp_time);
                            }
                            else
                                spdlog::warn("[{}] source {} bounds does not match the destination, skipping!", fx::label, i + 1);
                        }
                        else
                            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
                    }
                    else
                        spdlog::debug("[{}] source image is empty!", fx::label);
                }
                else
                    spdlog::warn("[{}] source {} exposure time is not set (= {}), skipping!", fx::label, i + 1, exp_time);
            }
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

        for (int i = 0; i < size; ++i)
            _input_weights[i] = (float)std::min(i, size - 1 - i);
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
    OFX::BooleanParamDescriptor* use_middle_gray_param = desc.defineBooleanParam("use_middle_gray");
    OFX::DoubleParamDescriptor* exposure_param = desc.defineDoubleParam("exposure");
    OFX::DoubleParamDescriptor* gamma_param = desc.defineDoubleParam("gamma");
    OFX::DoubleParamDescriptor* highlights_param = desc.defineDoubleParam("highlights");
    OFX::RGBAParamDescriptor* middle_gray_param = desc.defineRGBAParam("middle_gray");
    OFX::BooleanParamDescriptor* show_samples_param = desc.defineBooleanParam("show_samples");
    OFX::IntParamDescriptor* samples_param = desc.defineIntParam("samples");
    OFX::ChoiceParamDescriptor* solver_param = desc.defineChoiceParam("solver");
    OFX::DoubleParamDescriptor* smoothness_param = desc.defineDoubleParam("smoothness");
    OFX::ChoiceParamDescriptor* input_depth_param = desc.defineChoiceParam("input_depth");
    OFX::ChoiceParamDescriptor* log_level_param = desc.defineChoiceParam("log_level");

    exposure_times_group->setLabel("exposure times");
    tone_mapping_group->setLabel("tone mapping");

    calibrate_param->setDefault(true);
    calibrate_param->setLabel("calibrate response");
    calibrate_param->setHint("When on, the camera response curve is estimated from the source images using the selected solver. When off, a linear response is assumed.");
    calibrate_param->setParent(*exposure_times_group);

    use_middle_gray_param->setDefault(false);
    use_middle_gray_param->setLabel("target middle gray");
    use_middle_gray_param->setHint("Enable middle gray normalisation. When on, the merged image is scaled so its geometric mean luminance matches the target middle gray value.");
    use_middle_gray_param->setParent(*tone_mapping_group);

    exposure_param->setDefault(0);
    exposure_param->setDisplayRange(-16, 16);
    exposure_param->setHint("Global exposure adjustment in stops applied to the merged HDR image before tone mapping.");
    exposure_param->setParent(*tone_mapping_group);

    gamma_param->setDefault(1.0);
    gamma_param->setDisplayRange(0, 2);
    gamma_param->setHint("Gamma exponent applied per-pixel after HDR merging. 1.0 keeps linear output.");
    gamma_param->setParent(*tone_mapping_group);

    highlights_param->setDefault(1.0);
    highlights_param->setDisplayRange(0, 1);
    highlights_param->setHint("Blends between fully tone-mapped (0) and linear (1) output. Lower values compress highlights more aggressively.");
    highlights_param->setParent(*tone_mapping_group);

    middle_gray_param->setDefault(0.18, 0.18, 0.18, 1.0);
    middle_gray_param->setLabel("middle gray");
    middle_gray_param->setHint("Pick the reference gray patch from the scene (e.g. a color checker). The plugin scales the merged image so the luminance of the picked color matches the scene average.");
    middle_gray_param->setEnabled(false);
    middle_gray_param->setParent(*tone_mapping_group);

    show_samples_param->setDefault(false);
    show_samples_param->setParent(*advanced_group);
    show_samples_param->setLabel("show samples");
    show_samples_param->setHint("Overlay the calibration sample positions on the output image as bright green pixels.");

    input_depth_param->appendOption("8 bit");
    input_depth_param->appendOption("10 bit");
    input_depth_param->appendOption("12 bit");
    input_depth_param->setParent(*advanced_group);
    input_depth_param->setLabel("input depth");
    input_depth_param->setHint("Bit depth of the original camera footage. Determines the response curve resolution (256, 1024, or 4096 bins).");

    samples_param->setDefault(100);
    samples_param->setRange(1, 100);
    samples_param->setDisplayRange(1, 100);
    samples_param->setHint("Debevec: number of sample points. Robertson: multiplied by 100 to compensate for sparse bin coverage (e.g. 100 = 10000 samples).");
    samples_param->setParent(*advanced_group);
    solver_param->appendOption("debevec");
    solver_param->appendOption("robertson");
    solver_param->setDefault(0);
    solver_param->setParent(*advanced_group);
    solver_param->setLabel("solver");
    solver_param->setHint("Response curve estimation algorithm. Debevec solves a linear system (fast, sparse samples). Robertson uses an iterative expectation-maximisation approach.");
    smoothness_param->setDefault(50);
    smoothness_param->setRange(1, 100);
    smoothness_param->setDisplayRange(1, 100);
    smoothness_param->setHint("Debevec: regularization strength. Robertson: number of iterations.");
    smoothness_param->setLabel("smoothness / iterations");
    smoothness_param->setParent(*advanced_group);

    log_level_param->appendOption("off");
    log_level_param->appendOption("error");
    log_level_param->appendOption("warn");
    log_level_param->appendOption("info");
    log_level_param->appendOption("debug");
    log_level_param->setDefault(2);
    log_level_param->setParent(*advanced_group);
    log_level_param->setLabel("log level");
    log_level_param->setHint("Controls the verbosity of plugin logging. Off silences all output; debug prints detailed per-frame diagnostics.");

    advanced_group->setOpen(false);

    // Setup inputs
    for (int i = 0; i < SRC_MAX; ++i)
    {
        const std::string src_name = "src" + std::to_string(i + 1);

        OFX::ClipDescriptor* src_clip = desc.defineClip(src_name);
        src_clip->addSupportedComponent(OFX::ePixelComponentRGBA);
        src_clip->setLabels(std::to_string(i + 1), std::to_string(i + 1), std::to_string(i + 1));

        if (i > 0)
            src_clip->setOptional(true);

        OFX::DoubleParamDescriptor* exp_times_param = desc.defineDoubleParam(src_name);
        exp_times_param->setLabels(std::to_string(i + 1), std::to_string(i + 1), std::to_string(i + 1));
        exp_times_param->setDisplayRange(0, 1);
        exp_times_param->setHint("Shutter speed of corresponding source in seconds");
        exp_times_param->setParent(*exposure_times_group);
    }

    OFX::ClipDescriptor* dst_clip = desc.defineClip(kOfxImageEffectOutputClipName);
    dst_clip->addSupportedComponent(OFX::ePixelComponentRGBA);

}