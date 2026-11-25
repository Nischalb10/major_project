# Gilbert-Elliott Fading Channel Emulation & Reed-Solomon AL-FEC over 5G Standalone Networks

A real-time **5G Standalone (SA)** wireless channel emulation and **Application-Layer Forward Error Correction (AL-FEC)** testbed built on **Open5GS** and **UERANSIM**.  
This project emulates time-correlated burst errors using a **2-State Gilbert-Elliott Discrete-Time Markov Chain (DTMC)** inside UERANSIM's 5G NR Radio Link Simulation (RLS) layer, and performs autonomous, zero-cheat error detection and recovery using **Reed-Solomon $\text{RS}(N+10, N)$ over $\text{GF}(2^8)$** and **CRC-16-CCITT**.

---

## Table of Contents
1. [Abstract & Motivation](#1-abstract--motivation)
2. [System Architecture & Design](#2-system-architecture--design)
3. [Error Model: 2-State Gilbert-Elliott DTMC](#3-error-model-2-state-gilbert-elliott-dtmc)
4. [Error Injection & RFC 768 Checksum Bypass](#4-error-injection--rfc-768-checksum-bypass)
5. [Error Detection & Recovery Algorithms](#5-error-detection--recovery-algorithms)
6. [Repository Structure](#6-repository-structure)
7. [How to Run](#7-how-to-run)
8. [Experimental Results & Test Cases](#8-experimental-results--test-cases)
9. [Screenshots](#9-screenshots)
10. [Conclusion & Future Scope](#10-conclusion--future-scope)

---

## 1. Abstract & Motivation

Real-world 5G wireless channels do not suffer from independent random bit flips (AWGN); instead, multipath fading, urban building shadowing, vehicular Doppler shifts, and cell-edge terrain blockage cause **time-correlated burst errors**. Traditional ARQ/TCP retransmissions introduce high latency unsuitable for URLLC and Industrial IoT workloads.

This project addresses two fundamental challenges:
1. **Realistic Burst Fading in a Live 5G SA Stack:** Embedding a mathematical 2-State Gilbert-Elliott Markov model directly into UERANSIM's NR RLS transmitter (`src/ue/rls/udp_task.cpp`) across 8 empirical RF scenarios while protecting 3GPP control-plane signaling (RRC, NGAP, SCTP).
2. **Observing & Repairing Corrupted Payloads in User Space:** Standard OS kernels silently drop UDP packets with invalid checksums. By applying **RFC 768 UDP checksum zeroing (`0x0000`)** and `SO_NO_CHECK`, corrupted datagrams traverse the entire 5G SA stack (`UE3 → gNodeB → Open5GS UPF → UE1`) and are autonomously repaired at the receiver using pure-Python **Reed-Solomon $\text{GF}(2^8)$** decoding and **CRC-16-CCITT** verification.

---

## 2. System Architecture & Design

```text
[Sender App: al-fec/sender_traffic_gen.py]
   │  1. Encode payload with RS(N+10, N) over GF(2^8) + CRC-16-CCITT
   │  2. Enable SO_NO_CHECK (RFC 768) on UDP socket
   ▼
[UE3 TUN Interface (uesimtun0)] ──► [UERANSIM UE3 RLS: src/ue/rls/udp_task.cpp]
                                         │
                                         ▼
                              [src/error_model/ErrorInjector.hpp (C++)]
                              • Step Gilbert-Elliott DTMC (p, q)
                              • State GOOD: 0 errors
                              • State BAD: Inject visible noise (#?*@!...)
                              • Zero UDP checksum (0x0000)
                              • NEVER drop packet
                                         │
                                         ▼
[gNodeB] ──► [Open5GS 5G Core: AMF / SMF / UPF (GTP-U)] ──► [gNodeB] ──► [UE1 TUN]
                                                                              │
                                                                              ▼
                                                        [Receiver: al-fec/packet_receiver.py (:9999)]
                                                         • al-fec/error_detector.py (RS Syndromes + BM + Chien + Forney + CRC-16)
                                                         • al-fec/metrics_engine.py → /tmp/5g_telemetry.json
                                                                              │
                                                                              ▼
                                                        [Telemetry Server: al-fec/telemetry_server.py (:8080)]
                                                         • Live Web Dashboard (al-fec/dashboard/web/index.html)
```

### Design Highlights
- **Zero Global Cheating:** The receiver never accesses the original transmitted string or any simulation flags; it relies strictly on mathematical syndromes and CRC-16 polynomial division.
- **Cross-Namespace IPC:** Since UE1's receiver executes inside an isolated Linux network namespace (`ueransim-...-internet-psi1`), telemetry is bridged via `/tmp/5g_telemetry.json` (`chmod 0666`) and synced every `40ms` by `telemetry_server.py`.
- **Static Subscriber IP Bindings:** Registered in MongoDB (`al-fec/register_subscribers.js`) so `imsi-1 → 10.45.0.2` and `imsi-3 → 10.45.0.4` remain deterministic across restarts.

---

## 3. Error Model: 2-State Gilbert-Elliott DTMC

Implemented in `src/error_model/MarkovChannelModel.hpp` and `src/error_model/DropDecisionEngine.hpp`, the channel alternates between two states $S_k \in \{\text{GOOD } (G), \text{BAD } (B)\}$:

$$P = \begin{bmatrix} 1 - p & p \\ q & 1 - q \end{bmatrix}$$

- **$p = P(S_{k+1} = B \mid S_k = G)$**: Fade initiation probability.
- **$q = P(S_{k+1} = G \mid S_k = B)$**: Channel recovery probability.
- **Steady-State Fading Probability (Theoretical FER):** $\pi_B = \frac{p}{p + q}$
- **Average Burst Error Length (ABEL):** $\text{ABEL} = \frac{1}{q}$ packets/burst.
- **Average Good Sojourn Length (AGSL):** $\text{AGSL} = \frac{1}{p}$ packets.

### Supported 5G RF Scenarios (`src/error_model/ChannelScenarios.hpp`)

| Scenario ID | Profile Name | $p$ ($G \to B$) | $q$ ($B \to G$) | $\pi_B$ (Theo FER) | ABEL (pkts) | Physical Environment |
| :--- | :--- | :---: | :---: | :---: | :---: | :--- |
| `URBAN_HIGH` | Urban High Dense | 0.10 | 0.50 | 16.67% | 2.0 | $>1000\text{ UEs/km}^2$, dense microcells |
| `URBAN_MED` | Urban Medium Dense | 0.05 | 0.20 | 20.00% | 5.0 | $\sim 500\text{ UEs/km}^2$, building shadowing |
| `URBAN_MILD` | Urban Mild Dense | 0.08 | 0.40 | 16.67% | 2.5 | $\sim 100\text{ UEs/km}^2$, vehicular Doppler |
| `RURAL_HIGH` | Rural High Dense | 0.01 | 0.90 | 1.10% | 1.1 | $\sim 50\text{ UEs/km}^2$, stationary IoT LoS |
| `RURAL_MED` | Rural Medium Dense | 0.04 | 0.10 | 28.57% | 10.0 | $\sim 20\text{ UEs/km}^2$, sparse BS coverage |
| `RURAL_LOW` | Rural Low Dense | 0.06 | 0.05 | 54.55% | 20.0 | $<5\text{ UEs/km}^2$, extreme terrain blockage |
| `CUSTOM` | Custom Severe (99%) | 0.99 | 0.01 | 99.00% | 100.0 | Extreme burst stress testing |
| `CLEAN` | Baseline Clean Link | 0.00 | 1.00 | 0.00% | 1.0 | Ideal wired/fiber control baseline |

---

## 4. Error Injection & RFC 768 Checksum Bypass

Implemented in `src/error_model/ErrorInjector.hpp` and hooked inside `RlsUdpTask::sendRlsPdu()` (`src/ue/rls/udp_task.cpp`):

1. **Control-Plane Safety Filter:** Only packets containing `5G_GE_`, `5G_RS:`, or `5G_TEST` are evaluated; 3GPP RRC, NGAP, and SCTP signaling remain untouched.
2. **RFC 768 UDP Checksum Bypass:** In IPv4, setting the 2-byte UDP checksum field to `0x0000` disables kernel-level checksum drops, allowing corrupted payloads to reach user-space AL-FEC.
3. **Dynamic Corruption Depth:** Scaled to the active scenario's fading severity:
   $$\text{baseCorrupt} = \text{round}\left(1.0 + 8.0 \cdot \pi_B + 0.15 \cdot (\text{ABEL} - 1.0)\right) + \text{jitter} \in \{0,1\}$$
4. **Visible Noise Injection:** Corrupts characters in the message body using printable noise (`#?*@!~$%&X123456789`) so both the terminal and web inspector clearly display raw vs. restored text.

---

## 5. Error Detection & Recovery Algorithms

Implemented from scratch in pure Python (`al-fec/fec_codec.py`) with zero external dependencies:

### Wire Frame Format
```text
+------------+--------------+--------------+-------------------------+------------------+
| "5G_RS:"   | Payload Len  | CRC-16-CCITT | Systematic Payload      | RS Parity        |
| (6 Bytes)  | N (2 Bytes)  | (2 Bytes)    | (N Bytes)               | (10 Bytes, 2t)   |
+------------+--------------+--------------+-------------------------+------------------+
```

### Galois Field $\text{GF}(2^8)$ & Encoding
- **Primitive Polynomial:** $p(x) = x^8 + x^4 + x^3 + x^2 + 1$ (`0x11D`), standard NASA/CCSDS/3GPP field.
- **Generator Polynomial:** $g(x) = \prod_{i=0}^{9} (x - \alpha^i)$ with $2t = 10$ parity symbols.
- **Correction Capability:** Maximum Distance Separable (MDS) bound of **$t = 5$ completely corrupted bytes/characters** per frame.

### 4-Stage Autonomous Decoding Pipeline
1. **Syndrome Calculation:** Evaluate $S_i = r(\alpha^i)$ for $i = 0 \dots 9$. If all $S_i = 0$, the frame is **CLEAN**.
2. **Berlekamp-Massey Algorithm:** Iteratively synthesizes the minimal-degree Error Locator Polynomial $\Lambda(x)$ of degree $v$. If $v > 5$, the burst exceeds MDS capacity (**UNCORRECTABLE**).
3. **Chien Search:** Evaluates $\Lambda(X_i^{-1}) = 0$ over all frame positions to locate exact corrupted byte indices.
4. **Forney Algorithm:** Computes exact error magnitudes and heals bytes via XOR:
   $$Y_k = \frac{X_k \cdot \Omega(X_k^{-1})}{\Lambda'(X_k^{-1})}, \qquad \text{recovered}[\text{pos}_k] \mathrel{\oplus}= Y_k$$
5. **CRC-16-CCITT Verification (`0x1021`, init `0xFFFF`):** Recomputed on the recovered payload to guarantee end-to-end integrity and catch any miscorrection when errors exceed $t = 5$.

---

## 6. Repository Structure

```text
├── src/
│   ├── error_model/
│   │   ├── MarkovChannelModel.hpp    # 2-state DTMC math (p, q, pi_B, ABEL)
│   │   ├── DropDecisionEngine.hpp    # RNG burst & sojourn tracker
│   │   ├── ChannelScenarios.hpp      # 8 empirical 5G RF scenario profiles
│   │   └── ErrorInjector.hpp         # RFC 768 checksum zeroing & visible noise injection
│   └── ue/rls/udp_task.cpp           # 5G NR RLS transmission hook
├── al-fec/
│   ├── fec_codec.py                  # Pure-Python GF(2^8) RS(N+10, N) & CRC-16-CCITT codec
│   ├── sender_traffic_gen.py         # RS-encoded UDP traffic generator (SO_NO_CHECK)
│   ├── error_detector.py             # Autonomous syndrome decoding & batch tracking
│   ├── metrics_engine.py             # FER, SER, RS efficiency, ABEL & convergence metrics
│   ├── packet_receiver.py            # Netns UDP receiver (:9999) + JSON telemetry writer
│   ├── telemetry_server.py           # HTTP server (:8080) with 40ms background IPC sync
│   ├── dashboard/web/index.html      # Live Web Dashboard & Message Corruption Inspector
│   ├── run_5g.sh                     # Automated tmux launcher (Core, gNB, 4 UEs, Receiver, Dashboard)
│   ├── send_5g.sh                    # Dynamic netns IP lookup & scenario test runner
│   ├── stop_5g.sh                    # Clean shutdown script
│   ├── register_subscribers.js       # MongoDB static IP bindings for UEs
│   └── 5G_GE_ReedSolomon_Presentation.pptx  # Project presentation deck
└── screenshots/                      # Project & testbed screenshots
```

---

## 7. How to Run

1. **Build UERANSIM:**
   ```bash
   cmake -B build && cmake --build build -j$(nproc)
   ```
2. **Launch the 5G SA Testbed (Open5GS + UERANSIM + Receiver + Dashboard):**
   ```bash
   ./run_5g.sh
   ```
3. **Open the Live Web Dashboard:**
   Navigate to `http://localhost:8080` in your browser.
4. **Run Channel Scenarios:**
   ```bash
   # Scenario 1: Urban High Dense (t <= 5, 100% RS Recovery)
   ./send_5g.sh ue3 ue1 1 30

   # Scenario 8: Baseline Clean Link (0% loss)
   ./send_5g.sh ue3 ue1 8 20

   # Scenario 6: Rural Low Dense (>5 errors, triggers UNCORRECTABLE & CRC: FAIL)
   ./send_5g.sh ue3 ue1 6 30
   ```

---

## 8. Experimental Results & Test Cases

| Test Case | Scenario | Theo $\pi_B$ | Corrupted Chars / Burst | Observed Outcome |
| :--- | :--- | :---: | :---: | :--- |
| **Baseline** | `CLEAN` (`8`) | 0.00% | 0 | 100% `CLEAN`, `CRC: PASS` |
| **Normal Fading** | `URBAN_HIGH` (`1`) | 16.67% | 2–3 ($\le 5$) | ~83% `CLEAN`, ~17% `RS RESTORED`, `CRC: PASS` |
| **Medium Bursts** | `URBAN_MED` (`2`) | 20.00% | 3–4 ($\le 5$) | Longer bursts (~5 pkts), 100% `RS RESTORED` |
| **IoT LoS** | `RURAL_HIGH` (`4`) | 1.10% | 1–2 ($\le 5$) | Almost all `CLEAN`, rare 1-char `RS RESTORED` |
| **MDS Limit Exceeded** | `RURAL_LOW` (`6`) | 54.55% | 7–9 ($> 5$) | Exceeds $t=5$ → `UNCORRECTABLE` + `CRC: FAIL` |
| **Extreme Stress** | `CUSTOM` (`7`) | 99.00% | 10+ ($> 5$) | ~99% `UNCORRECTABLE` + `CRC: FAIL` |

### Sample Live Receiver Output (`URBAN_HIGH`)
```text
[REED-SOLOMON RESTORED #014] from 10.45.0.4
  Raw Corrupted: '5G_GE_URBAN_HIGH [SEQ #014] HELLO PROFESSOR R@ED SOLOMON DEMO'
  RS Restored  : '5G_GE_URBAN_HIGH [SEQ #014] HELLO PROFESSOR REED SOLOMON DEMO' (Syndrome: 2 letters restored, CRC: PASS)
```

---

## 9. Screenshots

| Description | Screenshot |
| :--- | :--- |
| **5G SA Core & UERANSIM Registration** | `![UE Registration](screenshots/01_ue_registration.png)` |
| **Error Injector Telemetry (UE3 C++ RLS)** | `![Error Injector](screenshots/02_error_injector_console.png)` |
| **RS Recovery Output (Receiver Terminal)** | `![RS Recovery](screenshots/03_receiver_rs_restored.png)` |
| **Live Web Dashboard (Urban High)** | `![Dashboard Urban High](screenshots/04_dashboard_urban_high.png)` |
| **MDS Limit & CRC Fail (Rural Low)** | `![Dashboard Rural Low](screenshots/05_dashboard_rural_low_fail.png)` |

---

## 10. Conclusion & Future Scope

### Conclusion
- Successfully integrated a mathematically rigorous **2-State Gilbert-Elliott DTMC** into a live **Open5GS + UERANSIM 5G SA** testbed without disrupting 3GPP control-plane signaling.
- Solved Linux kernel UDP drop behavior via **RFC 768 checksum zeroing**, enabling real-time observation of burst errors at the application layer.
- Demonstrated autonomous, zero-cheat **Reed-Solomon $\text{RS}(N+10, N)$** error recovery over $\text{GF}(2^8)$ up to the MDS bound ($t=5$), validated by **CRC-16-CCITT** and a live telemetry dashboard.

### Future Scope
- Adaptive FEC redundancy ($t$ scaling dynamically based on observed $\pi_B$ / ABEL).
- Interleaving across consecutive UDP frames to survive longer `RURAL_LOW` bursts ($\text{ABEL} \ge 20$).
- Support for video/RTP media streaming over the 5G SA GE channel.
