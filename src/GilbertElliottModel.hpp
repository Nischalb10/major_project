#pragma once

#include <vector>
#include <random>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <mutex>

enum class GEScenario {
    URBAN_HIGH_DENSE,   // High interference, frequent short bursts
    URBAN_MEDIUM_DENSE, // Moderate bursts, building shadowing
    URBAN_MILD_DENSE,   // Brief, severe bursts from fast fading
    RURAL_HIGH_DENSE,   // Low interference, isolated drops
    RURAL_MEDIUM_DENSE, // Long, heavy bursts due to distance
    RURAL_LOW_DENSE,    // Extreme range fading, prolonged drops
    BASELINE_CLEAN      // 0% loss (wired/ideal link)
};

enum class GEState {
    GOOD = 0,
    BAD = 1
};

struct GEScenarioParams {
    std::string name;
    std::string description;
    double p; // Transition from Good -> Bad (loss initiation)
    double q; // Transition from Bad -> Good (recovery probability)
};

class GilbertElliottChannel {
private:
    GEState m_state;
    GEScenario m_currentScenario;
    GEScenarioParams m_params;

    // Empirical metrics
    uint64_t m_totalPackets;
    uint64_t m_deliveredPackets;
    uint64_t m_droppedPackets;
    uint64_t m_totalBursts;
    uint64_t m_currentBurstLength;
    uint64_t m_maxBurstLength;
    uint64_t m_totalBadSojourn; // Total packets spent in Bad state across all bursts

    std::mt19937 m_rng;
    std::uniform_real_distribution<double> m_dist;
    std::mutex m_mutex;

    // ---------------------------------------------------------------------------
    // Scenario Parameterization based on Section 4, Table of the Research Paper.
    //
    // Key Mathematical Relationships (Page 2):
    //   Stationary probability of Bad state:  pi_B  = p / (p + q)
    //   Average Burst Error Length:           ABEL  = 1 / q
    //   Average Good-state Sojourn:           AGSL  = 1 / p
    //
    // Physical Justification:
    //   Urban High Dense:  Many UEs cause frequent interference collisions (high p),
    //                      but dense tower coverage enables fast recovery (high q).
    //   Rural Low Dense:   Few UEs mean rare but devastating terrain/distance-based
    //                      fades (moderate p, very low q -> long outage bursts).
    // ---------------------------------------------------------------------------
    GEScenarioParams getParamsForScenario(GEScenario sc) {
        switch (sc) {
            case GEScenario::URBAN_HIGH_DENSE:
                // Node Density > 1000 UEs/km^2
                // High co-channel interference causes frequent drops, but dense
                // tower coverage enables rapid handover/recovery.
                // pi_B = 0.10/(0.10+0.50) = 16.7%  |  ABEL = 1/0.50 = 2.0 pkts
                return {"Urban High Dense", "High interference, frequent short bursts (>1000 UEs/km^2)", 0.10, 0.50};
            case GEScenario::URBAN_MEDIUM_DENSE:
                // Node Density ~500 UEs/km^2
                // Moderate interference with building shadowing, slower recovery.
                // pi_B = 0.05/(0.05+0.20) = 20.0%  |  ABEL = 1/0.20 = 5.0 pkts
                return {"Urban Medium Dense", "Moderate bursts, building shadowing (~500 UEs/km^2)", 0.05, 0.20};
            case GEScenario::URBAN_MILD_DENSE:
                // Node Density ~100 UEs/km^2
                // Fast vehicular fading, brief but severe bursts.
                // pi_B = 0.08/(0.08+0.40) = 16.7%  |  ABEL = 1/0.40 = 2.5 pkts
                return {"Urban Mild/Low Dense", "Brief, severe bursts from vehicular fading (~100 UEs/km^2)", 0.08, 0.40};
            case GEScenario::RURAL_HIGH_DENSE:
                // Node Density ~50 UEs/km^2
                // Stationary IoT, almost no interference, very rare and brief drops.
                // pi_B = 0.01/(0.01+0.90) = 1.1%  |  ABEL = 1/0.90 = 1.1 pkts
                return {"Rural High Dense", "Stationary IoT, isolated drops (~50 UEs/km^2)", 0.01, 0.90};
            case GEScenario::RURAL_MEDIUM_DENSE:
                // Node Density ~20 UEs/km^2
                // Agricultural IoT, sparse coverage causes longer burst drops.
                // pi_B = 0.04/(0.04+0.10) = 28.6%  |  ABEL = 1/0.10 = 10.0 pkts
                return {"Rural Medium Dense", "Agricultural, long bursts from sparse coverage (~20 UEs/km^2)", 0.04, 0.10};
            case GEScenario::RURAL_LOW_DENSE:
                // Node Density < 5 UEs/km^2
                // Extreme range, terrain blockage. Once signal fades, it stays gone.
                // pi_B = 0.06/(0.06+0.05) = 54.5%  |  ABEL = 1/0.05 = 20.0 pkts
                return {"Rural Low Dense", "Extreme range fading, prolonged outage (<5 UEs/km^2)", 0.06, 0.05};
            case GEScenario::BASELINE_CLEAN:
            default:
                // Ideal wired/fiber link with 0% loss for control comparison.
                // pi_B = 0.00/(0.00+1.00) = 0.0%  |  ABEL = 1/1.00 = 1.0 pkt
                return {"Baseline Clean", "Ideal channel with 0% loss (wired link)", 0.00, 1.00};
        }
    }

