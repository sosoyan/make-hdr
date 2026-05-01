//
//  solver.h
//  MakeHDR
//
//  Created by Vahan Sosoyan 2024.
//

#ifndef solver_h
#define solver_h

#include "resources.h"


/// Extracts the pixel value for a given point and channel, 
/// returning an integer in the range [0, input_depth-1].
template<typename ptype, typename ImageType>
inline int extract_pixel_index(const std::shared_ptr<ImageType>& source, 
            const fx::point& point, 
            const int channel, 
            const int input_depth)
{
    ptype* sample = (ptype*)source->getPixelAddress(point.x, point.y);
    float sample_flt = sample == nullptr ? 0 : sample[channel];
    sample_flt = std::min<float>(sample_flt, 1.f);
    sample_flt = std::max<float>(sample_flt, 0.f);

    return (int)(sample_flt * (input_depth - 1));
}

/// Implements Paul E. Debevec & Jitendra Malik, 1997
/// "Recovering High Dynamic Range Radiance Maps from Photographs"
template<typename ptype, typename ImageType>
void debevec_solver(const int channel,
            const int input_depth,
            const float smoothness,
            const std::vector<std::shared_ptr<ImageType>>& sources,
            const std::vector<fx::point>& points,
            const std::vector<float>& exp_times_log,
            const std::vector<float>& input_weights,
            double* response)
{

    const int sources_size = (int)sources.size();
    const int samples_size = (int)points.size();

    const int m = samples_size * sources_size + (input_depth - 2) + 1;
    const int n = input_depth + samples_size;

    arma::mat a = arma::mat(m, n).zeros();
    arma::vec b = arma::vec(m).zeros();
    arma::vec s = arma::vec(n).zeros();

    int k = 0;
    for (int i = 0; i < samples_size; ++i)
    {
        for (int j = 0; j < sources_size; ++j)
        {           
            const int sample_int = extract_pixel_index<ptype, ImageType>(sources[j], points[i], channel, input_depth);

            const float wij = input_weights[sample_int];

            /// 1. Data Objective Function
            /// w(Z_ij) * g(Z_ij) - w(Z_ij) * ln(E_i) = w(Z_ij) * ln(dt_j)
            a.at(k, sample_int) = wij;
            a.at(k, input_depth + i) = -wij;
            b.at(k, 0) = wij * exp_times_log[j];
            k++;
        }
    }
    
    /// 2. Mid-Value Constraint
    /// Fix the camera response curve scaling at the center point (g(Z_mid) = 0)
    a.at(k, input_depth / 2) = 1;
    k++;

    /// 3. Smoothness Objective Function
    /// Minimize the second derivative: lambda * w(z) * (g(z-1) - 2*g(z) + g(z+1)) = 0
    const float lambda = smoothness * (input_depth / 256.f);

    for (int i = 0; i < (input_depth - 2); ++i)
    {
        float wi = input_weights[i + 1];

        a.at(k, i) = lambda * wi;
        a.at(k, i + 1) = -2 * lambda * wi;
        a.at(k, i + 2) = lambda * wi;
        k++;
    }

    bool success = arma::solve(s, a, b);
    
    /// Fallback to SVD pseudo-inverse if the system is singular or ill-conditioned
    if (!success)
    {
        arma::mat a_pinv;
        success = arma::pinv(a_pinv, a);
        if (success)
            s = a_pinv * b;
    }

    if (success)
    {
        for (int i = 0; i < input_depth; ++i)
            response[i] = s[i];
    }
    else
        spdlog::error("{}: Solver has failed for channel {}!", fx::label , channel);
}

#endif