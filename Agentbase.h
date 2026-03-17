#pragma once

#include <string>
#include <atomic>
#include <mutex>

namespace Terra {

// ================= MESSAGE =================
struct AgentMessage {
    std::string sender;
    std::string payload;
};

// ================= BASE AGENT =================
class AgentBase {
public:
    AgentBase(const std::string& name);
    virtual ~AgentBase();

    // identity
    const std::string& GetName() const;

    // lifecycle
    virtual void Start();
    virtual void Stop();

    bool IsRunning() const;

    // messaging (core override point)
    virtual void OnMessage(const AgentMessage& msg) = 0;

protected:
    std::string m_name;

    std::atomic<bool> m_running{false};
    mutable std::mutex m_mutex;
};

}
