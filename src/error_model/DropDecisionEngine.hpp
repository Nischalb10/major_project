#pragma once

#include "MarkovChannelModel.hpp"
#include <random>
#include <cstdint>

namespace nr::error_model
{

struct DecisionResult {
    bool shouldDrop;             // True if packet must be erased, false if delivered
    GEState previousState;       // State before transition roll
    GEState currentState;        // State after transition roll
    bool stateTransitioned;      // True if state changed at this epoch
    uint64_t currentBurstLen;    // Current length of contiguous bad packets
    uint64_t maxBurstLen;        // Maximum contiguous burst length observed
    uint64_t totalBursts;        // Total number of discrete fading burst events
};

/**
 * @brief Drop Decision Engine.
 * Responsible for evaluating packet fate based on the Markov state machine.
 */
class DropDecisionEngine {
private:
    MarkovChannelModel m_model;
    uint64_t m_currentBurstLength;
    uint64_t m_maxBurstLength;
    uint64_t m_totalBursts;
    uint64_t m_totalBadSojourn;

    std::mt19937 m_rng;
    std::uniform_real_distribution<double> m_dist;

public:
    DropDecisionEngine(double p = 0.10, double q = 0.50)
        : m_model(p, q),
          m_currentBurstLength(0),
          m_maxBurstLength(0),
          m_totalBursts(0),
          m_totalBadSojourn(0),
          m_dist(0.0, 1.0)
    {
        std::random_device rd;
        m_rng.seed(rd());
    }

    void configure(double p, double q) {
        m_model.setParameters(p, q);
        reset();
    }

    void reset() {
        m_model.setState(GEState::GOOD);
        m_currentBurstLength = 0;
        m_maxBurstLength = 0;
        m_totalBursts = 0;
        m_totalBadSojourn = 0;
    }

    MarkovChannelModel& getModel() { return m_model; }
    const MarkovChannelModel& getModel() const { return m_model; }

    uint64_t getTotalBursts() const { return m_totalBursts; }
    uint64_t getTotalBadSojourn() const { return m_totalBadSojourn; }
    uint64_t getMaxBurstLength() const { return m_maxBurstLength; }

    /**
     * @brief Evaluates an incoming packet transmission epoch.
     * Transitions the Markov chain and returns the drop decision.
     */
    DecisionResult evaluatePacket() {
        GEState prevState = m_model.getState();
        double roll = m_dist(m_rng);

        bool transitioned = m_model.step(roll);
        GEState currState = m_model.getState();

        if (prevState == GEState::GOOD && currState == GEState::BAD) {
            // New burst event initiated
            m_totalBursts++;
            m_currentBurstLength = 0;
        } else if (prevState == GEState::BAD && currState == GEState::GOOD) {
            // Channel recovered from burst
            if (m_currentBurstLength > m_maxBurstLength) {
                m_maxBurstLength = m_currentBurstLength;
            }
            m_currentBurstLength = 0;
        }

        bool drop = (currState == GEState::BAD);

        if (drop) {
            m_totalBadSojourn++;
            m_currentBurstLength++;
            if (m_currentBurstLength > m_maxBurstLength) {
                m_maxBurstLength = m_currentBurstLength;
            }
        }

        return DecisionResult{
            drop,
            prevState,
            currState,
            transitioned,
            m_currentBurstLength,
            m_maxBurstLength,
            m_totalBursts
        };
    }
};

} // namespace nr::error_model

