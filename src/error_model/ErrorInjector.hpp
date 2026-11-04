#pragma once

#include "ChannelScenarios.hpp"
#include "DropDecisionEngine.hpp"
#include <mutex>
#include <iostream>
#include <iomanip>
#include <string>

namespace nr::error_model
{

/**
 * @brief Channel Error Injector.
 * The boundary subsystem between UERANSIM RLS and the channel error models.
 * Manages thread-safe packet interception, signaling safety, and telemetry.
 */
class ErrorInjector {
private:
    GEScenario m_currentScenario;
    ScenarioProfile m_profile;
    DropDecisionEngine m_decisionEngine;

    uint64_t m_totalPackets;
    uint64_t m_deliveredPackets;
    uint64_t m_droppedPackets;

    std::mutex m_mutex;

    ErrorInjector()
        : m_currentScenario(GEScenario::URBAN_HIGH_DENSE),
          m_profile(ChannelScenarios::getProfile(m_currentScenario)),
          m_decisionEngine(m_profile.p, m_profile.q),
          m_totalPackets(0),
          m_deliveredPackets(0),
          m_droppedPackets(0)
    {}

    void resetCountersInternal() {
        m_totalPackets = 0;
        m_deliveredPackets = 0;
        m_droppedPackets = 0;
        m_decisionEngine.reset();
    }

public:
    static ErrorInjector& getInstance() {
        static ErrorInjector instance;
        return instance;
    }

    void setScenario(GEScenario sc) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentScenario = sc;
        m_profile = ChannelScenarios::getProfile(sc);
        m_decisionEngine.configure(m_profile.p, m_profile.q);
        resetCountersInternal();
    }

    void resetStats() {
        std::lock_guard<std::mutex> lock(m_mutex);
        resetCountersInternal();
    }

    /**
     * @brief Intercepts an outgoing 5G PDU packet and decides if it should be dropped.
     * @return true if the packet should be dropped (erased), false if delivered.
     */
    bool shouldDropPacket(const uint8_t* data, size_t length, const std::string& nodeRole = "UE TRANSMITTER") {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::string payload(reinterpret_cast<const char*>(data), length);

        // 1. Scenario auto-selection from payload tags
        GEScenario requestedScenario = m_currentScenario;
        if (ChannelScenarios::resolveFromPayload(payload, requestedScenario)) {
            if (requestedScenario != m_currentScenario) {
                m_currentScenario = requestedScenario;
                m_profile = ChannelScenarios::getProfile(m_currentScenario);
                m_decisionEngine.configure(m_profile.p, m_profile.q);
                resetCountersInternal();
            }
        }

        // 2. Control-Plane Safety Filter
        // Pass through 3GPP RRC, NGAP, and SCTP heartbeats intact so the 5G connection never drops
        bool isTargetTraffic = (payload.find("5G_GE_") != std::string::npos) ||
                               (payload.find("5G_TEST") != std::string::npos);

        if (!isTargetTraffic) {
            return false; // Deliver control signaling untouched!
        }

        m_totalPackets++;

        // 3. Evaluate packet fate using the Drop Decision Engine
        DecisionResult res = m_decisionEngine.evaluatePacket();

        if (res.shouldDrop) {
            m_droppedPackets++;
        } else {
            m_deliveredPackets++;
        }

        // 4. Calculate Empirical vs Theoretical Metrics
        double theo_pi_B = m_decisionEngine.getModel().getTheoreticalPER();
        double theo_abel = m_decisionEngine.getModel().getTheoreticalABEL();
        double emp_per = (m_totalPackets > 0) ? ((double)m_droppedPackets / (double)m_totalPackets) * 100.0 : 0.0;
        double emp_abel = (res.totalBursts > 0) ? ((double)m_decisionEngine.getTotalBadSojourn() / (double)res.totalBursts) : 0.0;
        double convergence = (theo_pi_B > 0) ? (1.0 - std::abs(emp_per / 100.0 - theo_pi_B) / theo_pi_B) * 100.0 : 100.0;
        if (convergence < 0) convergence = 0.0;

        // 5. Real-Time Telemetry Dashboard Output
        std::cout << "\n";
        std::cout << "  =========================================================================\n";
        std::cout << "  [" << nodeRole << "] MODULAR GILBERT-ELLIOTT 5G ERROR INJECTOR\n";
        std::cout << "  =========================================================================\n";
        std::cout << "   Active Scenario     : " << m_profile.name << " (" << m_profile.id << ")\n";
        std::cout << "   RF Environment      : " << m_profile.description << "\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   MARKOV PARAMETERS\n";
        std::cout << "     p (G->B)          : " << std::fixed << std::setprecision(4) << m_profile.p 
                  << "    (Mean Good Sojourn: " << std::setprecision(1) << m_decisionEngine.getModel().getTheoreticalAGSL() << " pkts)\n";
        std::cout << "     q (B->G)          : " << std::setprecision(4) << m_profile.q 
                  << "    (Mean Bad Sojourn : " << std::setprecision(1) << theo_abel << " pkts)\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   THEORETICAL MODEL (Steady-State Targets)\n";
        std::cout << "     PER  (pi_B)       : " << std::setprecision(2) << (theo_pi_B * 100.0) << " %\n";
        std::cout << "     ABEL (1/q)        : " << std::setprecision(1) << theo_abel << " pkts/burst\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   EMPIRICAL RESULTS (Live)\n";
        std::cout << "     Channel State     : " 
                  << (res.shouldDrop ? "\033[1;31m[ STATE: BAD  - PACKET DROPPED ]\033[0m" 
                                     : "\033[1;32m[ STATE: GOOD - PACKET DELIVERED ]\033[0m") << "\n";
        std::cout << "     Total Packets     : " << std::setw(6) << m_totalPackets << "\n";
        std::cout << "     Delivered         : " << std::setw(6) << m_deliveredPackets 
                  << "  (" << std::setprecision(1) << (100.0 - emp_per) << " %)\n";
        std::cout << "     Dropped           : " << std::setw(6) << m_droppedPackets 
                  << "  (" << emp_per << " % PER)\n";
        std::cout << "     Burst Count       : " << std::setw(6) << res.totalBursts << "\n";
        std::cout << "     Current Burst Len : " << std::setw(6) << res.currentBurstLen << " pkts\n";
        std::cout << "     Max Burst Len     : " << std::setw(6) << res.maxBurstLen << " pkts\n";
        std::cout << "     Empirical ABEL    : " << std::setprecision(2) << emp_abel << " pkts/burst\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   CONVERGENCE\n";
        std::cout << "     Theo PER vs Emp   : " << std::setprecision(1) << (theo_pi_B * 100.0) << "% vs " << emp_per 
                  << "%  [Convergence: " << convergence << "%]\n";
        std::cout << "     Theo ABEL vs Emp  : " << std::setprecision(1) << theo_abel << " vs " 
                  << std::setprecision(2) << emp_abel << " pkts/burst\n";
        std::cout << "  =========================================================================\n\n";

        return res.shouldDrop;
    }
};

} // namespace nr::error_model
