// ============================================================
// TERRA ENGINE — TRANSFORMER BASE IMPLEMENTATION
// TransformerBase.cpp — Version 2.0
// ============================================================

#include "TransformerBase.h"

#include <sstream>
#include <algorithm>

namespace TerraEngine {

// ============================================================
// SHARED AI CONNECTOR
// ============================================================

static std::unique_ptr<AIConnector> g_ai;

static std::once_flag g_ai_once;

AIConnector& TransformerBase::GetAI()
{
    std::call_once(g_ai_once, [](){

        g_ai = std::make_unique<AIConnector>();

        g_ai->Init(".env");

    });

    return *g_ai;
}

// ============================================================
// CONSTRUCTOR
// ============================================================

TransformerBase::TransformerBase(

    int index,

    const std::string& domain,

    const std::string& agent_name,

    Model model

)

: m_index(index)

, m_domain(domain)

, m_agent_name(agent_name)

, m_model(model)

{}

// ============================================================
// PROCESS
// ============================================================

TransformerResult TransformerBase::Process(const TransformerTask& task)
{

    TransformerResult result;

    result.task_id = task.id;

    result.transformer_index = m_index;

    if(m_state == TransformerState::FAILED)
    {
        result.error = "transformer failed";
        return result;
    }

    m_state = TransformerState::THINKING;

    auto start = std::chrono::steady_clock::now();

    std::string prompt = BuildPrompt(task);

    AIRequest req;

    req.model = m_model;

    req.prompt = prompt;

    req.max_tokens = 200;

    std::vector<Model> fallbacks = {

        Model::GQ_LLAMA,

        Model::HF_MISTRAL,

        Model::HF_PHI3

    };

    AIResponse response = GetAI().CallWithFallback(req,fallbacks);

    auto end = std::chrono::steady_clock::now();

    double latency = std::chrono::duration<double,std::milli>(end-start).count();

    result = ParseResponse(response,task);

    result.transformer_index = m_index;

    result.latency_ms = latency;

    if(result.success)
    {
        m_state = TransformerState::IDLE;
        UpdateStats(true,latency);
    }
    else
    {
        m_fail_count++;

        m_state = m_fail_count >= 3
            ? TransformerState::FAILED
            : TransformerState::RECOVERING;

        UpdateStats(false,latency);
    }

    return result;
}

// ============================================================
// BUILD PROMPT
// ============================================================

std::string TransformerBase::BuildPrompt(const TransformerTask& task) const
{

    std::stringstream ss;

    ss<<"Agent:"<<m_agent_name<<"\n";

    ss<<"Domain:"<<m_domain<<"\n";

    ss<<"Context:"<<task.context<<"\n";

    ss<<"Task:"<<task.question<<"\n";

    return ss.str();
}

// ============================================================
// PARSE RESPONSE
// ============================================================

TransformerResult TransformerBase::ParseResponse(

    const AIResponse& response,

    const TransformerTask& task

) const
{

    TransformerResult result;

    result.task_id = task.id;

    if(!response.success)
    {
        result.error = response.error;
        return result;
    }

    if(response.text.empty())
    {
        result.error = "empty ai response";
        return result;
    }

    result.success = true;

    result.output = response.text;

    result.confidence = 0.8f;

    return result;
}

// ============================================================
// HEALTH
// ============================================================

bool TransformerBase::IsHealthy() const
{
    return m_state != TransformerState::FAILED;
}

TransformerState TransformerBase::GetState() const
{
    return m_state.load();
}

void TransformerBase::Reset()
{
    m_state = TransformerState::IDLE;
    m_fail_count = 0;
}

void TransformerBase::MarkFailed(const std::string& reason)
{
    m_last_error = reason;
    m_state = TransformerState::FAILED;
}

// ============================================================
// MEMORY
// ============================================================

void TransformerBase::AddMemory(const std::string& key,const std::string& value)
{

    std::lock_guard<std::mutex> lock(m_memory_mutex);

    if(m_memory.size()>=MAX_MEMORY)
    {

        auto oldest = m_memory_order.front();

        m_memory.erase(oldest);

        m_memory_order.pop_front();

    }

    m_memory[key] = value;

    m_memory_order.push_back(key);

}

std::string TransformerBase::GetMemory(const std::string& key) const
{

    std::lock_guard<std::mutex> lock(m_memory_mutex);

    auto it = m_memory.find(key);

    if(it==m_memory.end())
        return "";

    return it->second;
}

void TransformerBase::ClearMemory()
{

    std::lock_guard<std::mutex> lock(m_memory_mutex);

    m_memory.clear();

    m_memory_order.clear();

}

size_t TransformerBase::MemorySize() const
{

    std::lock_guard<std::mutex> lock(m_memory_mutex);

    return m_memory.size();

}

// ============================================================
// STATS
// ============================================================

void TransformerBase::UpdateStats(bool success,double latency)
{

    std::lock_guard<std::mutex> lock(m_stats_mutex);

    m_stats.tasks_processed++;

    if(!success)
        m_stats.tasks_failed++;

    double delta = latency - m_stats.avg_latency_ms;

    m_stats.avg_latency_ms += delta / m_stats.tasks_processed;

}

TransformerBase::Stats TransformerBase::GetStats() const
{

    std::lock_guard<std::mutex> lock(m_stats_mutex);

    return m_stats;

}

}
