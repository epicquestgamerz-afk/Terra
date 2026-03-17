#include "AIConnector.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>

using json = nlohmann::json;
using namespace TerraEngine;

// ================= CURL CALLBACK =================
static size_t WriteCallback(void* contents, size_t size,
                            size_t nmemb, std::string* out)
{
    out->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// ================= CTOR / DTOR =================
AIConnector::AIConnector() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

AIConnector::~AIConnector() {
    WaitAllAsync();
    curl_global_cleanup();
}

// ================= INIT =================
bool AIConnector::Init(const std::string&) {
    return LoadConfig("") && (InitModelMappings(), true);
}

// ================= CONFIG =================
bool AIConnector::LoadConfig(const std::string&) {
    m_config.max_retries = 3;
    m_config.timeout_seconds = 30;

    m_providers[Provider::OPENROUTER] =
        {"API_KEY", "https://openrouter.ai/api/v1", true};

    return true;
}

// ================= MODEL MAP =================
void AIConnector::InitModelMappings() {
    m_model_providers[Model::OR_GPT4O] = Provider::OPENROUTER;
    m_model_ids[Model::OR_GPT4O] = "gpt-4o-mini";
}

// ================= MAIN CALL =================
AIResponse AIConnector::Call(const AIRequest& req) {
    AIResponse res;

    if (req.model == Model::UNKNOWN) {
        res.error = "Unknown model";
        return res;
    }

    auto it = m_model_providers.find(req.model);
    if (it == m_model_providers.end()) {
        res.error = "Model not mapped";
        return res;
    }

    return CallWithRetry(req, it->second);
}

// ================= RETRY =================
AIResponse AIConnector::CallWithRetry(
    const AIRequest& req, Provider provider)
{
    AIResponse res;

    for (int i = 0; i < m_config.max_retries; i++) {
        res = DispatchCall(req, provider);
        res.retry_count = i;

        if (res.success) return res;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(500 * (i + 1)));
    }

    return res;
}

// ================= DISPATCH =================
AIResponse AIConnector::DispatchCall(
    const AIRequest& req, Provider provider)
{
    auto& cfg = m_providers[provider];

    if (!cfg.enabled) {
        return {false, "", "Provider disabled"};
    }

    if (IsRateLimited(provider)) {
        return {false, "", "Rate limited"};
    }

    IncrementCount(provider);

    switch (provider) {
        case Provider::OPENROUTER:
            return CallOpenAICompat(req, cfg);
        default:
            return {false, "", "Unsupported provider"};
    }
}

// ================= OPENAI-COMPAT =================
AIResponse AIConnector::CallOpenAICompat(
    const AIRequest& req, const ProviderConfig& cfg)
{
    AIResponse response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "CURL init failed";
        return response;
    }

    std::string buffer;

    json payload = {
        {"model", "gpt-4o-mini"},
        {"messages", {
            {{"role","user"},{"content",req.prompt}}
        }}
    };

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers,
        ("Authorization: Bearer " + cfg.api_key).c_str());
    headers = curl_slist_append(headers,
        "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,
        (cfg.base_url + "/chat/completions").c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,
        payload.dump().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
        WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,
        m_config.timeout_seconds);

    auto start = std::chrono::high_resolution_clock::now();

    CURLcode res = curl_easy_perform(curl);

    auto end = std::chrono::high_resolution_clock::now();
    response.latency_ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        try {
            json j = json::parse(buffer);

            if (j.contains("choices") &&
                j["choices"].is_array() &&
                !j["choices"].empty()) {

                response.text =
                    j["choices"][0]["message"]["content"];
                response.success = true;
            } else {
                response.error = "Invalid response format";
            }
        } catch (...) {
            response.error = "JSON parse failed";
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

// ================= ASYNC =================
void AIConnector::CallAsync(
    const AIRequest& req,
    std::function<void(AIResponse)> cb)
{
    std::thread t([=]() {
        AIResponse res = Call(req);
        if (cb) cb(res);
    });

    std::lock_guard<std::mutex> lock(m_thread_mutex);
    m_threads.push_back(std::move(t));
}

// ================= WAIT =================
void AIConnector::WaitAllAsync() {
    std::lock_guard<std::mutex> lock(m_thread_mutex);

    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
    m_threads.clear();
}

// ================= HELPERS =================
bool AIConnector::IsRateLimited(Provider p) {
    return m_providers[p].calls_made >= m_providers[p].rate_limit;
}

void AIConnector::IncrementCount(Provider p) {
    m_providers[p].calls_made++;
}

std::string AIConnector::ProviderName(Provider p) const {
    switch (p) {
        case Provider::OPENROUTER: return "OpenRouter";
        default: return "Unknown";
    }
}
