#pragma once

#include "code_viewer/base/base_def.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

namespace viewer
{

// ============================================================
// 基-2 时域抽取 (DIT) 原位 FFT
//
// real: 输入=信号实部 / 输出=幅值
// imag: 输入=信号虚部(全0) / 输出=频率采样点 (Hz)
// n:    FFT 点数 (必须是2的幂)
// sampleInterval: 采样间隔 (秒)
// ============================================================
inline void fft_radix2(double* real, double* imag, size_t n, double sampleInterval)
{
    if (n <= 1)
        return;

    // ---- 位反转置换 ----
    size_t j = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (i < j)
        {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        size_t m = n >> 1;
        while (m >= 1 && j >= m)
        {
            j -= m;
            m >>= 1;
        }
        j += m;
    }

    // ---- 蝶形运算 ----
    const double pi = 3.14159265358979323846;
    for (size_t len = 2; len <= n; len <<= 1)
    {
        size_t half = len >> 1;
        double angle = -2.0 * pi / static_cast<double>(len);

        // 预计算本级旋转因子
        double wReal = std::cos(angle);
        double wImag = std::sin(angle);

        for (size_t i = 0; i < n; i += len)
        {
            double curWReal = 1.0;
            double curWImag = 0.0;

            for (size_t k = 0; k < half; ++k)
            {
                size_t a = i + k;
                size_t b = a + half;

                // t = curW * X[b]
                double tReal = curWReal * real[b] - curWImag * imag[b];
                double tImag = curWReal * imag[b] + curWImag * real[b];

                // X[b] = X[a] - t
                real[b] = real[a] - tReal;
                imag[b] = imag[a] - tImag;

                // X[a] = X[a] + t
                real[a] += tReal;
                imag[a] += tImag;

                // 更新旋转因子: curW = curW * w
                double tmpReal = curWReal * wReal - curWImag * wImag;
                double tmpImag = curWReal * wImag + curWImag * wReal;
                curWReal = tmpReal;
                curWImag = tmpImag;
            }
        }
    }

    // ---- 计算幅值 + 频率轴（仅保留 DC ~ Fs/2） ----
    // real[0..halfN] → 幅值, imag[0..halfN] → 实际频率 (Hz)
    double fs = 1.0 / sampleInterval;
    size_t halfN = n / 2;

    // 先计算前 halfN+1 个 bin（DC ~ Fs/2）
    std::vector<double> magBuf(halfN + 1);
    std::vector<double> freqBuf(halfN + 1);
    for (size_t i = 0; i <= halfN; ++i)
    {
        magBuf[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
        freqBuf[i] = static_cast<double>(i) * fs / static_cast<double>(n);
    }

    // 将截断后的数据写回原数组（调用者应只使用前 halfN+1 个元素）
    for (size_t i = 0; i <= halfN; ++i)
    {
        real[i] = magBuf[i];
        imag[i] = freqBuf[i];
    }
}

// ============================================================
// 计算大于等于 n 的最小 2 的幂
// ============================================================
inline size_t nextPowerOfTwo(size_t n)
{
    if (n <= 1)
        return 1;
    size_t power = 1;
    while (power < n)
        power <<= 1;
    return power;
}

// ============================================================
// 从 Column 提取框选范围内的数据 → 两个 double* 数组（实部/虚部）
//
// srcReal: 源数据列
// start:   起始行索引
// count:   数据点数
// fftN:    FFT 点数 (≥ count, 必须为 2 的幂)
// outReal: 填充数据 + 补零的行 [0, count) + [count, fftN) 填 0
// outImag: 全零行 [0, fftN)
//
// 调用者负责 outReal/outImag 指向至少 fftN 个 double
// ============================================================
inline void prepareFFTInput(
    const Column& srcReal,
    size_t start,
    size_t count,
    size_t fftN,
    double* outReal,
    double* outImag)
{
    // 复制源数据
    size_t copyLimit = (count > fftN) ? fftN : count;
    for (size_t i = 0; i < copyLimit; ++i)
        outReal[i] = srcReal[start + i];

    // 补零
    for (size_t i = copyLimit; i < fftN; ++i)
        outReal[i] = 0.0;

    // 虚部全零
    std::fill(outImag, outImag + fftN, 0.0);
}

// ============================================================
// 完整 FFT 计算封装
//
// 输入:
//   srcReal:       源数据列
//   start, count:  框选范围
//   fftN:          FFT 点数 (必须为 2 的幂)
//   sampleInterval: 采样间隔 (秒)
//
// 输出:
//   outMag:  幅值列 (fftN 行)
//   outFreq: 频率列 (fftN 行)
// ============================================================
inline void fftCompute(
    const Column& srcReal,
    size_t start,
    size_t count,
    size_t fftN,
    double sampleInterval,
    Column& outMag,
    Column& outFreq)
{
    // 准备临时输入数组
    std::vector<double> real(fftN);
    std::vector<double> imag(fftN);

    prepareFFTInput(srcReal, start, count, fftN, real.data(), imag.data());

    // 基-2 FFT
    fft_radix2(real.data(), imag.data(), fftN, sampleInterval);

    // 将结果写回 Column（real=幅值, imag=频率，仅 DC ~ Fs/2）
    size_t halfN = fftN / 2;
    outMag.beginOverwrite(halfN + 1);
    outFreq.beginOverwrite(halfN + 1);
    for (size_t i = 0; i <= halfN; ++i)
    {
        outMag[i] = real[i];
        outFreq[i] = imag[i];
    }
    outMag.recalcMinMax();
    outFreq.recalcMinMax();
}

} // namespace viewer
