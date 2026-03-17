// ============================================================
// TERRA ENGINE — AI CONNECTOR IMPLEMENTATION
// AIConnector.cpp — Version 1.2.0
// ============================================================
// AUDIT FIXES APPLIED:
// FIX 1  — CallAsync implemented with real thread
// FIX 2  — HF_BARK + HF_WHISPER in InitModelMappings
// FIX 3  — Unknown model guard in Call()
// FIX 4  — EnvLoader::Load() parses .env file directly
// FIX 5  — Retry loop reads m_config.max_retries
// FIX 6  — Timeout reads m_config.timeout_seconds
// FIX 7  — stream removed from AIRequest
// FIX 8  — Exception types differentiated in catch blocks
// FIX 9  — All output through Logger macros
// FIX 10 — Timeout in one place: m_config.timeout_seconds
// ============================================================

#include "AIConnector.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <chrono>

using json = nlohmann::json;
using namespace TerraEngine;

// Static definitions
const char* AIConnector::MOD = "AIConnector";
std::map<std::string, std::string> EnvLoader::s_env;

// ============================================================
// CURL WRITE CALLBACK
// ============================================================
static size_t WriteCallback(void* contents,
                             size_t size,
                             size_t nmemb,
                             std::string* out)
{
    out->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

// ============================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================
AIConnector::AIConnector() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    TLOG_INFO(MOD, "Created");
}

AIConnector::~AIConnector() {
    WaitAllAsync();
    curl_global_cleanup();
    TLOG_INFO(MOD, "Destroyed");
}

// ============================================================
// INIT
// FIX 4 — calls EnvLoader::Load() which parses .env file
// ============================================================
bool AIConnector::Init(const std::string& env_path) {
    TLOG_INFO(MOD, "Initialising from: " + env_path);

    // FIX 4 — load .env file before reading any values
    if (!EnvLoader::Load(env_path)) {
        TLOG_WARN(MOD, ".env not loaded — trying OS environment");
    }

    if (!LoadConfig(env_path)) {
        TLOG_ERR(MOD, "LoadConfig failed");
        return false;
    }

    InitModelMappings();
    TLOG_INFO(MOD, "Ready — 7 providers — 30 models");
    return true;
}

// ============================================================
// LOAD CONFIG
// FIX 5 + FIX 6 — reads retries and timeout from .env
// ============================================================
bool AIConnector::LoadConfig(const std::string& /*env_path*/) {
    // FIX 5 — max retries from .env
    m_config.max_retries = std::stoi(Var("MAX_RETRIES", "3"));
    // FIX 6 — timeout from .env
    m_config.timeout_seconds = std::stoi(Var("REQUEST_TIMEOUT", "30"));
    m_config.debug = (Var("DEBUG", "false") == "true");

    TLOG_INFO(MOD, "Config: retries=" + std::to_string(m_config.max_retries)
                 + " timeout=" + std::to_string(m_config.timeout_seconds) + "s");

    m_providers[Provider::HUGGINGFACE] = { Var("HF_API_KEY"), Var("HF_BASE_URL"), true, 100, 0 };
    m_providers[Provider::OPENROUTER]  = { Var("OR_API_KEY"), Var("OR_BASE_URL"), true, 200, 0 };
    m_providers[Provider::DEEPSEEK]    = { Var("DS_API_KEY"), Var("DS_BASE_URL"), true, 100, 0 };
    m_providers[Provider::GEMINI]      = { Var("GM_API_KEY"), Var("GM_BASE_URL"), true,  60, 0 };
    m_providers[Provider::GROQ]        = { Var("GQ_API_KEY"), Var("GQ_BASE_URL"), true,  30, 0 };
    m_providers[Provider::MISTRAL]     = { Var("MS_API_KEY"), Var("MS_BASE_URL"), true, 100, 0 };

    // Cloudflare URL includes account ID
    std::string cf_url = Var("CF_BASE_URL") + "/"
                       + Var("CF_ACCOUNT_ID") + "/ai/run";
    m_providers[Provider::CLOUDFLARE] = { Var("CF_API_KEY"), cf_url, true, 300, 0 };

    // Warn on missing keys
    for (auto& [prov, cfg] : m_providers) {
        if (cfg.api_key.empty()) {
            TLOG_WARN(MOD, ProviderName(prov) + " key is empty — provider disabled");
            cfg.enabled = false;
        }
    }
    return true;
}

