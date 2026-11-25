#!/usr/bin/env python3
"""
5G Performance & Metrics Subsystem (Reed-Solomon AL-FEC QoS Evaluation)
Aggregates packet detection events, computes empirical symbol/frame error rates,
measures Reed-Solomon recovery efficiency, and tracks Markov convergence.
"""

import time
from collections import Counter

SCENARIO_PROFILES = {
    "5G_GE_URBAN_HIGH": {
        "name": "Urban High Dense",
        "p": 0.10, "q": 0.50,
        "desc": ">1000 UEs/km^2, dense microcells, frequent collisions"
    },
    "5G_GE_URBAN_MED": {
        "name": "Urban Medium Dense",
        "p": 0.05, "q": 0.20,
        "desc": "~500 UEs/km^2, building shadowing"
    },
    "5G_GE_URBAN_MILD": {
        "name": "Urban Mild Dense",
        "p": 0.08, "q": 0.40,
        "desc": "~100 UEs/km^2, fast vehicular fading"
    },
    "5G_GE_RURAL_HIGH": {
        "name": "Rural High Dense",
        "p": 0.01, "q": 0.90,
        "desc": "~50 UEs/km^2, stationary IoT, line-of-sight"
    },
    "5G_GE_RURAL_MED": {
        "name": "Rural Medium Dense",
        "p": 0.04, "q": 0.10,
        "desc": "~20 UEs/km^2, sparse coverage bursts"
    },
    "5G_GE_RURAL_LOW": {
        "name": "Rural Low Dense",
        "p": 0.06, "q": 0.05,
        "desc": "<5 UEs/km^2, extreme range fading, prolonged outage"
    },
    "5G_GE_CUSTOM": {
        "name": "Custom Severe Fading",
        "p": 0.99, "q": 0.01,
        "desc": "User-defined extreme degradation (99% fading rate)"
    },
    "5G_GE_CLEAN": {
        "name": "Baseline Clean Link",
        "p": 0.00, "q": 1.00,
        "desc": "Ideal channel with 0% loss (control baseline)"
    }
}

class MetricsEngine:
    def __init__(self):
        self.reset()

    def reset(self):
        self.scenario_tag = "5G_GE_URBAN_HIGH"
        self.total_frames = 0
        self.clean_frames = 0
        self.corrected_frames = 0
        self.uncorrectable_frames = 0
        self.total_symbols_corrupted = 0
        self.total_symbols_corrected = 0
        self.total_symbols_evaluated = 0
        self.burst_lengths = []
        self.current_burst = 0
        self.message_history = []
        self.bytes_received = 0
        self.start_time = time.time()

    def record_packet_event(self, detection_result):
        if detection_result.get("type") != "sequenced":
            return

        scenario_tag = detection_result["scenario_tag"]
        if detection_result.get("is_new_batch") or scenario_tag != self.scenario_tag:
            self.reset()
            self.scenario_tag = scenario_tag

        now = time.time()
        seq = detection_result["seq"]
        status = detection_result["status"]
        sym_err = detection_result.get("symbols_corrupted", 0)
        sym_fix = detection_result.get("symbols_corrected", 0)
        raw_text = detection_result.get("raw_corrupted_text", "")
        fixed_text = detection_result.get("corrected_text", "")
        crc_valid = detection_result.get("crc_valid", True)
        frame_bytes = detection_result.get("frame_size_bytes", 64)

        self.total_frames += 1
        self.bytes_received += frame_bytes
        self.total_symbols_evaluated += frame_bytes
        self.total_symbols_corrupted += sym_err
        self.total_symbols_corrected += sym_fix

        if status == "CLEAN":
            self.clean_frames += 1
            if self.current_burst > 0:
                self.burst_lengths.append(self.current_burst)
                self.current_burst = 0
        elif status == "CORRECTED":
            self.corrected_frames += 1
            self.current_burst += 1
        else: # UNCORRECTABLE
            self.uncorrectable_frames += 1
            self.current_burst += 1

        self.message_history.append({
            "seq": seq,
            "raw_text": raw_text,
            "fixed_text": fixed_text,
            "status": status,
            "symbols_corrupted": sym_err,
            "symbols_corrected": sym_fix,
            "crc_valid": crc_valid,
            "time": now
        })
        if len(self.message_history) > 60:
            self.message_history.pop(0)

    def get_snapshot(self):
        profile = SCENARIO_PROFILES.get(self.scenario_tag, SCENARIO_PROFILES["5G_GE_URBAN_HIGH"])
        p = profile["p"]
        q = profile["q"]

        theo_pi_B = (p / (p + q)) if (p + q) > 0 else 0.0
        theo_abel = (1.0 / q) if q > 0 else 0.0

        corrupted_frames = self.corrected_frames + self.uncorrectable_frames
        emp_fer = ((corrupted_frames / self.total_frames) * 100.0) if self.total_frames > 0 else 0.0
        ser = ((self.total_symbols_corrupted / self.total_symbols_evaluated) * 100.0) if self.total_symbols_evaluated > 0 else 0.0

        correction_rate = ((self.corrected_frames / corrupted_frames) * 100.0) if corrupted_frames > 0 else 100.0

        all_bursts = list(self.burst_lengths)
        if self.current_burst > 0:
            all_bursts.append(self.current_burst)

        total_burst_count = len(all_bursts)
        emp_abel = (sum(all_bursts) / total_burst_count) if total_burst_count > 0 else 0.0
        max_burst = max(all_bursts) if all_bursts else 0

        # Convergence
        if theo_pi_B > 0:
            diff = abs((emp_fer / 100.0) - theo_pi_B)
            convergence = max(0.0, (1.0 - (diff / theo_pi_B)) * 100.0)
        else:
            convergence = 100.0 if emp_fer == 0 else 0.0

        burst_histogram = dict(Counter(all_bursts))

        elapsed = max(0.1, time.time() - self.start_time)
        goodput_kbps = ((self.bytes_received * 8) / elapsed) / 1000.0

        return {
            "scenario_tag": self.scenario_tag,
            "scenario_name": profile["name"],
            "scenario_desc": profile["desc"],
            "p": p,
            "q": q,
            "theo_per": round(theo_pi_B * 100.0, 2),
            "theo_abel": round(theo_abel, 2),
            "total_sent": self.total_frames,
            "clean_frames": self.clean_frames,
            "corrected_frames": self.corrected_frames,
            "uncorrectable_frames": self.uncorrectable_frames,
            "total_delivered": self.clean_frames + self.corrected_frames,
            "total_dropped": self.uncorrectable_frames,
            "total_bit_errors": self.total_symbols_corrupted,
            "total_bit_corrected": self.total_symbols_corrected,
            "ber": round(ser, 3), # Symbol Error Rate (SER)
            "empirical_per": round(emp_fer, 2),
            "correction_rate": round(correction_rate, 1),
            "total_bursts": total_burst_count,
            "empirical_abel": round(emp_abel, 2),
            "max_burst": max_burst,
            "convergence": round(convergence, 1),
            "burst_histogram": burst_histogram,
            "goodput_kbps": round(goodput_kbps, 2),
            "recent_messages": self.message_history[-30:]
        }
