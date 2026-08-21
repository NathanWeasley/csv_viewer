#include "playback_state.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace view3d
{

void PlaybackState::setTimeline(const QVector<double>& timelineSeconds)
{
    m_timeline = timelineSeconds;
    for (qsizetype index = 1; index < m_timeline.size(); ++index)
    {
        if (!std::isfinite(m_timeline.at(index))
            || m_timeline.at(index) < m_timeline.at(index - 1))
        {
            m_timeline.clear();
            break;
        }
    }
    m_frame = 0;
    m_playTime = m_timeline.isEmpty() ? 0.0 : m_timeline.first();
    m_playing = false;
}

void PlaybackState::setFrame(qsizetype frame)
{
    if (m_timeline.isEmpty())
    {
        m_frame = 0;
        m_playTime = 0.0;
        return;
    }
    m_frame = qBound<qsizetype>(0, frame, m_timeline.size() - 1);
    m_playTime = m_timeline.at(m_frame);
}

void PlaybackState::setSpeed(double speed)
{
    if (std::isfinite(speed) && speed > 0.0)
        m_speed = speed;
}

void PlaybackState::play(PlaybackDirection direction)
{
    if (m_timeline.isEmpty())
        return;
    m_direction = direction;
    m_playing = true;
}

void PlaybackState::pause()
{
    m_playing = false;
}

qsizetype PlaybackState::advance(double elapsedSeconds)
{
    if (!m_playing || m_timeline.isEmpty()
        || !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0)
        return m_frame;

    m_playTime += elapsedSeconds * m_speed
        * static_cast<int>(m_direction);
    if (m_playTime <= m_timeline.first())
    {
        m_playTime = m_timeline.first();
        m_frame = 0;
        m_playing = false;
        return m_frame;
    }
    if (m_playTime >= m_timeline.last())
    {
        m_playTime = m_timeline.last();
        m_frame = m_timeline.size() - 1;
        m_playing = false;
        return m_frame;
    }

    const auto begin = m_timeline.constBegin();
    const auto end = m_timeline.constEnd();
    const auto upper = std::upper_bound(begin, end, m_playTime);
    m_frame = std::max<qsizetype>(0, std::distance(begin, upper) - 1);
    return m_frame;
}

qsizetype PlaybackState::frame() const noexcept
{
    return m_frame;
}

qsizetype PlaybackState::frameCount() const noexcept
{
    return m_timeline.size();
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
