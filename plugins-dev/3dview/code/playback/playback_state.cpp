#include "playback_state.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace view3d
{

void PlaybackState::setFrameCount(qsizetype frameCount)
{
    m_frameCount = std::max<qsizetype>(0, frameCount);
    m_frame = 0;
    m_position = 0.0;
    m_playing = false;
}

void PlaybackState::setFrame(qsizetype frame)
{
    if (m_frameCount <= 0)
    {
        m_frame = 0;
        m_position = 0.0;
        return;
    }
    m_frame = qBound<qsizetype>(0, frame, m_frameCount - 1);
    m_position = static_cast<double>(m_frame);
}

void PlaybackState::setSpeed(double speed)
{
    if (std::isfinite(speed) && speed > 0.0)
        m_speed = speed;
}

void PlaybackState::play(PlaybackDirection direction)
{
    if (m_frameCount <= 0)
        return;
    m_direction = direction;
    m_playing = true;
}

void PlaybackState::pause()
{
    m_playing = false;
}

qsizetype PlaybackState::advance()
{
    if (!m_playing || m_frameCount <= 0)
        return m_frame;

    m_position += m_speed * static_cast<int>(m_direction);
    if (m_position <= 0.0)
    {
        m_position = 0.0;
        m_frame = 0;
        m_playing = false;
        return m_frame;
    }
    const double lastFrame = static_cast<double>(m_frameCount - 1);
    if (m_position >= lastFrame)
    {
        m_position = lastFrame;
        m_frame = m_frameCount - 1;
        m_playing = false;
        return m_frame;
    }

    m_frame = m_direction == PlaybackDirection::Forward
        ? static_cast<qsizetype>(std::floor(m_position))
        : static_cast<qsizetype>(std::ceil(m_position));
    return m_frame;
}

qsizetype PlaybackState::frame() const noexcept
{
    return m_frame;
}

qsizetype PlaybackState::frameCount() const noexcept
{
    return m_frameCount;
}

double PlaybackState::speed() const noexcept
{
    return m_speed;
}

bool PlaybackState::isPlaying() const noexcept
{
    return m_playing;
}

PlaybackDirection PlaybackState::direction() const noexcept
{
    return m_direction;
}

} // namespace view3d
