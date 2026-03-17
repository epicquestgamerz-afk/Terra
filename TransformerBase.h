// ============================================================
// TERRA ENGINE — TRANSFORMER BASE HEADER
// TransformerBase.h — Version 2.0
// ============================================================

#pragma once

#include "AIConnector.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <deque>
#include <atomic>

namespace TerraEngine {

// ============================================================
// STATE
// ============================================================
enum class TransformerState {
    IDLE,
    THINKING,
    RESPONDING,
    FAILED,
    RECOVERING
};

// ============================================================
// TASK
// ============================================================
struct TransformerTask {

    std::string id;
    std::string context;
    std::string question;

    int priority = 5;

    std::chrono::milliseconds timeout{5000};

    std::chrono::steady_clock::time_point created_at{
        std::chrono::steady_clock::now()
    };
};

// ============================================================
// RESULT
// ============================================================
struct TransformerResult {

    bool success = false;

    std::string output;
    std::string error;

    std::string task_id;

    int transformer_index = -1;

    float confidence = 0.0f;

    double latency_ms = 0.0;
};

// ============================================================
// BASE CLASS
// ============================================================
class TransformerBase {

public:

    TransformerBase(
        int index,
        const std::string& domain,
        const std::string& agent_name,
        Model model = Model::HF_PHI3
    );

    virtual ~TransformerBase() = default;

    virtual TransformerResult Process(const TransformerTask& task);

    bool IsHealthy() const;

    TransformerState GetState() const;

    int GetIndex() const { return m_index; }

    const std::string& GetDomain() const { return m_domain; }

    int GetFailCount() const { return m_fail_count.load(); }

    void Reset();

    void MarkFailed(const std::string& reason);

    // MEMORY
    void AddMemory(const std::string& key,const std::string& value);

    std::string GetMemory(const std::string& key) const;

    void ClearMemory();

    size_t MemorySize() const;

    // STATS
    struct Stats {

        int tasks_processed = 0;

        int tasks_failed = 0;

        double avg_latency_ms = 0.0;
    };

    Stats GetStats() const;

protected:

    virtual std::string BuildPrompt(const TransformerTask& task) const;

    virtual TransformerResult ParseResponse(
        const AIResponse& response,
        const TransformerTask& task
    ) const;

    static AIConnector& GetAI();

protected:

    int m_index;

    std::string m_domain;

    std::string m_agent_name;

    Model m_model;

private:

    std::atomic<TransformerState> m_state{TransformerState::IDLE};

    std::atomic<int> m_fail_count{0};

    std::string m_last_error;

    mutable std::mutex m_memory_mutex;

    std::map<std::string,std::string> m_memory;

    std::deque<std::string> m_memory_order;

    static constexpr size_t MAX_MEMORY = 50;

    mutable std::mutex m_stats_mutex;

    Stats m_stats;

    void UpdateStats(bool success,double latency);
};

}
