#!/usr/bin/env python3
"""
5G Standalone Packet Receiver Subsystem (Reed-Solomon AL-FEC Decoding & Recovery)
Executes inside the receiver UE namespace (e.g. UE 1: 10.45.0.2:9999).
Integrates:
1. Autonomous Reed-Solomon Syndrome Decoding over GF(2^8) (t = 5 symbols)
2. CRC-16 Polynomial Integrity Validation (Zero Global Cheating)
3. Shared-Filesystem Telemetry IPC for Real-Time Live Dashboard
"""

import socket
import sys
import time
import json
import urllib.request
import urllib.error
import os

from error_detector import ErrorDetector
from metrics_engine import MetricsEngine

TELEMETRY_FILE = "/tmp/5g_telemetry.json"
TELEMETRY_API = "http://127.0.0.1:8080/api/telemetry"

def push_telemetry_async(snapshot):
    # Direct shared-filesystem IPC (Works across all Linux network namespaces)
    try:
        with open(TELEMETRY_FILE, "w") as f:
            json.dump(snapshot, f)
            f.flush()
        try:
            os.chmod(TELEMETRY_FILE, 0o666)
        except Exception:
            pass
    except Exception:
        pass

    # 2. HTTP fallback
    try:
        data = json.dumps(snapshot).encode('utf-8')
        req = urllib.request.Request(TELEMETRY_API, data=data, headers={'Content-Type': 'application/json'}, method='POST')
        with urllib.request.urlopen(req, timeout=0.05):
            pass
    except Exception:
        pass

def main():
    port = 9999
    detector = ErrorDetector()
    metrics = MetricsEngine()

    # Publish baseline state to dashboard immediately
    push_telemetry_async(metrics.get_snapshot())

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", port))

    print("=========================================================================")
    print(f" [5G RECEIVER SUBSYSTEM ACTIVE] Listening on UDP Port {port}")
    print(" Architecture    : Autonomous Reed-Solomon MDS Decoding & Forney Recovery")
    print(" Research Paper  : Adaptive AL-FEC & 2-State Markov Burst Loss Emulation")
    print(" Error Detection : Autonomous CRC-16-CCITT Polynomial Division")
    print(" Error Correction: Reed-Solomon RS(N+10, N) over GF(2^8) (t = 5 letters)")
    print(" Web Dashboard   : http://localhost:8080")
    print("=========================================================================\n")
    sys.stdout.flush()

    last_report_time = time.time()
    packet_in_batch = 0

    while True:
        try:
            data, addr = sock.recvfrom(4096)
            now = time.time()

            # Process packet through Error Detection & Reed-Solomon Correction Engine
            event = detector.process_packet(data, addr)

            # Check if this is a new test batch after an idle period (>2.0s)
            if now - last_report_time > 2.0 and packet_in_batch > 0:
                print("-------------------------------------------------------------------------")
                snap = metrics.get_snapshot()
                corr_total = snap['corrected_frames'] + snap['uncorrectable_frames']
                print(f" [BATCH SUMMARY] Profile: {snap['scenario_name']} | Messages: {snap['total_sent']}")
                print(f"   Clean: {snap['clean_frames']} | Corrupted: {corr_total} ({snap['empirical_per']}% FER)")
                print(f"   RS Restored: {snap['corrected_frames']} | Uncorrectable: {snap['uncorrectable_frames']}")
                print(f"   Total Corrupted Chars: {snap['total_bit_errors']} | Chars Fixed: {snap['total_bit_corrected']}")
                print(f"   RS Recovery Efficiency: {snap['correction_rate']}% | Convergence: {snap['convergence']}%")
                print("-------------------------------------------------------------------------\n")
                packet_in_batch = 0

            # Feed detection result to the Metrics Engine
            metrics.record_packet_event(event)
            packet_in_batch += 1
            last_report_time = now

            # Console output with letter-level visual feedback
            if event.get("type") == "sequenced":
                seq = event["seq"]
                status = event.get("status")
                raw_txt = event.get("raw_corrupted_text", "")
                fix_txt = event.get("corrected_text", "")
                sym_err = event.get("symbols_corrupted", 0)

                if status == "CORRECTED":
                    print(f"\033[1;33m [REED-SOLOMON RESTORED #{seq:03d}]\033[0m from {addr[0]}")
                    print(f"   Raw Corrupted: '\033[31m{raw_txt}\033[0m'")
                    print(f"   RS Restored  : '\033[32m{fix_txt}\033[0m' (Syndrome: {sym_err} letters restored, CRC: PASS)")
                elif status == "UNCORRECTABLE":
                    print(f"\033[1;31m [UNCORRECTABLE BURST #{seq:03d}]\033[0m from {addr[0]}")
                    print(f"   Raw Corrupted: '\033[31m{raw_txt}\033[0m' (Burst exceeds t=5, CRC-16: FAIL)")
                else:
                    print(f"\033[1;32m [RCV CLEAN #{seq:03d}]\033[0m from {addr[0]} -> '{fix_txt}' (CRC: PASS)")
            else:
                print(f" [RCV UNENCODED] from {addr[0]} -> {event.get('raw_text', '')}")

            sys.stdout.flush()

            # Push telemetry snapshot to web dashboard
            push_telemetry_async(metrics.get_snapshot())

        except KeyboardInterrupt:
            print("\nReceiver stopped by user.")
            break
        except Exception as e:
            print(f"Error in receiver loop: {e}", file=sys.stderr)

    sock.close()

if __name__ == "__main__":
    main()
