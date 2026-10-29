#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/oops-all-vampires/OopsAllVampiresConfig.h"

#include <ship/Context.h>

// ============================================================================
// Oops! All Vampires? — difficulty tuning loaded from oops-all-vampires.json.
// ============================================================================

namespace OAV {

namespace {

constexpr const char* CONFIG_FILE = "oops-all-vampires.json";

OavTuning sTuning;
bool sLoaded = false;

// The tuning file always lives next to the game executable (the zip root on
// Windows), regardless of the working directory the game was launched from.
// The stock app-directory helper resolves against the cwd on portable
// builds, which can differ from the exe's folder.
std::string TuningFilePath() {
    // Test isolation: headless soak instances can point at their own file.
    if (const char* override = getenv("OAV_TUNING_PATH")) {
        return std::string(override);
    }
#if defined(_WIN32)
    wchar_t wbuf[4096];
    DWORD len = GetModuleFileNameW(nullptr, wbuf, 4096);
    if (len > 0 && len < 4096) {
        std::error_code ec;
        std::filesystem::path dir = std::filesystem::path(std::wstring(wbuf, len)).parent_path();
        std::string out = (dir / CONFIG_FILE).string();
        if (!ec) {
            return out;
        }
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::filesystem::path dir = std::filesystem::path(buf).parent_path();
        return (dir / CONFIG_FILE).string();
    }
#endif
    // Fallback: app directory (cwd on portable builds).
    return Ship::Context::GetPathRelativeToAppDirectory(CONFIG_FILE);
}

// Default template written to the game folder on first launch so the file can
// be discovered and edited without reading the source.
const char* TEMPLATE = R"JSON({
    "runLengthSeconds": 1800,

    "xp": { "base": 25, "perLevel": 25 },

    "spawn": {
        "intervalStart": 22,
        "intervalRamp": 2400,
        "intervalMin": 10,
        "batchStart": 2,
        "batchRamp": 7000,
        "batchMax": 6,
        "cap": 35,
        "ringRadius": 430,
        "despawnDistance": 3500
    },

    "elite": { "startSeconds": 240, "intervalSeconds": 45, "hardCount": 2 },

    "finale": { "startSeconds": 1500, "eliteIntervalSeconds": 15 },

    "wave": { "secondsPerTier": 300, "maxTier": 7 },

    "rosterPhaseSeconds": [0, 60, 120, 240, 360, 480, 720]
}
)JSON";

template <typename T>
void ReadKey(const nlohmann::json& json, const char* key, T& out, T min, T max) {
    if (!json.contains(key)) {
        return;
    }
    try {
        T value = json.at(key).get<T>();
        if (value < min) {
            value = min;
        }
        if (value > max) {
            value = max;
        }
        out = value;
    } catch (const std::exception& e) {
        SPDLOG_WARN("[OAV] tuning: bad value for '{}' ({}), keeping default", key, e.what());
    }
}

void ReadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.good()) {
        // First launch: write the template so it can be edited.
        std::ofstream out(path);
        if (out.good()) {
            out << TEMPLATE;
            SPDLOG_INFO("[OAV] tuning: wrote default template to {}", path);
        }
        return;
    }

    try {
        nlohmann::json json = nlohmann::json::parse(file);

        ReadKey<uint32_t>(json, "runLengthSeconds", sTuning.runLengthSeconds, 30, 24 * 60 * 60);

        if (json.contains("xp")) {
            const auto& xp = json.at("xp");
            ReadKey<uint32_t>(xp, "base", sTuning.xpBase, 1, 10000);
            ReadKey<uint32_t>(xp, "perLevel", sTuning.xpPerLevel, 0, 10000);
        }

        if (json.contains("spawn")) {
            const auto& spawn = json.at("spawn");
            ReadKey<uint32_t>(spawn, "intervalStart", sTuning.spawnIntervalStart, 3, 200);
            ReadKey<uint32_t>(spawn, "intervalRamp", sTuning.spawnIntervalRamp, 1, 1000000);
            ReadKey<uint32_t>(spawn, "intervalMin", sTuning.spawnIntervalMin, 3, 200);
            ReadKey<uint32_t>(spawn, "batchStart", sTuning.spawnBatchStart, 1, 50);
            ReadKey<uint32_t>(spawn, "batchRamp", sTuning.spawnBatchRamp, 1, 1000000);
            ReadKey<uint32_t>(spawn, "batchMax", sTuning.spawnBatchMax, 1, 50);
            ReadKey<uint32_t>(spawn, "cap", sTuning.spawnCap, 5, 200);
            ReadKey<float>(spawn, "ringRadius", sTuning.spawnRingRadius, 200.0f, 1000.0f);
            ReadKey<float>(spawn, "despawnDistance", sTuning.despawnDistance, 1000.0f, 10000.0f);
        }

        if (json.contains("elite")) {
            const auto& elite = json.at("elite");
            ReadKey<uint32_t>(elite, "startSeconds", sTuning.eliteStartSeconds, 0, 24 * 60 * 60);
            ReadKey<uint32_t>(elite, "intervalSeconds", sTuning.eliteIntervalSeconds, 5, 3600);
            ReadKey<uint32_t>(elite, "hardCount", sTuning.eliteHardCount, 1, 10);
        }

        if (json.contains("finale")) {
            const auto& finale = json.at("finale");
            ReadKey<uint32_t>(finale, "startSeconds", sTuning.finaleStartSeconds, 0, 24 * 60 * 60);
            ReadKey<uint32_t>(finale, "eliteIntervalSeconds", sTuning.finaleEliteIntervalSeconds, 5,
                              3600);
        }

        if (json.contains("wave")) {
            const auto& wave = json.at("wave");
            ReadKey<uint32_t>(wave, "secondsPerTier", sTuning.waveSecondsPerTier, 30, 3600);
            ReadKey<uint32_t>(wave, "maxTier", sTuning.waveMaxTier, 1, 99);
        }

        if (json.contains("rosterPhaseSeconds")) {
            const auto& phases = json.at("rosterPhaseSeconds");
            if (phases.is_array() && phases.size() == 7) {
                constexpr uint32_t kMaxPhase = 24 * 60 * 60;
                for (size_t i = 0; i < 7; i++) {
                    if (!phases[i].is_number()) {
                        continue;
                    }
                    uint32_t value = phases[i].get<uint32_t>();
                    if (value > kMaxPhase) {
                        value = kMaxPhase;
                    }
                    // Phases must unlock in order: never earlier than the
                    // previous tier.
                    if (i > 0 && value < sTuning.phaseSeconds[i - 1]) {
                        value = sTuning.phaseSeconds[i - 1];
                    }
                    sTuning.phaseSeconds[i] = value;
                }
            } else {
                SPDLOG_WARN("[OAV] tuning: rosterPhaseSeconds must be an array of 7 seconds, keeping defaults");
            }
        }

        SPDLOG_INFO("[OAV] tuning: loaded {}", path);
    } catch (const std::exception& e) {
        SPDLOG_WARN("[OAV] tuning: failed to parse {} ({}), keeping defaults", path, e.what());
    }
}

} // namespace

void ReloadTuning() {
    sTuning = OavTuning{}; // reset to defaults, then merge the file over them
    std::string path = TuningFilePath();
    ReadFile(path);
    sLoaded = true;
}

const OavTuning& GetTuning() {
    if (!sLoaded) {
        ReloadTuning();
    }
    return sTuning;
}

} // namespace OAV
