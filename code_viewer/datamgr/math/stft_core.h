#pragma once

#include "code_viewer/datamgr/math/fft_core.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace viewer
{

enum class STFTWindowType : int
{
    Hann = 0,
    Hamming,
    Rectangular
};

struct STFTResult
{
    size_t timeBinCount = 0;
    size_t freqBinCount = 0;
    std::vector<double> timeAxis;
    std::vector<double> freqAxis;
    std::vector<double> magnitudeDb;

    bool empty() const noexcept
    {
        return timeBinCount == 0 || freqBinCount == 0 || magnitudeDb.empty();
    }
};

inline std::vector<double> buildSTFTWindow(size_t windowSize, STFTWindowType windowType)
{
    std::vector<double> window(windowSize, 1.0);
    if (windowSize <= 1)
        return window;

    const double pi = 3.14159265358979323846;
    const double denom = static_cast<double>(windowSize - 1);
    for (size_t i = 0; i < windowSize; ++i)
    {
        const double phase = 2.0 * pi * static_cast<double>(i) / denom;
        switch (windowType)
        {
        case STFTWindowType::Hann:
            window[i] = 0.5 * (1.0 - std::cos(phase));
            break;
        case STFTWindowType::Hamming:
            window[i] = 0.54 - 0.46 * std::cos(phase);
            break;
        case STFTWindowType::Rectangular:
        default:
            window[i] = 1.0;
            break;
        }
    }
    return window;
}

namespace detail
{

struct HighPassBiquad
{
    double b0 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

inline HighPassBiquad makeButterworthHighPass(double sampleFrequency,
                                              double cutoffFrequency)
{
    const double pi = 3.14159265358979323846;
    const double omega = 2.0 * pi * cutoffFrequency / sampleFrequency;
    const double cosOmega = std::cos(omega);
    const double sinOmega = std::sin(omega);
    const double q = std::sqrt(0.5);
    const double alpha = sinOmega / (2.0 * q);
    const double a0 = 1.0 + alpha;

    HighPassBiquad coefficients;
    coefficients.b0 = (1.0 + cosOmega) * 0.5 / a0;
    coefficients.b1 = -(1.0 + cosOmega) / a0;
    coefficients.b2 = coefficients.b0;
    coefficients.a1 = -2.0 * cosOmega / a0;
    coefficients.a2 = (1.0 - alpha) / a0;
    return coefficients;
}

inline void applyHighPassPass(std::vector<double>& data,
                              size_t begin,
                              size_t end,
                              const HighPassBiquad& coefficients,
                              bool forward)
{
    if (begin >= end)
        return;

    // 以端点常值的稳态初始化，减小滤波器启动瞬态。
    const size_t firstIndex = forward ? begin : end - 1;
    const double endpoint = data[firstIndex];
    double state1 = -coefficients.b0 * endpoint;
    double state2 = coefficients.b2 * endpoint;
    const size_t length = end - begin;

    for (size_t offset = 0; offset < length; ++offset)
    {
        const size_t index = forward ? begin + offset : end - 1 - offset;
        const double input = data[index];
        const double output = coefficients.b0 * input + state1;
        state1 = coefficients.b1 * input - coefficients.a1 * output + state2;
        state2 = coefficients.b2 * input - coefficients.a2 * output;
        data[index] = output;
    }
}

} // namespace detail

// 二阶 Butterworth 高通前向、反向各执行一次，消除相位偏移。
inline bool zeroPhaseHighPassInPlace(std::vector<double>& data,
                                     double sampleFrequency,
                                     double cutoffFrequency)
{
    if (data.empty() || !std::isfinite(sampleFrequency)
        || !std::isfinite(cutoffFrequency) || sampleFrequency <= 0.0
        || cutoffFrequency <= 0.0 || cutoffFrequency >= sampleFrequency * 0.5)
        return false;

    const detail::HighPassBiquad coefficients =
        detail::makeButterworthHighPass(sampleFrequency, cutoffFrequency);

    // 非有限值作为分段边界，避免一个坏点污染其后的全部数据。
    size_t begin = 0;
    while (begin < data.size())
    {
        while (begin < data.size() && !std::isfinite(data[begin]))
            ++begin;
        size_t end = begin;
        while (end < data.size() && std::isfinite(data[end]))
            ++end;
        if (begin < end)
        {
            detail::applyHighPassPass(data, begin, end, coefficients, true);
            detail::applyHighPassPass(data, begin, end, coefficients, false);
        }
        begin = end;
    }
    return true;
}

inline STFTResult stftCompute(const Column& src,
                              size_t windowSize,
                              size_t overlap,
                              size_t fftSize,
                              double sampleFrequency,
                              STFTWindowType windowType,
                              bool removeBaseline = false,
                              double highPassCutoffFrequency = 0.1)
{
    STFTResult result;
    if (windowSize == 0 || fftSize == 0 || overlap >= windowSize || sampleFrequency <= 0.0)
        return result;
    if ((fftSize & (fftSize - 1)) != 0 || fftSize < windowSize || src.empty())
        return result;

    std::vector<double> filteredSamples;
    if (removeBaseline)
    {
        filteredSamples.resize(src.size());
        for (size_t i = 0; i < src.size(); ++i)
            filteredSamples[i] = src[i];
        if (!zeroPhaseHighPassInPlace(filteredSamples, sampleFrequency,
                                      highPassCutoffFrequency))
            return result;
    }

    const size_t hopSize = windowSize - overlap;
    const size_t sampleCount = src.size();
    const size_t frameCount = (sampleCount <= windowSize)
        ? 1
        : (1 + (sampleCount - windowSize + hopSize - 1) / hopSize);
    const size_t freqCount = fftSize / 2 + 1;

    result.timeBinCount = frameCount;
    result.freqBinCount = freqCount;
    result.timeAxis.resize(frameCount);
    result.freqAxis.resize(freqCount);
    result.magnitudeDb.resize(frameCount * freqCount);

    const std::vector<double> window = buildSTFTWindow(windowSize, windowType);
    std::vector<double> real(fftSize, 0.0);
    std::vector<double> imag(fftSize, 0.0);
    const double sampleInterval = 1.0 / sampleFrequency;
    const double minMagnitude = 1e-12;

    for (size_t k = 0; k < freqCount; ++k)
        result.freqAxis[k] = static_cast<double>(k) * sampleFrequency / static_cast<double>(fftSize);

    for (size_t frame = 0; frame < frameCount; ++frame)
    {
        const size_t start = frame * hopSize;
        std::fill(real.begin(), real.end(), 0.0);
        std::fill(imag.begin(), imag.end(), 0.0);

        for (size_t i = 0; i < windowSize; ++i)
        {
            const size_t srcIndex = start + i;
            if (srcIndex >= sampleCount)
                break;
            const double sample = removeBaseline ? filteredSamples[srcIndex] : src[srcIndex];
            real[i] = sample * window[i];
        }

        fft_radix2(real.data(), imag.data(), fftSize, sampleInterval);

        const double frameCenter = (static_cast<double>(start) + 0.5 * static_cast<double>(windowSize)) / sampleFrequency;
        result.timeAxis[frame] = frameCenter;

        for (size_t k = 0; k < freqCount; ++k)
        {
            const double mag = std::max(real[k], minMagnitude);
            result.magnitudeDb[k * frameCount + frame] = 20.0 * std::log10(mag);
        }
    }

    return result;
}

} // namespace viewer
