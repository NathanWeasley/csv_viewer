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

inline STFTResult stftCompute(const Column& src,
                              size_t windowSize,
                              size_t overlap,
                              size_t fftSize,
                              double sampleFrequency,
                              STFTWindowType windowType)
{
    STFTResult result;
    if (windowSize == 0 || fftSize == 0 || overlap >= windowSize || sampleFrequency <= 0.0)
        return result;
    if ((fftSize & (fftSize - 1)) != 0 || fftSize < windowSize || src.empty())
        return result;

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
            real[i] = src[srcIndex] * window[i];
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
