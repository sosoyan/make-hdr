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

/// Implements Mark A. Robertson et al., 1999
/// "Dynamic Range Improvement Through Multiple Exposures"
template<typename ptype, typename ImageType>
void robertson_solver(const int channel,
                      const int input_depth,
                      const int iterations,
                      const std::vector<std::shared_ptr<ImageType>>& sources,
                      const std::vector<fx::point>& points,
                      const std::vector<float>& exp_times,
                      const std::vector<float>& input_weights,
                      double* response)
{

    const int sources_size = (int)sources.size();
    const int samples_size = (int)points.size();

    std::vector<double> I(input_depth);
    for (int i = 0; i < input_depth; ++i)
        I[i] = (double)i / (double)(input_depth - 1);

    std::vector<double> E(samples_size, 0.0);

    /// Precompute sample integers to avoid fetching pixel data during iterations
    std::vector<std::vector<int>> sample_ints(samples_size, std::vector<int>(sources_size));
    for (int i = 0; i < samples_size; ++i)
    {
        for (int j = 0; j < sources_size; ++j)
        {
            sample_ints[i][j] = extract_pixel_index<ptype, ImageType>(sources[j], points[i], channel, input_depth);
        }
    }

    for (int iter = 0; iter < iterations; ++iter)
    {
        /// 1. Estimate irradiance E for each sample
        /// x_j = sum(w(y_ij) * t_i * I(y_ij)) / sum(w(y_ij) * t_i^2)
        for (int i = 0; i < samples_size; ++i)
        {
            double sum_num = 0.0;
            double sum_den = 0.0;

            for (int j = 0; j < sources_size; ++j)
            {
                const int s_int = sample_ints[i][j];
                const double w = input_weights[s_int];
                const double t = exp_times[j];

                sum_num += w * t * I[s_int];
                sum_den += w * t * t;
            }

            E[i] = (sum_den > 0.0) ? (sum_num / sum_den) : 0.0;
        }

        /// 2. Update response function I
        /// I(m) = sum(t_i * x_j) / Card(y_ij = m)
        std::vector<double> sum_I_num(input_depth, 0.0);
        std::vector<double> sum_I_den(input_depth, 0.0);

        for (int i = 0; i < samples_size; ++i)
        {
            for (int j = 0; j < sources_size; ++j)
            {
                const int s_int = sample_ints[i][j];
                const double t = exp_times[j];

                sum_I_num[s_int] += t * E[i];
                sum_I_den[s_int] += 1.0; 
            }
        }

        for (int m = 0; m < input_depth; ++m)
        {
            if (sum_I_den[m] > 0.0)
                I[m] = sum_I_num[m] / sum_I_den[m];
            else
                I[m] = -1.0; // Mark unobserved
        }

        /// 3. Interpolate unobserved values in the Log-Domain
        int last_valid = -1;
        for (int m = 0; m < input_depth; ++m) {
            if (I[m] >= 0.0) {
                if (last_valid == -1) {
                    for (int k = 0; k < m; ++k) I[k] = I[m] * ((double)k / std::max(1, m)); 
                } else if (m - last_valid > 1) {
                    double v0 = std::log(std::max(1e-12, I[last_valid]));
                    double v1 = std::log(std::max(1e-12, I[m]));
                    for (int k = last_valid + 1; k < m; ++k) {
                        double t = (double)(k - last_valid) / (m - last_valid);
                        I[k] = std::exp(v0 + t * (v1 - v0));
                    }
                }
                last_valid = m;
            }
        }
        if (last_valid != -1 && last_valid < input_depth - 1) {
            for (int k = last_valid + 1; k < input_depth; ++k) {
                I[k] = I[last_valid] * 1.001; // Slight extrapolation
            }
        }

        /// 4. Enforce strict monotonicity (crucial to prevent color channel inversions)
        for (int m = 1; m < input_depth; ++m) {
            if (I[m] < I[m - 1]) I[m] = I[m - 1];
        }

        /// 5. Normalize to Mid-Value
        const double mid_val = I[input_depth / 2];
        if (mid_val > 0.0)
        {
            for (int m = 0; m < input_depth; ++m)
                I[m] /= mid_val;
        }
    }

    /// 6. Output Logarithmic Response exactly like Debevec so processor logic stays identical
    for (int m = 0; m < input_depth; ++m)
    {
        if (I[m] <= 0.0)
            response[m] = std::log(1e-6); 
        else
            response[m] = std::log(I[m]);
    }
}

#endif