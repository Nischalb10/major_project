#pragma once

#include <string>

namespace nr::error_model
{

/**
 * @brief Realistic 5G Propagation Scenarios based on empirical wireless channel measurements.
 */
enum class GEScenario {
    URBAN_HIGH_DENSE,   // Scenario 1: High co-channel interference (>1000 UEs/km^2)
    URBAN_MEDIUM_DENSE, // Scenario 2: Moderate interference & building shadowing (~500 UEs/km^2)
    URBAN_MILD_DENSE,   // Scenario 3: Vehicular Doppler & fast multipath fading (~100 UEs/km^2)
    RURAL_HIGH_DENSE,   // Scenario 4: Stationary IoT, clear line-of-sight (~50 UEs/km^2)
    RURAL_MEDIUM_DENSE, // Scenario 5: Agricultural IoT, sparse base station coverage (~20 UEs/km^2)
    RURAL_LOW_DENSE,    // Scenario 6: Extreme cell-edge range, terrain blockage (<5 UEs/km^2)
    BASELINE_CLEAN,     // Scenario 7: Ideal wired/fiber control link (0% error loss)
    CUSTOM_SCENARIO     // Scenario 8: Custom user-defined p and q values (e.g. 99% packet drop)
};

struct ScenarioProfile {
    std::string id;
    std::string name;
    std::string description;
    double p; // Transition probability: Good -> Bad (Fade initiation)
    double q; // Transition probability: Bad -> Good (Channel recovery)
    std::string physicalJustification;
};

class ChannelScenarios {
public:
    /**
     * @brief Returns the complete parameters for any scenario profile.
     * 
     * HOW TO MODIFY OR ADD NEW SCENARIOS:
     * To change p or q for any scenario, simply edit the p and q return values below!
     * Mathematical Formula: Theoretical PER (pi_B) = p / (p + q)
     * Average Burst Error Length (ABEL) = 1 / q
     */
    static ScenarioProfile getProfile(GEScenario sc) {
        switch (sc) {
            case GEScenario::URBAN_HIGH_DENSE:
                // Node Density > 1000 UEs/km^2
                // pi_B = 0.10 / (0.10 + 0.50) = 16.67%  |  ABEL = 1 / 0.50 = 2.0 pkts
                return {
                    "URBAN_HIGH",
                    "Urban High Dense",
                    ">1000 UEs/km^2, dense microcells, frequent collisions",
                    0.10, 0.50,
                    "High interference causes frequent drops, but dense microcells enable rapid recovery."
                };

            case GEScenario::URBAN_MEDIUM_DENSE:
                // Node Density ~500 UEs/km^2
                // pi_B = 0.05 / (0.05 + 0.20) = 20.00%  |  ABEL = 1 / 0.20 = 5.0 pkts
                return {
                    "URBAN_MED",
                    "Urban Medium Dense",
                    "~500 UEs/km^2, concrete building shadowing",
                    0.05, 0.20,
                    "Moderate interference with building shadowing causes medium burst outages."
                };

            case GEScenario::URBAN_MILD_DENSE:
                // Node Density ~100 UEs/km^2
                // pi_B = 0.08 / (0.08 + 0.40) = 16.67%  |  ABEL = 1 / 0.40 = 2.5 pkts
                return {
                    "URBAN_MILD",
                    "Urban Mild Dense",
                    "~100 UEs/km^2, fast vehicular fading",
                    0.08, 0.40,
                    "Fast vehicular speeds produce Doppler shifts and short, severe fade dips."
                };

            case GEScenario::RURAL_HIGH_DENSE:
                // Node Density ~50 UEs/km^2
                // pi_B = 0.01 / (0.01 + 0.90) = 1.10%  |  ABEL = 1 / 0.90 = 1.1 pkts
                return {
                    "RURAL_HIGH",
                    "Rural High Dense",
                    "~50 UEs/km^2, stationary IoT, line-of-sight",
                    0.01, 0.90,
                    "Stationary rural IoT with line-of-sight experiences almost no interference."
                };

            case GEScenario::RURAL_MEDIUM_DENSE:
                // Node Density ~20 UEs/km^2
                // pi_B = 0.04 / (0.04 + 0.10) = 28.57%  |  ABEL = 1 / 0.10 = 10.0 pkts
                return {
                    "RURAL_MED",
                    "Rural Medium Dense",
                    "~20 UEs/km^2, sparse base station coverage",
                    0.04, 0.10,
                    "Sparse rural base stations create large cell-edge zones with longer fade bursts."
                };

            case GEScenario::RURAL_LOW_DENSE:
                // Node Density < 5 UEs/km^2
                // pi_B = 0.06 / (0.06 + 0.05) = 54.55%  |  ABEL = 1 / 0.05 = 20.0 pkts
                return {
                    "RURAL_LOW",
                    "Rural Low Dense",
                    "<5 UEs/km^2, extreme range, terrain blockage",
                    0.06, 0.05,
                    "Extreme distance and hill/tree blockage cause deep, prolonged signal outages."
                };

            case GEScenario::CUSTOM_SCENARIO:
                // Custom user scenario: e.g. 99% packet drop!
                // Formula: pi_B = p / (p + q) = 0.99  =>  p = 99 * q.
                // For q = 0.01 and p = 0.99: pi_B = 0.99 / (0.99 + 0.01) = 99.0% | ABEL = 100 pkts
                return {
                    "CUSTOM",
                    "Custom Severe Degradation (99% Drop)",
                    "User-defined parameters for extreme channel testing",
                    0.99, 0.01,
                    "Custom channel profile configured for 99% packet error rate."
                };

            case GEScenario::BASELINE_CLEAN:
            default:
                // Control baseline: 0% loss
                // pi_B = 0.00 / (0.00 + 1.00) = 0.00%  |  ABEL = 1.0 pkt
                return {
                    "CLEAN",
                    "Baseline Clean Link",
                    "Ideal channel with 0% loss (wired/fiber control baseline)",
                    0.00, 1.00,
                    "Zero packet loss control baseline for comparative evaluation."
                };
        }
    }

    /**
     * @brief Resolves a scenario from incoming packet payload tags.
     */
    static bool resolveFromPayload(const std::string& payload, GEScenario& outScenario) {
        if (payload.find("5G_GE_URBAN_HIGH") != std::string::npos) { outScenario = GEScenario::URBAN_HIGH_DENSE; return true; }
        if (payload.find("5G_GE_URBAN_MED") != std::string::npos)  { outScenario = GEScenario::URBAN_MEDIUM_DENSE; return true; }
        if (payload.find("5G_GE_URBAN_MILD") != std::string::npos) { outScenario = GEScenario::URBAN_MILD_DENSE; return true; }
        if (payload.find("5G_GE_RURAL_HIGH") != std::string::npos) { outScenario = GEScenario::RURAL_HIGH_DENSE; return true; }
        if (payload.find("5G_GE_RURAL_MED") != std::string::npos)  { outScenario = GEScenario::RURAL_MEDIUM_DENSE; return true; }
        if (payload.find("5G_GE_RURAL_LOW") != std::string::npos)  { outScenario = GEScenario::RURAL_LOW_DENSE; return true; }
        if (payload.find("5G_GE_CUSTOM") != std::string::npos)     { outScenario = GEScenario::CUSTOM_SCENARIO; return true; }
        if (payload.find("5G_GE_CLEAN") != std::string::npos)      { outScenario = GEScenario::BASELINE_CLEAN; return true; }
        return false;
    }
};

} // namespace nr::error_model

