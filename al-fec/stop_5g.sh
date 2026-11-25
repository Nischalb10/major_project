#!/bin/bash

# ==============================================================================
# Open5GS + UERANSIM Clean Stop Script
# ==============================================================================

echo "[*] Stopping Open5GS and UERANSIM..."

# 1. Kill tmux session
tmux kill-session -t open5gs 2>/dev/null && echo "[+] Closed tmux session 'open5gs'" || echo "[-] No active 'open5gs' tmux session"

# 2. Terminate 5G daemons
sudo killall -9 open5gs-nrfd open5gs-scpd open5gs-amfd open5gs-smfd open5gs-upfd open5gs-ausfd open5gs-udmd open5gs-udrd open5gs-pcfd nr-gnb nr-ue telemetry_server.py packet_receiver.py sender_traffic_gen.py 2>/dev/null || \
killall -9 open5gs-nrfd open5gs-scpd open5gs-amfd open5gs-smfd open5gs-upfd open5gs-ausfd open5gs-udmd open5gs-udrd open5gs-pcfd nr-gnb nr-ue telemetry_server.py packet_receiver.py sender_traffic_gen.py 2>/dev/null || true

# 3. Terminate WebUI node process
pkill -f "open5gs.*webui" 2>/dev/null || true
pkill -f "npm run dev" 2>/dev/null || true

echo "[+] All Open5GS Core, UERANSIM, and WebUI processes stopped."
