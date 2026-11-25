#pragma once

#include <cstdint>
#include <string>
#include <cmath>

namespace nr::error_model
{

/**
 * @brief Discrete-Time Markov Chain (DTMC) States.
 * State G (GOOD) = Clear channel, error probability e_G = 0.0
 * State B (BAD)  = In-fade/interference, error probability e_B = 1.0
 */
enum class GEState {
    GOOD = 0,
    BAD = 1
};

/**
 * @brief Pure Mathematical 2-State Gilbert-Elliott Markov Model.
 * Represents the discrete stochastic process S_k in {GOOD, BAD}.
 *
 * Transition Matrix:
 *        [ 1-p    p  ]
 *    P = [           ]
 *        [  q    1-q ]
 *
 * Where:
 *   p = P(S_{k+1} = BAD  | S_k = GOOD) -> Fade initiation rate
 *   q = P(S_{k+1} = GOOD | S_k = BAD)  -> Fade recovery rate
 */
class MarkovChannelModel {
private:
    GEState m_state;
    double m_p; // Transition probability: Good -> Bad
    double m_q; // Transition probability: Bad -> Good

public:
    MarkovChannelModel(double p = 0.10, double q = 0.50)
        : m_state(GEState::GOOD), m_p(p), m_q(q) {}

    void setParameters(double p, double q) {
        m_p = p;
        m_q = q;
        m_state = GEState::GOOD;
    }

    double getP() const { return m_p; }
    double getQ() const { return m_q; }
    GEState getState() const { return m_state; }
    void setState(GEState state) { m_state = state; }

    /**
     * @brief Transitions the Markov chain based on a uniform random draw in [0, 1).
     * @param roll A random double in [0.0, 1.0)
     * @return true if state transitioned, false if state remained the same
     */
    bool step(double roll) {
        if (m_state == GEState::GOOD) {
            if (roll < m_p) {
                m_state = GEState::BAD;
                return true; // Transitioned G -> B
            }
        } else { // State is BAD
            if (roll < m_q) {
                m_state = GEState::GOOD;
                return true; // Transitioned B -> G
            }
        }
        return false; // No state change
    }

    /**
     * @brief Calculates theoretical stationary probability of State Bad (pi_B).
     * pi_B = p / (p + q) = Long-term Packet Error Rate (PER).
     */
    double getTheoreticalPER() const {
        if (m_p + m_q <= 0.0) return 0.0;
        return m_p / (m_p + m_q);
    }

    /**
     * @brief Calculates theoretical Average Burst Error Length (ABEL).
     * ABEL = 1 / q = Expected consecutive packets spent in State Bad.
     */
    double getTheoreticalABEL() const {
        if (m_q <= 0.0) return 0.0;
        return 1.0 / m_q;
    }

    /**
     * @brief Calculates theoretical Average Good State Sojourn Length (AGSL).
     * AGSL = 1 / p = Expected consecutive packets spent in State Good.
     */
    double getTheoreticalAGSL() const {
        if (m_p <= 0.0) return 0.0;
        return 1.0 / m_p;
    }
};

} // namespace nr::error_model

