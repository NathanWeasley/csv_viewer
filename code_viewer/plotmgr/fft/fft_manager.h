#pragma once

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <functional>

#include "code_viewer/base/base_def.h"
#include "code_viewer/datamgr/data_struct.hpp"

namespace viewer
{

// ============================================================
// FFTManager: FFT 异步线程管理器
//
// 职责：
//   - 接收两个 Column 裸指针 + FFT 参数
//   - 通过 QConcurrent 启动后台计算
//   - 通过 Qt 信号报告进度和完成
// ============================================================
class VIEWER_API FFTManager : public QObject
{
    Q_OBJECT

public:
    explicit FFTManager(QObject* parent = nullptr);
    ~FFTManager() override;

    // 禁止拷贝
    FFTManager(const FFTManager&) = delete;
    FFTManager& operator=(const FFTManager&) = delete;

    // ============================================================
    // 启动 FFT 计算
    //
    // realCol:  输入=信号数据 (viewer::Column*), 输出=幅值
    // imagCol:  输入=全0 (viewer::Column*), 输出=频率采样点
    // fftN:     FFT 点数 (必须是2的幂)
    // sampleInterval: 采样间隔 (秒)
    //
    // 回调:
    //   onFinished: 计算完成 (在主线程调用)
    //   onProgress: 进度 0.0~1.0 (在主线程调用)
    // ============================================================
    void startFFT(
        Column* realCol,
        Column* imagCol,
        size_t fftN,
        double sampleInterval,
        std::function<void()> onFinished,
        std::function<void(float progress)> onProgress);

    // 是否正在计算
    bool isRunning() const noexcept { return m_running; }

    // 取消当前计算（尽力而为，不会强制终止线程）
    void cancel() { m_cancelled = true; }

    // 更新进度（由后台线程通过 QMetaObject::invokeMethod 调用）
    Q_INVOKABLE void reportProgress(float progress);

Q_SIGNALS:
    void progressChanged(float progress);
    void finished();

private:
    QFutureWatcher<void>* m_watcher = nullptr;
    bool m_running = false;
    bool m_cancelled = false;
};

} // namespace viewer