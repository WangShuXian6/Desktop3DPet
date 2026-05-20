#pragma once
#include <string>

struct PoseParams
{
    float rotationRad = 0.0f;
    float scale = 1.0f;
    float bounce = 0.0f;
    float wave = 0.0f;
    float mood = 0.0f;
};

class AnimationController
{
public:
    void Update(float dt);
    void RequestMontage(std::string const& id);
    PoseParams const& CurrentPose() const { return m_pose; }

private:
    enum class State
    {
        Idle,
        Wave
    };

    State m_state{ State::Idle };
    PoseParams m_pose{};
    float m_time{ 0.0f };
    float m_waveTime{ 0.0f };
};
