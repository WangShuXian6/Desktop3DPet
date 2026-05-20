#include "pch.h"
#include "AnimationController.h"

void AnimationController::Update(float dt)
{
    m_time += dt;

    m_pose.rotationRad = std::sinf(m_time * 1.6f) * 0.08f;
    m_pose.bounce = 0.5f + 0.5f * std::sinf(m_time * 3.2f);
    m_pose.scale = 1.0f;
    m_pose.wave = 0.0f;
    m_pose.mood = 0.35f + 0.35f * std::sinf(m_time * 0.7f);

    if (m_state == State::Wave)
    {
        m_waveTime += dt;
        m_pose.wave = std::sinf(m_waveTime * 16.0f) * 0.7f;
        m_pose.rotationRad += std::sinf(m_waveTime * 7.0f) * 0.14f;
        m_pose.scale = 1.0f + 0.04f * std::sinf(m_waveTime * 12.0f);

        if (m_waveTime > 1.8f)
        {
            m_waveTime = 0.0f;
            m_state = State::Idle;
        }
    }
}

void AnimationController::RequestMontage(std::string const& id)
{
    if (id == "wave")
    {
        m_state = State::Wave;
        m_waveTime = 0.0f;
    }
}
