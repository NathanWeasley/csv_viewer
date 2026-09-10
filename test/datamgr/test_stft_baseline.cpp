#include "test_case.h"
#include "code_viewer/datamgr/math/stft_core.h"

#include <cmath>
#include <vector>

namespace
{

double harmonicAmplitude(const std::vector<double>& data,
                         double sampleFrequency,
                         double frequency,
                         size_t begin,
                         size_t end)
{
    const double pi = 3.14159265358979323846;
    double sinPart = 0.0;
    double cosPart = 0.0;
    for (size_t i = begin; i < end; ++i)
    {
        const double phase = 2.0 * pi * frequency
            * static_cast<double>(i) / sampleFrequency;
        sinPart += data[i] * std::sin(phase);
        cosPart += data[i] * std::cos(phase);
    }
    return 2.0 * std::sqrt(sinPart * sinPart + cosPart * cosPart)
        / static_cast<double>(end - begin);
}

} // namespace

TEST_GROUP(STFTBaseline)
{

TEST(STFTBaseline, ConstantSignalIsRemoved)
{
    std::vector<double> data(2048, 12.5);
    TEST_ASSERT_TRUE(viewer::zeroPhaseHighPassInPlace(data, 20.0, 0.1));
    for (const double value : data)
        TEST_ASSERT_NEAR(value, 0.0, 1e-12);
}

TEST(STFTBaseline, ForwardBackwardFilterKeepsImpulseSymmetric)
{
    std::vector<double> data(1001, 0.0);
    const size_t center = data.size() / 2;
    data[center] = 1.0;

    TEST_ASSERT_TRUE(viewer::zeroPhaseHighPassInPlace(data, 100.0, 5.0));
    for (size_t offset = 1; offset < 200; ++offset)
        TEST_ASSERT_NEAR(data[center - offset], data[center + offset], 1e-11);
}

TEST(STFTBaseline, SuppressesLowFrequencyAndPreservesHighFrequency)
{
    constexpr double sampleFrequency = 20.0;
    constexpr double lowFrequency = 0.02;
    constexpr double highFrequency = 2.0;
    const double pi = 3.14159265358979323846;
    std::vector<double> data(6000);
    for (size_t i = 0; i < data.size(); ++i)
    {
        const double time = static_cast<double>(i) / sampleFrequency;
        data[i] = std::sin(2.0 * pi * lowFrequency * time)
            + std::sin(2.0 * pi * highFrequency * time);
    }

    TEST_ASSERT_TRUE(viewer::zeroPhaseHighPassInPlace(data, sampleFrequency, 0.1));
    const double lowAmplitude = harmonicAmplitude(
        data, sampleFrequency, lowFrequency, 1000, 5000);
    const double highAmplitude = harmonicAmplitude(
        data, sampleFrequency, highFrequency, 1000, 5000);
    TEST_ASSERT_TRUE(lowAmplitude < 0.02);
    TEST_ASSERT_TRUE(highAmplitude > 0.9);
}

TEST(STFTBaseline, RejectsInvalidCutoffFrequency)
{
    std::vector<double> data(64, 1.0);
    TEST_ASSERT_FALSE(viewer::zeroPhaseHighPassInPlace(data, 10.0, 0.0));
    TEST_ASSERT_FALSE(viewer::zeroPhaseHighPassInPlace(data, 10.0, 5.0));
}

TEST(STFTBaseline, STFTAppliesBaselineRemovalBeforeWindowing)
{
    viewer::Column source(std::vector<double>(128, 5.0));
    const viewer::STFTResult raw = viewer::stftCompute(
        source, 128, 0, 128, 10.0, viewer::STFTWindowType::Rectangular,
        false, 0.1, true);
    const viewer::STFTResult filtered = viewer::stftCompute(
        source, 128, 0, 128, 10.0, viewer::STFTWindowType::Rectangular,
        true, 0.1, true);

    TEST_ASSERT_FALSE(raw.empty());
    TEST_ASSERT_FALSE(filtered.empty());
    TEST_ASSERT_TRUE(raw.spectrumValues[0] > 0.0);
    TEST_ASSERT_NEAR(filtered.spectrumValues[0], -240.0, 1e-9);
}

TEST(STFTBaseline, SelectsAmplitudeOrPowerSpectrum)
{
    viewer::Column source(std::vector<double>(8, 1.0));
    const viewer::STFTResult amplitude = viewer::stftCompute(
        source, 8, 0, 8, 8.0, viewer::STFTWindowType::Rectangular,
        false, 0.1, false);
    const viewer::STFTResult powerDb = viewer::stftCompute(
        source, 8, 0, 8, 8.0, viewer::STFTWindowType::Rectangular,
        false, 0.1, true);

    TEST_ASSERT_FALSE(amplitude.empty());
    TEST_ASSERT_FALSE(powerDb.empty());
    TEST_ASSERT_FALSE(amplitude.powerSpectrum);
    TEST_ASSERT_TRUE(powerDb.powerSpectrum);
    TEST_ASSERT_NEAR(amplitude.spectrumValues[0], 8.0, 1e-12);
    TEST_ASSERT_NEAR(powerDb.spectrumValues[0],
                     10.0 * std::log10(64.0), 1e-12);
    TEST_ASSERT_NEAR(powerDb.spectrumValues[1], -240.0, 1e-9);
}

} // TEST_GROUP(STFTBaseline)
