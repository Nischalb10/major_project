#!/usr/bin/env python3
"""
5G Sender Traffic Generation Subsystem (Reed-Solomon MDS Forward Error Correction)
Encodes user telemetry and text messages with CRC-16-CCITT and Systematic Reed-Solomon RS(N+10, N)
over Galois Field GF(2^8) as specified in the 5G Topology & Industrial Control research paper.
"""

import sys
import time
import socket
import argparse
import json
import urllib.request

from fec_codec import FecCodec

SCENARIOS = {
    "1": ("5G_GE_URBAN_HIGH", "Urban High Dense",   0.10, 0.50, ">1000 UEs/km^2 (Dense microcells, frequent collisions)"),
    "2": ("5G_GE_URBAN_MED",  "Urban Medium Dense", 0.05, 0.20, "~500 UEs/km^2 (Moderate bursts, building shadowing)"),
    "3": ("5G_GE_URBAN_MILD", "Urban Mild Dense",   0.08, 0.40, "~100 UEs/km^2 (Fast vehicular fading, brief severe bursts)"),
    "4": ("5G_GE_RURAL_HIGH", "Rural High Dense",   0.01, 0.90, "~50 UEs/km^2 (Stationary IoT, rare isolated drops)"),
    "5": ("5G_GE_RURAL_MED",  "Rural Medium Dense", 0.04, 0.10, "~20 UEs/km^2 (Agricultural IoT, sparse coverage bursts)"),
    "6": ("5G_GE_RURAL_LOW",  "Rural Low Dense",    0.06, 0.05, "<5 UEs/km^2 (Extreme range fading, prolonged outage)"),
    "7": ("5G_GE_CLEAN",      "Baseline Clean",     0.00, 1.00, "Ideal link with 0% loss (control baseline)"),
    "8": ("5G_GE_CUSTOM",     "Custom Severe (99%)",0.99, 0.01, "Custom degradation configured for 99% fading rate"),
}

UE_MAP = {
    "ue1": "10.45.0.2",
    "ue2": "10.45.0.3",
    "ue3": "10.45.0.4",
    "ue4": "10.45.0.5",
}

SAMPLE_MESSAGES = [
    "HELLO 5G WORLD: SENSOR_PRESSURE=101.3KPA",
    "ROBOTIC_ARM_TELEMETRY: MOTOR_CURRENT=4.2A",
    "V2X_SAFETY_CRITICAL: BRAKE_ALERT_DISTANCE=12M",
    "SMART_GRID_STATUS: VOLTAGE=230V_FREQ=50.0HZ",
    "AUTONOMOUS_VEHICLE: STEERING_ANGLE=+2.4DEG",
    "INDUSTRIAL_IOT_DATA: VIBRATION_PEAK=0.88G",
    "DRONE_SURVEILLANCE: ALTITUDE=45M_GPS_LOCK=OK",
]

def notify_dashboard_reset():
    try:
        req = urllib.request.Request("http://127.0.0.1:8080/api/reset", data=b'{}', headers={'Content-Type': 'application/json'}, method='POST')
        with urllib.request.urlopen(req, timeout=0.05):
            pass
    except Exception:
        pass

