#include "AgentBase.h"

using namespace Terra;

// ================= CTOR =================
AgentBase::AgentBase(const std::string& name)
    : m_name(name)
{
}

// ================= DTOR =================
AgentBase::~AgentBase() {
    Stop();
}

// ================= ID =================
const std::string& AgentBase::GetName() const {
    return m_name;
}

// ================= LIFECYCLE =================
void AgentBase::Start() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_running) return;
    m_running = true;
}

void AgentBase::Stop() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_running) return;
    m_running = false;
}

bool AgentBase::IsRunning() const {
    return m_running.load();
}
