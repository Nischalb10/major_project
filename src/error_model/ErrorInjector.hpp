#pragma once

#include "ChannelScenarios.hpp"
#include "DropDecisionEngine.hpp"
#include <mutex>
#include <iostream>
#include <iomanip>
#include <string>
#include <random>

namespace nr::error_model
{

/**
 * @brief Channel Error Injector Subsystem.
 * Directly implements the research paper's Gilbert-Elliott Discrete-Time Markov Chain (DTMC).
 * Emulates physical wireless channel fading: packets are transmitted across the 5G air interface
 * while time-correlated character/byte corruptions are injected during State Bad (deep fading bursts).
 * Evaluated by the receiver using Reed-Solomon MDS decoding and CRC-16 polynomial division.
 */
class ErrorInjector {
private:
    GEScenario m_currentScenario;
    ScenarioProfile m_profile;
    DropDecisionEngine m_decisionEngine;

    uint64_t m_totalPackets;
    uint64_t m_cleanPackets;
    uint64_t m_corruptedPackets;
    uint64_t m_totalBitFlips;

    std::mt19937 m_rng;
    std::mutex m_mutex;

    ErrorInjector()
        : m_currentScenario(GEScenario::URBAN_HIGH_DENSE),
          m_profile(ChannelScenarios::getProfile(m_currentScenario)),
          m_decisionEngine(m_profile.p, m_profile.q),
          m_totalPackets(0),
          m_cleanPackets(0),
          m_corruptedPackets(0),
          m_totalBitFlips(0)
    {
        std::random_device rd;
        m_rng.seed(rd());
    }

