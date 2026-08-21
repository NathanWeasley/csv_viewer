#pragma once

#include <QVector>
#include <QtGlobal>

namespace view3d
{

enum class PlaybackDirection
{
    Reverse = -1,
    Forward = 1
};

class PlaybackState final
{
public:
    void setTimeline(const QVector<double>& timelineSeconds);
    void setFrame(qsizetype frame);
    void setSpeed(double speed);
    void play(PlaybackDirection direction);
    void pause();
    qsizetype advance(double elapsedSeconds);

    qsizetype frame() const noexcept;
    qsizetype frameCount() const noexcept;
    double speed() const noexcept;
    bool isPlaying() const noexcept;
    PlaybackDirection direction() const noexcept;

private:
    QVector<double> m_timeline;
    qsizetype m_frame = 0;
    double m_playTime = 0.0;
    double m_speed = 1.0;
    PlaybackDirection m_direction = PlaybackDirection::Forward;
    bool m_playing = false;
};

} // namespace view3d
