// =============================== // TransformerBase.h (Production Ready v1.1) // =============================== #pragma once

#include "AIConnector.h" #include <string> #include <vector> #include <map> #include <deque> #include <memory> #include <mutex> #include <atomic> #include <chrono>

namespace TerraEngine {

// ------------------------------- // STATE // ------------------------------- enum class TransformerState { IDLE, THINKING, RESPONDING, FAILED, RECOVERING };

// ------------------------------- // TASK // ------------------------------- struct TransformerTask { std::string id; std::string context; std::string question;

int priority = 5;

std::chrono::milliseconds timeout{5000};

std::chrono::steady_clock::time_point created_at{std::chrono::steady_clock::now()};

};

// ------------------------------- // RESULT // ------------------------------- struct TransformerResult {

bool success = false;

std::string output;
std::string error;

std::string task_id;

int transformer_index = -1;

float confidence = 0.0f;

double latency_ms = 0.0;

};

// ============================================================ // TRANSFORMER BASE // ============================================================ class TransformerBase {

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


// ---------------- Memory ----------------

void AddMemory(const std::string& key, const std::string& value);

std::string GetMemory(const std::string& key) const;

void ClearMemory();

size_t MemorySize() const;


struct Stats {

    int tasks_processed = 0;

    int tasks_failed = 0;

    double avg_latency_ms = 0.0;

    int recovery_count = 0;
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

std::map<std::string, std::string> m_memory;

std::deque<std::string> m_memory_order;

static constexpr size_t MAX_MEMORY = 50;


mutable std::mutex m_stats_mutex;

Stats m_stats;


void UpdateStats(bool success, double latency);

};

// ============================================================ // TRANSFORMER POOL // ============================================================ class TransformerPool {

public:

explicit TransformerPool(const std::string& agent);


void AddTransformer(std::shared_ptr<TransformerBase> t);


TransformerResult Process(const TransformerTask& task);


TransformerResult ProcessOn(int index, const TransformerTask& task);


int HealthyCount() const;

bool HasHealthy() const;


void ResetAll();


std::shared_ptr<TransformerBase> Get(int index);


std::shared_ptr<TransformerBase> GetHealthy(int preferred = 0);

private:

std::string m_agent;

std::vector<std::shared_ptr<TransformerBase>> m_transformers;

mutable std::mutex m_mutex;

};

}

// =============================== // TransformerBase.cpp // ===============================

#include "TransformerBase.h" #include <sstream> #include <algorithm>

namespace TerraEngine {

static std::unique_ptr<AIConnector> g_ai;

static std::once_flag g_ai_once;

AIConnector& TransformerBase::GetAI() {

std::call_once(g_ai_once, [](){

    g_ai = std::make_unique<AIConnector>();

    g_ai->Init(".env");

});

return *g_ai;

}

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

TransformerResult TransformerBase::Process(const TransformerTask& task) {

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


AIResponse response = GetAI().CallWithFallback(req, fallbacks);


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

std::string TransformerBase::BuildPrompt(const TransformerTask& task) const {

std::stringstream ss;


ss<<"Agent:"<<m_agent_name<<"\n";

ss<<"Domain:"<<m_domain<<"\n";

ss<<"Context:"<<task.context<<"\n";

ss<<"Task:"<<task.question<<"\n";


return ss.str();

}

TransformerResult TransformerBase::ParseResponse(

const AIResponse& response,

const TransformerTask& task

) const {

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

bool TransformerBase::IsHealthy() const {

return m_state != TransformerState::FAILED;

}

TransformerState TransformerBase::GetState() const {

return m_state.load();

}

void TransformerBase::Reset() {

m_state = TransformerState::IDLE;

m_fail_count = 0;

}

void TransformerBase::MarkFailed(const std::string& reason) {

m_last_error = reason;

m_state = TransformerState::FAILED;

}

void TransformerBase::AddMemory(const std::string& key,const std::string& value) {

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

std::string TransformerBase::GetMemory(const std::string& key) const {

std::lock_guard<std::mutex> lock(m_memory_mutex);


auto it = m_memory.find(key);


if(it==m_memory.end())

    return "";


return it->second;

}

void TransformerBase::ClearMemory() {

std::lock_guard<std::mutex> lock(m_memory_mutex);


m_memory.clear();

m_memory_order.clear();

}

size_t TransformerBase::MemorySize() const {

std::lock_guard<std::mutex> lock(m_memory_mutex);


return m_memory.size();

}

void TransformerBase::UpdateStats(bool success,double latency) {

std::lock_guard<std::mutex> lock(m_stats_mutex);


m_stats.tasks_processed++;


if(!success)

    m_stats.tasks_failed++;


double delta = latency - m_stats.avg_latency_ms;


m_stats.avg_latency_ms += delta / m_stats.tasks_processed;

}

TransformerBase::Stats TransformerBase::GetStats() const {

std::lock_guard<std::mutex> lock(m_stats_mutex);


return m_stats;

}

// ============================================================ // POOL // ============================================================

TransformerPool::TransformerPool(const std::string& agent)

: m_agent(agent)

{}

void TransformerPool::AddTransformer(std::shared_ptr<TransformerBase> t) {

std::lock_guard<std::mutex> lock(m_mutex);


if(m_transformers.size()>=4)

    return;


m_transformers.push_back(t);

}

TransformerResult TransformerPool::Process(const TransformerTask& task) {

std::lock_guard<std::mutex> lock(m_mutex);


for(auto& t: m_transformers)

{

    if(!t)

        continue;


    if(!t->IsHealthy())

        continue;


    auto result = t->Process(task);


    if(result.success)

        return result;

}


TransformerResult r;

r.error = "all transformers failed";


return r;

}

TransformerResult TransformerPool::ProcessOn(int index,const TransformerTask& task) {

std::lock_guard<std::mutex> lock(m_mutex);


if(index<0 || index>=m_transformers.size())

{

    TransformerResult r;

    r.error="index out of range";


    return r;

}


return m_transformers[index]->Process(task);

}

int TransformerPool::HealthyCount() const {

std::lock_guard<std::mutex> lock(m_mutex);


int c=0;


for(auto& t:m_transformers)

    if(t && t->IsHealthy())

        c++;


return c;

}

bool TransformerPool::HasHealthy() const {

return HealthyCount()>0;

}

void TransformerPool::ResetAll() {

std::lock_guard<std::mutex> lock(m_mutex);


for(auto& t:m_transformers)

    if(t)

        t->Reset();

}

std::shared_ptr<TransformerBase> TransformerPool::Get(int index) {

std::lock_guard<std::mutex> lock(m_mutex);


if(index<0 || index>=m_transformers.size())

    return nullptr;


return m_transformers[index];

}

std::shared_ptr<TransformerBase> TransformerPool::GetHealthy(int preferred) {

std::lock_guard<std::mutex> lock(m_mutex);


if(preferred>=0 && preferred<m_transformers.size())

{

    auto& t = m_transformers[preferred];

    if(t && t->IsHealthy())

        return t;

}


for(auto& t:m_transformers)

    if(t && t->IsHealthy())

        return t;


return nullptr;

}

}