    void resetCountersInternal() {
        m_totalPackets = 0;
        m_cleanPackets = 0;
        m_corruptedPackets = 0;
        m_totalBitFlips = 0;
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
     * @brief Injects Gilbert-Elliott burst corruption into the user payload.
     * The packet is NEVER discarded; bytes/letters are scrambled in memory and the packet
     * continues through the 5G New Radio stack, gNodeB, UPF, and to the receiver.
     * @return true if error was injected, false if transmitted clean.
     */
    bool injectChannelErrors(uint8_t* data, size_t length, const std::string& nodeRole = "UE TRANSMITTER") {
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
        // Protect 3GPP RRC, NGAP, and SCTP heartbeats intact so the 5G connection never drops
        bool isTargetTraffic = (payload.find("5G_GE_") != std::string::npos) ||
                               (payload.find("5G_RS:") != std::string::npos) ||
                               (payload.find("5G_TEST") != std::string::npos);

        if (!isTargetTraffic) {
            return false; // Deliver control signaling untouched!
        }

        m_totalPackets++;

        // 3. Step the 2-State Markov Chain (State Good vs State Bad)
        DecisionResult res = m_decisionEngine.evaluatePacket();
        bool isCorrupted = false;
        int bytesFlippedInPkt = 0;

        if (res.shouldDrop) {
            // STATE BAD: Inject fading burst corruption into the user payload
            m_corruptedPackets++;
            isCorrupted = true;

            // Check if this is a Reed-Solomon frame: "5G_RS:"
            size_t rsPos = payload.find("5G_RS:");
            if (rsPos != std::string::npos) {
                // Zero out the UDP checksum field (2 bytes preceding the UDP payload)
                // In RFC 768 / IPv4, checksum 0x0000 disables kernel-level checksum verification,
                // allowing corrupted datagrams to reach Application-Layer FEC (Reed-Solomon)
                if (rsPos >= 2) {
                    data[rsPos - 2] = 0x00;
                    data[rsPos - 1] = 0x00;
                }

                // Header format: "5G_RS:" (6 bytes) + length (2 bytes) + crc (2 bytes) = 10 bytes
                size_t payloadStart = rsPos + 6 + 4;
                if (payloadStart < length) {
                    size_t availableBytes = length - payloadStart;
                    
                    // Corruption depth in State Bad is dynamically derived from Markov parameters (p and q)
                    // Fading severity metric: pi_B = p / (p + q), Mean Burst Length ABEL = 1 / q
                    double p_val = m_profile.p;
                    double q_val = m_profile.q;
                    double pi_B_val = (p_val + q_val > 0.0) ? (p_val / (p_val + q_val)) : 0.0;
                    double abel_val = (q_val > 0.0) ? (1.0 / q_val) : 1.0;

                    // Physical RF scaling: deeper fades (higher pi_B, higher 1/q) corrupt more symbols
                    int baseCorrupt = static_cast<int>(std::round(1.0 + 8.0 * pi_B_val + 0.15 * (abel_val - 1.0)));
                    int jitter = static_cast<int>(m_rng() % 2); // Stochastic multipath variation (+0 or +1)
                    int charsToCorrupt = std::max(1, baseCorrupt + jitter);

                    if (charsToCorrupt > static_cast<int>(availableBytes)) {
                        charsToCorrupt = static_cast<int>(availableBytes);
                    }

                    // Target corruption within the visible message body (after tag & seq header)
                    // so the user and professor clearly see corrupted letters in their text message
                    size_t textOffset = 0;
                    if (availableBytes > 32) {
                        textOffset = 28; // Skip "[5G_GE_TAG] [SEQ #XXX] "
                    }
                    size_t corruptRange = availableBytes - textOffset;

                    // Use clearly visible noise characters so errors are instantly noticeable to human eyes
                    static const char NOISE_CHARS[] = "#?*@!~$%&X123456789";
                    static const size_t NUM_NOISE = sizeof(NOISE_CHARS) - 1;

                    for (int c = 0; c < charsToCorrupt && corruptRange > 0; ++c) {
                        size_t targetOffset = payloadStart + textOffset + (m_rng() % corruptRange);
                        char oldChar = data[targetOffset];
                        char newNoise = NOISE_CHARS[m_rng() % NUM_NOISE];
                        if (newNoise == oldChar) {
                            newNoise = (newNoise == '#') ? '?' : '#';
                        }
                        data[targetOffset] = newNoise;
                        bytesFlippedInPkt++;
                        m_totalBitFlips++;
                    }
                }
            } else {
                // For unencoded text: corrupt characters directly
                size_t tagPos = payload.find("5G_GE_");
                if (tagPos != std::string::npos && tagPos + 25 < length) {
                    size_t targetOffset = tagPos + 20 + (m_rng() % (length - (tagPos + 20)));
                    data[targetOffset] ^= 0x24; // Scramble letter
                    bytesFlippedInPkt++;
                    m_totalBitFlips++;
                }
            }
        } else {
            // STATE GOOD: Clean channel, 0 errors
            m_cleanPackets++;
        }

        // 4. Calculate Empirical vs Theoretical Markov Metrics
        double theo_pi_B = m_decisionEngine.getModel().getTheoreticalPER();
        double theo_abel = m_decisionEngine.getModel().getTheoreticalABEL();
        double emp_fer = (m_totalPackets > 0) ? ((double)m_corruptedPackets / (double)m_totalPackets) * 100.0 : 0.0;
        double emp_abel = (res.totalBursts > 0) ? ((double)m_decisionEngine.getTotalBadSojourn() / (double)res.totalBursts) : 0.0;
        double convergence = (theo_pi_B > 0) ? (1.0 - std::abs(emp_fer / 100.0 - theo_pi_B) / theo_pi_B) * 100.0 : 100.0;
        if (convergence < 0) convergence = 0.0;

        // 5. Console Telemetry Output
        std::cout << "\n";
        std::cout << "  =========================================================================\n";
        std::cout << "  [" << nodeRole << "] GILBERT-ELLIOTT 5G BURST ERROR INJECTOR\n";
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
        std::cout << "     Fading Prob (pi_B): " << std::setprecision(2) << (theo_pi_B * 100.0) << " %\n";
        std::cout << "     ABEL (1/q)        : " << std::setprecision(1) << theo_abel << " pkts/burst\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   AIR INTERFACE TRANSMISSION (Packet Sent via 5G NR RLS)\n";
        std::cout << "     Channel State     : " 
                  << (isCorrupted ? "\033[1;31m[ STATE: BAD  - FADING BURST INJECTED (" + std::to_string(bytesFlippedInPkt) + " chars) ]\033[0m" 
                                  : "\033[1;32m[ STATE: GOOD - CLEAN TRANSMISSION (0 errors) ]\033[0m") << "\n";
        if (isCorrupted) {
            size_t rsPreviewPos = payload.find("5G_RS:");
            if (rsPreviewPos != std::string::npos && rsPreviewPos + 10 < length) {
                std::string preview(reinterpret_cast<const char*>(data + rsPreviewPos + 10), std::min(length - rsPreviewPos - 10, (size_t)60));
                std::cout << "     Injected Payload  : \033[1;31m'" << preview << "'\033[0m\n";
            }
        }
        std::cout << "     Total Frames      : " << std::setw(6) << m_totalPackets << "\n";
        std::cout << "     Clean Frames      : " << std::setw(6) << m_cleanPackets 
                  << "  (" << std::setprecision(1) << (100.0 - emp_fer) << " %)\n";
        std::cout << "     Corrupted Frames  : " << std::setw(6) << m_corruptedPackets 
                  << "  (" << emp_fer << " % FER)\n";
        std::cout << "     Total Corruptions : " << std::setw(6) << m_totalBitFlips << " chars\n";
        std::cout << "     Fading Bursts     : " << std::setw(6) << res.totalBursts << "\n";
        std::cout << "     Empirical ABEL    : " << std::setprecision(2) << emp_abel << " pkts/burst\n";
        std::cout << "  -------------------------------------------------------------------------\n";
        std::cout << "   CONVERGENCE\n";
        std::cout << "     Theo pi_B vs Emp  : " << std::setprecision(1) << (theo_pi_B * 100.0) << "% vs " << emp_fer 
                  << "%  [Convergence: " << convergence << "%]\n";
        std::cout << "  =========================================================================\n\n";

        return isCorrupted;
    }

    /**
     * @brief Backward-compatibility hook.
     */
    bool shouldDropPacket(const uint8_t* data, size_t length, const std::string& nodeRole = "UE TRANSMITTER") {
        return false;
    }
};

} // namespace nr::error_model
