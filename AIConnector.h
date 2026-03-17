#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <functional>
#include <atomic>

namespace TerraEngine {

// ================= ENUMS =================
enum class Provider {
    HUGGINGFACE,
    OPENROUTER,
    DEEPSEEK,
    GEMINI,
    GROQ,
    CLOUDFLARE,
    MISTRAL
};

enum class Model {
    UNKNOWN,

    // HF
    HF_PHI3, HF_QWEN, HF_MISTRAL, HF_LLAMA, HF_GEMMA, HF_BARK, HF_WHISPER,

    // OR
    OR_GPT4O, OR_CLAUDE, OR_GEMINI, OR_LLAMA405B, OR_MIXTRAL, OR_CLAUDE_HAIKU,

    // DS
    DS_CHAT, DS_CODER, DS_REASONER,

    // GM
    GM_PRO, GM_FLASH, GM_ULTRA,

    // GQ
    GQ_LLAMA, GQ_MIXTRAL, GQ_GEMMA, GQ_LLAMA_FAST,

    // CF
    CF_LLAMA, CF_MISTRAL, CF_GEMMA,

    // MS
    MS_LARGE, MS_MEDIUM, MS_SMALL, MS_NEMO
};

// ================= REQUEST =================
struct AIRequest {
    Model model = Model::UNKNOWN;
    std::string prompt;
    std::string system_prompt;
    int max_tokens = 512;
    float temperature = 0.7f;
};

// ================= RESPONSE =================
struct AIResponse {
    bool success = false;
    std::string text;
    std::string error;

    int retry_count = 0;
    double latency_ms = 0;

    Provider provider_used;
    std::string provider_name;
};

// ================= PROVIDER CONFIG =================
struct ProviderConfig {
    std::string api_key;
    std::string base_url;

    bool enabled = true;
    int rate_limit = 100;
    int calls_made = 0;
};

// ================= MAIN CLASS =================
class AIConnector {
public:
    AIConnector();
    ~AIConnector();

    bool Init(const std::string& env_path);

    AIResponse Call(const AIRequest& request);

    void CallAsync(const AIRequest& request,
                   std::function<void(AIResponse)> callback);

    void WaitAllAsync();

private:
    // core
    AIResponse CallWithRetry(const AIRequest&, Provider);
    AIResponse DispatchCall(const AIRequest&, Provider);

    // provider handlers
    AIResponse CallOpenAICompat(const AIRequest&, const ProviderConfig&);
    AIResponse CallHuggingFace(const AIRequest&, const ProviderConfig&);
    AIResponse CallGemini(const AIRequest&, const ProviderConfig&);
    AIResponse CallCloudflare(const AIRequest&, const ProviderConfig&);

    // setup
    bool LoadConfig(const std::string&);
    void InitModelMappings();

    // helpers
    bool IsRateLimited(Provider);
    void IncrementCount(Provider);
    std::string ProviderName(Provider) const;

private:
    std::map<Provider, ProviderConfig> m_providers;
    std::map<Model, Provider> m_model_providers;
    std::map<Model, std::string> m_model_ids;

    struct Config {
        int max_retries = 3;
        int timeout_seconds = 30;
    } m_config;

    std::vector<std::thread> m_threads;
    std::mutex m_thread_mutex;
};

} // namespace TerraEngine