def main():
    parser = argparse.ArgumentParser(description="5G Sender Traffic Generator with Reed-Solomon AL-FEC")
    parser.add_argument("--scenario", choices=["1", "2", "3", "4", "5", "6", "7", "8"], default="1",
                        help="1:UrbanHigh, 2:UrbanMed, 3:UrbanMild, 4:RuralHigh, 5:RuralMed, 6:RuralLow, 7:Clean, 8:Custom99%")
    parser.add_argument("--count", type=int, default=30, help="Number of frames to send (default: 30)")
    parser.add_argument("--delay", type=float, default=0.15, help="Inter-frame interval in seconds (default: 0.15s)")
    parser.add_argument("--dest", type=str, default="ue1", help="Target IP or UE name (ue1, ue2, ue3, ue4)")
    parser.add_argument("--port", type=int, default=9999, help="Target UDP port (default: 9999)")
    parser.add_argument("--msg", type=str, default=None, help="Custom message string to transmit")

    args = parser.parse_args()

    dest_key = args.dest.lower()
    target_ip = UE_MAP.get(dest_key, args.dest)

    tag, name, p, q, desc = SCENARIOS[args.scenario]
    theo_pi_b = (p / (p + q)) if (p + q) > 0 else 0.0
    theo_abel = (1.0 / q) if q > 0 else 0.0
    exp_fades = round(args.count * theo_pi_b)
    exp_clean = args.count - exp_fades

    # Notify live dashboard to start a fresh sequence
    notify_dashboard_reset()

    print("\n=========================================================================")
    print(f" [5G SENDER SUBSYSTEM] REED-SOLOMON AL-FEC TRANSMISSION STREAM")
    print("=========================================================================")
    print(f" Profile Selected    : [{args.scenario}] {name}")
    print(f" Propagation Context : {desc}")
    print(f" Transition p (G->B) : {p:.4f}  (Mean Good Sojourn: {1.0/p if p > 0 else 0:.1f} frames)")
    print(f" Recovery   q (B->G) : {q:.4f}  (Mean Bad Sojourn : {theo_abel:.1f} frames)")
    print("-------------------------------------------------------------------------")
    print(" THEORETICAL MARKOV PREDICTIONS:")
    print(f"   Stationary Bad State Prob (pi_B / FER) : {theo_pi_b * 100.0:.1f} %")
    print(f"   Average Burst Error Length (ABEL = 1/q): {theo_abel:.1f} frames/burst")
    print(f"   Expected for {args.count} frames: ~{exp_fades} corrupted, ~{exp_clean} clean")
    print("-------------------------------------------------------------------------")
    print(" ERROR CONTROL ARCHITECTURE (RESEARCH PAPER SECTION 3):")
    print("   Error Detection   : Autonomous CRC-16-CCITT Polynomial Division")
    print("   Forward Correction: Systematic Reed-Solomon over GF(2^8) (t = 5 symbols)")
    print(f"   Target Endpoint   : {target_ip}:{args.port} ({args.dest.upper()}) at {args.delay}s interval")
    print("   Live Web Dashboard: http://localhost:8080")
    print("=========================================================================\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # Disable kernel UDP checksum generation (RFC 768 / SO_NO_CHECK)
    # Allows physical fading channel corruptions to reach Application-Layer FEC (Reed-Solomon)
    # instead of being discarded by the receiver's Linux kernel network stack
    SO_NO_CHECK = 11
    try:
        sock.setsockopt(socket.SOL_SOCKET, SO_NO_CHECK, 1)
    except Exception:
        pass

    start_time = time.time()
    for i in range(1, args.count + 1):
        if args.msg:
            base_text = args.msg
        else:
            base_text = SAMPLE_MESSAGES[(i - 1) % len(SAMPLE_MESSAGES)]

        raw_payload = f"{tag} [SEQ #{i:03d}] {base_text}"
        
        # Apply Systematic Reed-Solomon RS(N+10, N) + CRC-16 Encoding
        rs_frame = FecCodec.encode(raw_payload)

        sock.sendto(rs_frame, (target_ip, args.port))
        print(f" -> Sent Frame #{i:03d}/{args.count:03d} | '{base_text[:38]}...'")
        time.sleep(args.delay)

    duration = time.time() - start_time
    sock.close()

    print("\n=========================================================================")
    print(" [5G SENDER SUBSYSTEM] TRANSMISSION COMPLETE")
    print("=========================================================================")
    print(f" Total Sent       : {args.count} RS-encoded frames in {duration:.2f} seconds")
    print(f" Target Endpoint  : {target_ip}:{args.port} ({args.dest.upper()})")
    print(f" Active Scenario  : {name} (p={p:.4f}, q={q:.4f})")
    print("-------------------------------------------------------------------------")
    print(" VERIFICATION:")
    print(" 1. Receiver Terminal : Review Reed-Solomon syndrome decoding & letter recovery.")
    print(" 2. Live Web Dashboard: Open http://localhost:8080 for live character comparison.")
    print("=========================================================================\n")

if __name__ == "__main__":
    main()
