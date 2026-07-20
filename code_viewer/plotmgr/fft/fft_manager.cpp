#include "code_viewer/plotmgr/fft/fft_manager.h"
//#include "code_viewer/datamgr/math/fft_core.h"

#include <QtConcurrent/QtConcurrent>
#include <QThread>
#include <QMetaObject>

namespace viewer
{

FFTManager::FFTManager(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFutureWatcher<void>(this))
{
    connect(m_watcher, &QFutureWatcher<void>::finished, this, [this]()
    {
        m_running = false;
        if (!m_cancelled)
            emit finished();
    });
}

FFTManager::~FFTManager()
{
    cancel();
    if (m_watcher && m_watcher->isRunning())
        m_watcher->waitForFinished();
}

void FFTManager::reportProgress(float progress)
{
    if (!m_cancelled)
        emit progressChanged(progress);
}

void FFTManager::startFFT(
    Column* realCol,
    Column* imagCol,
    size_t fftN,
    double sampleInterval,
    std::function<void()> onFinished,
    std::function<void(float progress)> onProgress)
{
    if (m_running)
        return;

    m_running = true;
    m_cancelled = false;

    // 连接外部回调
    if (onFinished)
    {
        connect(this, &FFTManager::finished, this, onFinished, Qt::SingleShotConnection);
    }
    if (onProgress)
    {
        connect(this, &FFTManager::progressChanged, this,
            [onProgress](float p) { onProgress(p); }, Qt::SingleShotConnection);
    }

    // 通过信号安全地传递裸指针给后台线程
    // 后台线程通过 QMetaObject::invokeMethod 在主线程报告进度
    QPointer<FFTManager> self(this);

    QFuture<void> future = QtConcurrent::run([realCol, imagCol, fftN, sampleInterval, self]()
    {
        if (!realCol || !imagCol || fftN == 0)
            return;

        double* real = realCol->data();
        double* imag = imagCol->data();

        // ---- 位反转置换 ----
        if (self && !self->m_cancelled)
        {
            QMetaObject::invokeMethod(self, "reportProgress", Qt::QueuedConnection,
                Q_ARG(float, 0.05f));
        }

        size_t j = 0;
        for (size_t i = 0; i < fftN; ++i)
        {
            if (self && self->m_cancelled)
                return;

            if (i < j)
            {
                std::swap(real[i], real[j]);
                std::swap(imag[i], imag[j]);
            }
            size_t m = fftN >> 1;
            while (m >= 1 && j >= m)
            {
                j -= m;
                m >>= 1;
            }
            j += m;
        }

        if (self && !self->m_cancelled)
        {
            QMetaObject::invokeMethod(self, "reportProgress", Qt::QueuedConnection,
                Q_ARG(float, 0.10f));
        }

        // ---- 蝶形运算 ----
        const double pi = 3.14159265358979323846;
        size_t stages = 0;
        {
            size_t tmp = fftN;
            while (tmp > 1) { tmp >>= 1; ++stages; }
        }

        size_t stageIdx = 0;
        for (size_t len = 2; len <= fftN; len <<= 1)
        {
            if (self && self->m_cancelled)
                return;

            size_t half = len >> 1;
            double angle = -2.0 * pi / static_cast<double>(len);
            double wReal = std::cos(angle);
            double wImag = std::sin(angle);

            for (size_t i = 0; i < fftN; i += len)
            {
                double curWReal = 1.0;
                double curWImag = 0.0;

                for (size_t k = 0; k < half; ++k)
                {
                    size_t a = i + k;
                    size_t b = a + half;

                    double tReal = curWReal * real[b] - curWImag * imag[b];
                    double tImag = curWReal * imag[b] + curWImag * real[b];

                    real[b] = real[a] - tReal;
                    imag[b] = imag[a] - tImag;

                    real[a] += tReal;
                    imag[a] += tImag;

                    double tmpReal = curWReal * wReal - curWImag * wImag;
                    double tmpImag = curWReal * wImag + curWImag * wReal;
                    curWReal = tmpReal;
                    curWImag = tmpImag;
                }
            }

            ++stageIdx;
            if (self && !self->m_cancelled)
            {
                float prog = 0.10f + (0.80f * static_cast<float>(stageIdx) / static_cast<float>(stages));
                QMetaObject::invokeMethod(self, "reportProgress", Qt::QueuedConnection,
                    Q_ARG(float, prog));
            }
        }

        if (self && self->m_cancelled)
            return;

        // ---- 计算幅值 + 频率轴（仅保留 DC ~ Fs/2） ----
        double fs = 1.0 / sampleInterval;
        size_t halfN = fftN / 2;

        // 结果长度只保留 DC ~ Nyquist；容量来自 FFT 输入，不会在此重新分配。
        realCol->beginOverwrite(halfN + 1);
        imagCol->beginOverwrite(halfN + 1);
        for (size_t i = 0; i <= halfN; ++i)
        {
            if (self && (i % 4096 == 0) && self->m_cancelled)
                return;

            real[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
            imag[i] = static_cast<double>(i) * fs / static_cast<double>(fftN);
        }
        realCol->recalcMinMax();
        imagCol->recalcMinMax();

        if (self && !self->m_cancelled)
        {
            QMetaObject::invokeMethod(self, "reportProgress", Qt::QueuedConnection,
                Q_ARG(float, 1.0f));
        }
    });

    m_watcher->setFuture(future);
}

} // namespace viewer