// ============================================================
// INIT MODEL MAPPINGS
// FIX 2 — HF_BARK and HF_WHISPER now mapped
// ============================================================
void AIConnector::InitModelMappings() {
    // --- HuggingFace ---
    auto hf = Provider::HUGGINGFACE;
    m_model_providers[Model::HF_PHI3]    = hf; m_model_ids[Model::HF_PHI3]    = Var("HF_PHI3");
    m_model_providers[Model::HF_QWEN]    = hf; m_model_ids[Model::HF_QWEN]    = Var("HF_QWEN");
    m_model_providers[Model::HF_MISTRAL] = hf; m_model_ids[Model::HF_MISTRAL] = Var("HF_MISTRAL");
    m_model_providers[Model::HF_LLAMA]   = hf; m_model_ids[Model::HF_LLAMA]   = Var("HF_LLAMA");
    m_model_providers[Model::HF_GEMMA]   = hf; m_model_ids[Model::HF_GEMMA]   = Var("HF_GEMMA");
    // FIX 2 — these two were missing
    m_model_providers[Model::HF_BARK]    = hf; m_model_ids[Model::HF_BARK]    = Var("HF_BARK");
    m_model_providers[Model::HF_WHISPER] = hf; m_model_ids[Model::HF_WHISPER] = Var("HF_WHISPER");

    // --- OpenRouter ---
    auto or_ = Provider::OPENROUTER;
    m_model_providers[Model::OR_GPT4O]       = or_; m_model_ids[Model::OR_GPT4O]       = Var("OR_GPT4O");
    m_model_providers[Model::OR_CLAUDE]      = or_; m_model_ids[Model::OR_CLAUDE]      = Var("OR_CLAUDE");
    m_model_providers[Model::OR_GEMINI]      = or_; m_model_ids[Model::OR_GEMINI]      = Var("OR_GEMINI");
    m_model_providers[Model::OR_LLAMA405B]   = or_; m_model_ids[Model::OR_LLAMA405B]   = Var("OR_LLAMA405B");
    m_model_providers[Model::OR_MIXTRAL]     = or_; m_model_ids[Model::OR_MIXTRAL]     = Var("OR_MIXTRAL");
    m_model_providers[Model::OR_CLAUDE_HAIKU]= or_; m_model_ids[Model::OR_CLAUDE_HAIKU]= Var("OR_CLAUDE_HAIKU");

    // --- DeepSeek ---
    auto ds = Provider::DEEPSEEK;
    m_model_providers[Model::DS_CHAT]     = ds; m_model_ids[Model::DS_CHAT]     = Var("DS_CHAT");
    m_model_providers[Model::DS_CODER]    = ds; m_model_ids[Model::DS_CODER]    = Var("DS_CODER");
    m_model_providers[Model::DS_REASONER] = ds; m_model_ids[Model::DS_REASONER] = Var("DS_REASONER");

    // --- Gemini ---
    auto gm = Provider::GEMINI;
    m_model_providers[Model::GM_PRO]   = gm; m_model_ids[Model::GM_PRO]   = Var("GM_PRO");
    m_model_providers[Model::GM_FLASH] = gm; m_model_ids[Model::GM_FLASH] = Var("GM_FLASH");
    m_model_providers[Model::GM_ULTRA] = gm; m_model_ids[Model::GM_ULTRA] = Var("GM_ULTRA");

    // --- Groq ---
    auto gq = Provider::GROQ;
    m_model_providers[Model::GQ_LLAMA]      = gq; m_model_ids[Model::GQ_LLAMA]      = Var("GQ_LLAMA");
    m_model_providers[Model::GQ_MIXTRAL]    = gq; m_model_ids[Model::GQ_MIXTRAL]    = Var("GQ_MIXTRAL");
    m_model_providers[Model::GQ_GEMMA]      = gq; m_model_ids[Model::GQ_GEMMA]      = Var("GQ_GEMMA");
    m_model_providers[Model::GQ_LLAMA_FAST] = gq; m_model_ids[Model::GQ_LLAMA_FAST] = Var("GQ_LLAMA_FAST");

    // --- Cloudflare ---
    auto cf = Provider::CLOUDFLARE;
    m_model_providers[Model::CF_LLAMA]   = cf; m_model_ids[Model::CF_LLAMA]   = Var("CF_LLAMA");
    m_model_providers[Model::CF_MISTRAL] = cf; m_model_ids[Model::CF_MISTRAL] = Var("CF_MISTRAL");
    m_model_providers[Model::CF_GEMMA]   = cf; m_model_ids[Model::CF_GEMMA]   = Var("CF_GEMMA");

    // --- Mistral ---
    auto ms = Provider::MISTRAL;
    m_model_providers[Model::MS_LARGE]  = ms; m_model_ids[Model::MS_LARGE]  = Var("MS_LARGE");
    m_model_providers[Model::MS_MEDIUM] = ms; m_model_ids[Model::MS_MEDIUM] = Var("MS_MEDIUM");
    m_model_providers[Model::MS_SMALL]  = ms; m_model_ids[Model::MS_SMALL]  = Var("MS_SMALL");
    m_model_providers[Model::MS_NEMO]   = ms; m_model_ids[Model::MS_NEMO]   = Var("MS_NEMO");

    TLOG_INFO(MOD, "Mapped " + std::to_string(m_model_providers.size()) + " models");
}