    void resetStatsInternal() {
        m_totalPackets = 0;
        m_deliveredPackets = 0;
        m_droppedPackets = 0;
        m_totalBursts = 0;
        m_currentBurstLength = 0;
        m_maxBurstLength = 0;
        m_totalBadSojourn = 0;
    }

public:
    GilbertElliottChannel()
        : m_state(GEState::GOOD),
          m_currentScenario(GEScenario::URBAN_HIGH_DENSE),
          m_totalPackets(0),
          m_deliveredPackets(0),
          m_droppedPackets(0),
          m_totalBursts(0),
          m_currentBurstLength(0),
          m_maxBurstLength(0),
          m_totalBadSojourn(0),
          m_dist(0.0, 1.0)
    {
        std::random_device rd;
        m_rng.seed(rd());
        m_params = getParamsForScenario(m_currentScenario);
    }

    static GilbertElliottChannel& getInstance() {
        static GilbertElliottChannel instance;
        return instance;
    }

    void setScenario(GEScenario sc) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentScenario = sc;
        m_params = getParamsForScenario(sc);
        m_state = GEState::GOOD;
        resetStatsInternal();
    }

    void resetStats() {
        std::lock_guard<std::mutex> lock(m_mutex);
        resetStatsInternal();
    }

    // Process a packet: returns true if packet is DELIVERED, false if DROPPED
    bool processPacket(const uint8_t* data, size_t length, const std::string& nodeRole = "RADIO") {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::string payload(reinterpret_cast<const char*>(data), length);

        // -----------------------------------------------------------------------
        // Scenario auto-selection from payload tags.
        // When switching scenario, reset stats so empirical metrics start fresh
        // and converge cleanly to the new theoretical values.
        // -----------------------------------------------------------------------
        GEScenario requestedScenario = m_currentScenario;
        if (payload.find("5G_GE_URBAN_HIGH") != std::string::npos) requestedScenario = GEScenario::URBAN_HIGH_DENSE;
        else if (payload.find("5G_GE_URBAN_MED") != std::string::npos) requestedScenario = GEScenario::URBAN_MEDIUM_DENSE;
        else if (payload.find("5G_GE_URBAN_MILD") != std::string::npos) requestedScenario = GEScenario::URBAN_MILD_DENSE;
        else if (payload.find("5G_GE_RURAL_HIGH") != std::string::npos) requestedScenario = GEScenario::RURAL_HIGH_DENSE;
        else if (payload.find("5G_GE_RURAL_MED") != std::string::npos) requestedScenario = GEScenario::RURAL_MEDIUM_DENSE;
        else if (payload.find("5G_GE_RURAL_LOW") != std::string::npos) requestedScenario = GEScenario::RURAL_LOW_DENSE;
        else if (payload.find("5G_GE_CLEAN") != std::string::npos) requestedScenario = GEScenario::BASELINE_CLEAN;

        if (requestedScenario != m_currentScenario) {
            m_currentScenario = requestedScenario;
            m_params = getParamsForScenario(m_currentScenario);
            m_state = GEState::GOOD;
            resetStatsInternal();
        }

        // Only inject loss on test payloads (pass control-plane signaling intact)
        bool isTargetTraffic = (payload.find("5G_GE_") != std::string::npos) ||
                               (payload.find("5G_TEST") != std::string::npos);

        if (!isTargetTraffic) {
            return true; // Pass through heartbeats and control signaling untouched
        }

        m_totalPackets++;

        // -----------------------------------------------------------------------
        // Gilbert-Elliott 2-State Markov Chain Transition Logic
        //
        // State transitions happen BEFORE packet fate is decided for the current
        // packet. This matches the standard DTMC formulation where the chain
        // transitions at each time step and then the current state determines
        // the packet outcome.
        //
        //   In GOOD state:  P(stay GOOD) = 1 - p,  P(transition to BAD) = p
        //   In BAD state:   P(stay BAD)  = 1 - q,  P(transition to GOOD) = q
        // -----------------------------------------------------------------------
        double roll = m_dist(m_rng);

        if (m_state == GEState::GOOD) {
            if (roll < m_params.p) {
                m_state = GEState::BAD;
                m_totalBursts++;
                m_currentBurstLength = 0; // Will be incremented below
            }
        } else { // In BAD state
            if (roll < m_params.q) {
                m_state = GEState::GOOD;
                // Update max burst before resetting
                if (m_currentBurstLength > m_maxBurstLength) {
                    m_maxBurstLength = m_currentBurstLength;
                }
                m_currentBurstLength = 0;
            }
        }

        // Determine packet fate based on current state
        bool dropped = (m_state == GEState::BAD);

        if (dropped) {
            m_droppedPackets++;
            m_totalBadSojourn++;
            m_currentBurstLength++;
            // Update max burst length live
            if (m_currentBurstLength > m_maxBurstLength) {
                m_maxBurstLength = m_currentBurstLength;
            }
        } else {
            m_deliveredPackets++;
        }

        // -----------------------------------------------------------------------
        // Theoretical vs Empirical Metric Calculations
        // -----------------------------------------------------------------------
        double theo_pi_B = (m_params.p + m_params.q > 0) ? (m_params.p / (m_params.p + m_params.q)) : 0.0;
        double theo_abel = (m_params.q > 0) ? (1.0 / m_params.q) : 0.0;
        double emp_per = (m_totalPackets > 0) ? ((double)m_droppedPackets / (double)m_totalPackets) * 100.0 : 0.0;
        double emp_abel = (m_totalBursts > 0) ? ((double)m_totalBadSojourn / (double)m_totalBursts) : 0.0;
        double convergence = (theo_pi_B > 0) ? (1.0 - std::abs(emp_per / 100.0 - theo_pi_B) / theo_pi_B) * 100.0 : 
                             (emp_per == 0 ? 100.0 : 0.0);
        if (convergence < 0) convergence = 0.0;

        // -----------------------------------------------------------------------
        // Real-Time Dashboard Output
        // -----------------------------------------------------------------------
        std::cout << "\n";
        std::cout << "  =========================================================================\n";
        std::cout << "  [" << nodeRole << "] GILBERT-ELLIOTT 2-STATE MARKOV ERROR MODEL\n";
        std::cout << "  =========================================================================\n";
        std::cout << "   Scenario            : " << m_params.name << "\n";
        std::cout << "   Description         : " << m_params.description << "\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   MARKOV PARAMETERS\n";
        std::cout << "     p (G->B)          : " << std::fixed << std::setprecision(4) << m_params.p 
                  << "    (Avg Good Sojourn: " << std::setprecision(1) << (m_params.p > 0 ? 1.0/m_params.p : 0) << " pkts)\n";
        std::cout << "     q (B->G)          : " << std::setprecision(4) << m_params.q 
                  << "    (Avg Bad Sojourn:  " << std::setprecision(1) << theo_abel << " pkts)\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   THEORETICAL MODEL (Steady-State)\n";
        std::cout << "     PER  (pi_B)       : " << std::setprecision(2) << (theo_pi_B * 100.0) << " %\n";
        std::cout << "     ABEL (1/q)        : " << std::setprecision(1) << theo_abel << " pkts/burst\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   EMPIRICAL RESULTS (Live)\n";
        std::cout << "     Channel State     : " 
                  << (dropped ? "\033[1;31m[ BAD  - PACKET DROPPED ]\033[0m" 
                              : "\033[1;32m[ GOOD - PACKET DELIVERED ]\033[0m") << "\n";
        std::cout << "     Total Packets     : " << std::setw(6) << m_totalPackets << "\n";
        std::cout << "     Delivered         : " << std::setw(6) << m_deliveredPackets 
                  << "  (" << std::setprecision(1) << (100.0 - emp_per) << " %)\n";
        std::cout << "     Dropped           : " << std::setw(6) << m_droppedPackets 
                  << "  (" << emp_per << " % PER)\n";
        std::cout << "     Burst Count       : " << std::setw(6) << m_totalBursts << "\n";
        std::cout << "     Current Burst Len : " << std::setw(6) << m_currentBurstLength << " pkts\n";
        std::cout << "     Max Burst Len     : " << std::setw(6) << m_maxBurstLength << " pkts\n";
        std::cout << "     Empirical ABEL    : " << std::setprecision(2) << emp_abel << " pkts/burst\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   CONVERGENCE\n";
        std::cout << "     Theo PER vs Emp   : " << std::setprecision(1) << (theo_pi_B * 100.0) << "% vs " << emp_per 
                  << "%  [Convergence: " << convergence << "%]\n";
        std::cout << "     Theo ABEL vs Emp  : " << std::setprecision(1) << theo_abel << " vs " 
                  << std::setprecision(2) << emp_abel << " pkts/burst\n";
        std::cout << "  =========================================================================\n\n";

        return !dropped;
    }
};