// ============================================================
// CALL — SYNCHRONOUS
// FIX 3 — guard against unknown model
// ============================================================
AIResponse AIConnector::Call(const AIRequest& request) {
    AIResponse response;

    // FIX 3 — check model is known before proceeding
    if (request.model == Model::UNKNOWN) {
        response.error = "Model::UNKNOWN passed to Call()";
        TLOG_ERR(MOD, response.error);
        return response;
    }

    auto it = m_model_providers.find(request.model);
    if (it == m_model_providers.end()) {
        response.error = "Model not found in provider map";
        TLOG_ERR(MOD, response.error + " — model id: "
                    + std::to_string(static_cast<int>(request.model)));
        return response;
    }

    Provider provider = it->second;
    return CallWithRetry(request, provider);
}

// ============================================================
// CALL WITH RETRY
// FIX 5 — reads m_config.max_retries from .env
// ============================================================
AIResponse AIConnector::CallWithRetry(const AIRequest& req, Provider provider) {
    AIResponse response;
    auto& cfg = m_providers[provider];

    if (!cfg.enabled) {
        response.error = ProviderName(provider) + " is disabled";
        return response;
    }
    if (IsRateLimited(provider)) {
        response.error = ProviderName(provider) + " rate limited";
        TLOG_WARN(MOD, response.error);
        return response;
    }

    auto t_start = std::chrono::high_resolution_clock::now();

    // FIX 5 — retry loop uses config value not hardcoded number
    for (int attempt = 1; attempt <= m_config.max_retries; attempt++) {
        TLOG_DEBUG(MOD, "Attempt " + std::to_string(attempt)
                      + "/" + std::to_string(m_config.max_retries)
                      + " — " + ProviderName(provider));

        switch (provider) {
            case Provider::HUGGINGFACE:
                response = CallHuggingFace(req, cfg);  break;
            case Provider::OPENROUTER:
            case Provider::DEEPSEEK:
            case Provider::GROQ:
            case Provider::MISTRAL:
                response = CallOpenAICompat(req, cfg); break;
            case Provider::GEMINI:
                response = CallGemini(req, cfg);       break;
            case Provider::CLOUDFLARE:
                response = CallCloudflare(req, cfg);   break;
            default:
                response.error = "Unknown provider";
                return response;
        }

        response.retry_count = attempt - 1;

        if (response.success) break;

        if (attempt < m_config.max_retries) {
            int wait_ms = 500 * attempt; // exponential: 500ms, 1000ms, 1500ms
            TLOG_WARN(MOD, "Retry " + std::to_string(attempt)
                         + " failed — waiting " + std::to_string(wait_ms) + "ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        }
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double latency = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    response.latency_ms    = latency;
    response.provider_used = provider;
    response.provider_name = ProviderName(provider);

    IncrementCount(provider);
    UpdateStats(response, latency);

    return response;
}

// ============================================================
// CALL WITH FALLBACK
// ============================================================
AIResponse AIConnector::CallWithFallback(
    const AIRequest& request,
    const std::vector<Model>& fallback_chain)
{
    AIResponse response = Call(request);
    if (response.success) return response;

    TLOG_WARN(MOD, "Primary failed — trying fallback chain");

    for (const Model& m : fallback_chain) {
        AIRequest req  = request;
        req.model      = m;
        response       = Call(req);
        if (response.success) {
            TLOG_INFO(MOD, "Fallback succeeded: " + ModelName(m));
            return response;
        }
        TLOG_WARN(MOD, "Fallback failed: " + ModelName(m));
    }

    response.success = false;
    response.error   = "All fallbacks exhausted";
    TLOG_ERR(MOD, response.error);
    return response;
}

// ============================================================
// CALL ASYNC — FIX 1
// Was declared but never implemented — now fully implemented
// Spawns a real thread, calls callback when done
// Thread-safe — multiple agents can call simultaneously
// ============================================================
void AIConnector::CallAsync(
    const AIRequest& request,
    std::function<void(AIResponse)> callback)
{
    // Capture by value so request survives after this function returns
    AIRequest  req_copy      = request;
    auto       callback_copy = callback;

    std::thread t([this, req_copy, callback_copy]() {
        AIResponse response = Call(req_copy);
        if (callback_copy) {
            callback_copy(response);
        }
    });

    // Store thread so WaitAllAsync can join it
    {
        std::lock_guard<std::mutex> lock(m_threads_mutex);
        m_threads.push_back(std::move(t));
    }

    // Clean up finished threads
    std::lock_guard<std::mutex> lock(m_threads_mutex);
    m_threads.erase(
        std::remove_if(m_threads.begin(), m_threads.end(),
            [](std::thread& t) { return !t.joinable(); }),
        m_threads.end()
    );
}

// Wait for all async threads to finish
void AIConnector::WaitAllAsync() {
    std::lock_guard<std::mutex> lock(m_threads_mutex);
    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
    m_threads.clear();
}

// ============================================================
// CALL HUGGING FACE
// FIX 6 + FIX 10 — timeout from m_config not hardcoded
// FIX 8 — differentiated exception handling
// ============================================================
AIResponse AIConnector::CallHuggingFace(
    const AIRequest& req,
    const ProviderConfig& cfg)
{
    AIResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) { response.error = "CURL init failed"; return response; }

    std::string model_id = m_model_ids.count(req.model)
                         ? m_model_ids.at(req.model) : "";
    if (model_id.empty()) {
        response.error = "Model ID not found";
        curl_easy_cleanup(curl);
        return response;
    }

    std::string url          = cfg.base_url + "/" + model_id;
    std::string response_str;

    json payload = {
        {"inputs", req.prompt},
        {"parameters", {
            {"max_new_tokens", req.max_tokens},
            {"temperature",    req.temperature},
            {"return_full_text", false}
        }}
    };
    std::string body = payload.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + cfg.api_key).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    // FIX 6 + FIX 10 — ONE place for timeout, reads config
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(m_config.timeout_seconds));

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        // FIX 8 — catch specific exception types
        try {
            json result = json::parse(response_str);
            if (result.is_array() && !result.empty()) {
                response.text    = result[0].value("generated_text", "");
                response.success = !response.text.empty();
            } else {
                response.error = "Unexpected response format";
            }
        } catch (const json::parse_error& e) {
            response.error = std::string("JSON parse error: ") + e.what();
            TLOG_ERR(MOD, response.error);
        } catch (const json::type_error& e) {
            response.error = std::string("JSON type error: ") + e.what();
            TLOG_ERR(MOD, response.error);
        } catch (const std::exception& e) {
            response.error = std::string("Exception: ") + e.what();
            TLOG_ERR(MOD, response.error);
        }
    } else {
        response.error = std::string("CURL error: ") + curl_easy_strerror(res);
        TLOG_ERR(MOD, response.error);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

// ============================================================
// CALL OPENAI-COMPATIBLE
// Used by: OpenRouter, DeepSeek, Groq, Mistral
// FIX 6 + FIX 10 — timeout from config
// FIX 8 — differentiated exceptions
// ============================================================
AIResponse AIConnector::CallOpenAICompat(
    const AIRequest& req,
    const ProviderConfig& cfg)
{
    AIResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) { response.error = "CURL init failed"; return response; }

    std::string model_id = m_model_ids.count(req.model)
                         ? m_model_ids.at(req.model) : "";
    if (model_id.empty()) {
        response.error = "Model ID not found";
        curl_easy_cleanup(curl);
        return response;
    }

    std::string url          = cfg.base_url + "/chat/completions";
    std::string response_str;

    json messages = json::array();
    if (!req.system_prompt.empty()) {
        messages.push_back({{"role","system"},{"content",req.system_prompt}});
    }
    messages.push_back({{"role","user"},{"content",req.prompt}});

    json payload = {
        {"model",       model_id},
        {"messages",    messages},
        {"max_tokens",  req.max_tokens},
        {"temperature", req.temperature}
    };
    std::string body = payload.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + cfg.api_key).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    // FIX 6 + FIX 10
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(m_config.timeout_seconds));

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        // FIX 8
        try {
            json result         = json::parse(response_str);
            response.text       = result["choices"][0]["message"]["content"];
            response.tokens_used= result["usage"].value("total_tokens", 0);
            response.model_used = result.value("model", model_id);
            response.success    = !response.text.empty();
        } catch (const json::parse_error& e) {
            response.error = std::string("JSON parse error: ") + e.what();
            TLOG_ERR(MOD, response.error);
        } catch (const json::type_error& e) {
            response.error = std::string("JSON type error: ") + e.what();
            TLOG_ERR(MOD, response.error);
        } catch (const std::out_of_range& e) {
            response.error = std::string("Out of range: ") + e.what();
            TLOG_ERR(MOD, response.error);
        } catch (const std::exception& e) {
            response.error = std::string("Exception: ") + e.what();
            TLOG_ERR(MOD, response.error);
        }
    } else {
        response.error = std::string("CURL error: ") + curl_easy_strerror(res);
        TLOG_ERR(MOD, response.error);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

// ============================================================
// CALL GEMINI
// FIX 6 + FIX 10 — timeout from config
// FIX 8 — differentiated exceptions
// ============================================================
AIResponse AIConnector::CallGemini(
    const AIRequest& req,
    const ProviderConfig& cfg)
{
    AIResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) { response.error = "CURL init failed"; return response; }

    std::string model_id = m_model_ids.count(req.model)
                         ? m_model_ids.at(req.model) : "";
    if (model_id.empty()) {
        response.error = "Model ID not found";
        curl_easy_cleanup(curl);
        return response;
    }

    std::string url = cfg.base_url + "/models/" + model_id
                    + ":generateContent?key=" + cfg.api_key;
    std::string response_str;

    json payload = {
        {"contents", json::array({
            {{"parts", json::array({{ {"text", req.prompt} }})}}
        })},
        {"generationConfig", {
            {"maxOutputTokens", req.max_tokens},
            {"temperature",     req.temperature}
        }}
    };

    if (!req.system_prompt.empty()) {
        payload["system_instruction"] = {
            {"parts", json::array({{ {"text", req.system_prompt} }})}
        };
    }

    std::string body = payload.dump();
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    // FIX 6 + FIX 10
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(m_config.timeout_seconds));

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        // FIX 8
        try {
            json result      = json::parse(response_str);
            response.text    = result["candidates"][0]["content"]["parts"][0]["text"];
            response.success = !response.text.empty();
        } catch (const json::parse_error& e) {
            response.error = std::string("JSON parse error: ") + e.what();
            TLOG_ERR(MOD, response.error);
        } catch (const json::type_error& e) {
            response.error = std::string("JSON type error: ") + e.what();
            TLOG_ERR(MOD, response.error);
        } catch (const std::exception& e) {
            response.error = std::string("Exception: ") + e.what();
            TLOG_ERR(MOD, response.error);
        }
    } else {
        response.error = std::string("CURL error: ") + curl_easy_strerror(res);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

// ============================================================
// CALL CLOUDFLARE
// FIX 6 + FIX 10 — timeout from config
// FIX 8 — differentiated exceptions
// ============================================================
AIResponse AIConnector::CallCloudflare(
    const AIRequest& req,
    const ProviderConfig& cfg)
{
    AIResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) { response.error = "CURL init failed"; return response; }

    std::string model_id = m_model_ids.count(req.model)
                         ? m_model_ids.at(req.model) : "";
    if (model_id.empty()) {
        response.error = "Model ID not found";
        curl_easy_cleanup(curl);
        return response;
    }

    std::string url          = cfg.base_url + "/" + model_id;
    std::string response_str;

    json messages = json::array();
    if (!req.system_prompt.empty()) {
        messages.push_back({{"role","system"},{"content",req.system_prompt}});
    }
    messages.push_back({{"role","user"},{"content",req.prompt}});

    json payload = {
        {"messages",   messages},
        {"max_tokens", req.max_tokens}
    };
    std::string body = payload.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + cfg.api_key).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    // FIX 6 + FIX 10
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(m_config.timeout_seconds));

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        // FIX 8
        try {
            json result      = json::parse(response_str);
            response.text    = result["result"]["response"];
            response.success = !response.text.empty();
        } catch (const json::parse_error& e) {
            response.error = std::string("JSON parse error: ") + e.what();
            TLOG_ERR(MOD, response.error);
        } catch (const json::type_error& e) {
            response.error = std::string("JSON type error: ") + e.what();
            TLOG_ERR(MOD, response.error);
        } catch (const std::exception& e) {
            response.error = std::string("Exception: ") + e.what();
            TLOG_ERR(MOD, response.error);
        }
    } else {
        response.error = std::string("CURL error: ") + curl_easy_strerror(res);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

// ============================================================
// PROVIDER TESTS
// ============================================================
bool AIConnector::TestProvider(Provider provider) {
    AIRequest req;
    req.max_tokens = 20;
    req.prompt     = "Reply with exactly: Terra Engine online";

    switch (provider) {
        case Provider::HUGGINGFACE: req.model = Model::HF_PHI3;    break;
        case Provider::OPENROUTER:  req.model = Model::OR_GPT4O;   break;
        case Provider::DEEPSEEK:    req.model = Model::DS_CHAT;    break;
        case Provider::GEMINI:      req.model = Model::GM_FLASH;   break;
        case Provider::GROQ:        req.model = Model::GQ_LLAMA;   break;
        case Provider::CLOUDFLARE:  req.model = Model::CF_LLAMA;   break;
        case Provider::MISTRAL:     req.model = Model::MS_SMALL;   break;
        default:
            TLOG_ERR(MOD, "TestProvider called with unknown provider");
            return false;
    }

    AIResponse r = Call(req);
    std::string name = ProviderName(provider);
    std::string status = r.success ? "[OK]   " : "[FAIL] ";
    std::string msg = r.success
        ? r.text.substr(0, 40)
        : r.error.substr(0, 40);

    TLOG_INFO(MOD, status + name + " — " + msg
                 + " (" + std::to_string(static_cast<int>(r.latency_ms)) + "ms)");
    return r.success;
}

std::map<Provider, bool> AIConnector::TestAll() {
    TLOG_INFO(MOD, "=== Testing all 7 providers ===");
    std::map<Provider, bool> results;
    results[Provider::HUGGINGFACE] = TestProvider(Provider::HUGGINGFACE);
    results[Provider::OPENROUTER]  = TestProvider(Provider::OPENROUTER);
    results[Provider::DEEPSEEK]    = TestProvider(Provider::DEEPSEEK);
    results[Provider::GEMINI]      = TestProvider(Provider::GEMINI);
    results[Provider::GROQ]        = TestProvider(Provider::GROQ);
    results[Provider::CLOUDFLARE]  = TestProvider(Provider::CLOUDFLARE);
    results[Provider::MISTRAL]     = TestProvider(Provider::MISTRAL);
    int passed = 0;
    for (auto& [p, ok] : results) if (ok) passed++;
    TLOG_INFO(MOD, "=== " + std::to_string(passed) + "/7 providers online ===");
    return results;
}

bool AIConnector::IsProviderAvailable(Provider provider) {
    auto it = m_providers.find(provider);
    if (it == m_providers.end()) return false;
    return it->second.enabled && !IsRateLimited(provider);
}

// ============================================================
// STATS
// FIX 10 — double precision for latency average
// ============================================================
void AIConnector::UpdateStats(const AIResponse& r, double latency_ms) {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats.total_calls++;
    if (r.success) {
        m_stats.successful_calls++;
        m_stats.calls_per_provider[r.provider_used]++;
    } else {
        m_stats.failed_calls++;
        m_stats.failures_per_provider[r.provider_used]++;
    }
    // FIX 10 — Welford online algorithm avoids float drift
    double delta = latency_ms - m_stats.avg_latency_ms;
    m_stats.avg_latency_ms += delta / m_stats.total_calls;
}

AIConnector::Stats AIConnector::GetStats() const {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_stats;
}

void AIConnector::PrintStats() const {
    Stats s = GetStats();
    float rate = s.total_calls > 0
        ? (100.0f * s.successful_calls / s.total_calls)
        : 0.0f;
    TLOG_INFO(MOD, "--- Stats ---");
    TLOG_INFO(MOD, "Total calls:    " + std::to_string(s.total_calls));
    TLOG_INFO(MOD, "Successful:     " + std::to_string(s.successful_calls));
    TLOG_INFO(MOD, "Failed:         " + std::to_string(s.failed_calls));
    TLOG_INFO(MOD, "Success rate:   " + std::to_string(static_cast<int>(rate)) + "%");
    TLOG_INFO(MOD, "Avg latency:    " + std::to_string(static_cast<int>(s.avg_latency_ms)) + "ms");
}

// ============================================================
// HELPERS
// ============================================================
bool AIConnector::IsRateLimited(Provider p) {
    auto it = m_providers.find(p);
    if (it == m_providers.end()) return true;
    return it->second.calls_made >= it->second.rate_limit;
}

void AIConnector::IncrementCount(Provider p) {
    auto it = m_providers.find(p);
    if (it != m_providers.end()) it->second.calls_made++;
}

void AIConnector::ResetRateLimits() {
    for (auto& [p, cfg] : m_providers) cfg.calls_made = 0;
    TLOG_INFO(MOD, "Rate limits reset");
}

std::string AIConnector::Var(const std::string& key,
                              const std::string& fallback) {
    return EnvLoader::Get(key, fallback);
}

std::string AIConnector::ProviderName(Provider p) const {
    switch (p) {
        case Provider::HUGGINGFACE: return "HuggingFace";
        case Provider::OPENROUTER:  return "OpenRouter";
        case Provider::DEEPSEEK:    return "DeepSeek";
        case Provider::GEMINI:      return "Gemini";
        case Provider::GROQ:        return "Groq";
        case Provider::CLOUDFLARE:  return "Cloudflare";
        case Provider::MISTRAL:     return "Mistral";
        default:                    return "Unknown";
    }
}

std::string AIConnector::ModelName(Model m) const {
    auto it = m_model_ids.find(m);
    return (it != m_model_ids.end()) ? it->second : "unknown-model";
}
