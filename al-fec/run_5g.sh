#!/bin/bash

echo -n "Please enter your sudo password to automate the setup: "
read -s SUDO_PASS
echo ""

while ! echo "$SUDO_PASS" | sudo -S -v 2>/dev/null; do
    echo "Incorrect sudo password. Please re-enter your sudo password:"
    read -s SUDO_PASS
    echo ""
done

# Clean up any stuck processes and namespaces
tmux kill-session -t open5gs 2>/dev/null
echo "$SUDO_PASS" | sudo -S -p '' killall -9 open5gs-nrfd open5gs-scpd open5gs-amfd open5gs-smfd open5gs-upfd open5gs-ausfd open5gs-udmd open5gs-udrd open5gs-pcfd nr-gnb nr-ue node ncat nc tcpdump telemetry_server.py packet_receiver.py sender_traffic_gen.py 2>/dev/null

echo "$SUDO_PASS" | sudo -S -p '' ip netns del ueransim-999700000000001-internet-psi1 2>/dev/null
echo "$SUDO_PASS" | sudo -S -p '' ip netns del ueransim-999700000000002-internet-psi1 2>/dev/null
echo "$SUDO_PASS" | sudo -S -p '' ip netns del ueransim-999700000000003-internet-psi1 2>/dev/null
echo "$SUDO_PASS" | sudo -S -p '' ip netns del ueransim-999700000000004-internet-psi1 2>/dev/null
echo "$SUDO_PASS" | sudo -S -p '' ip netns del ueransim-imsi-999700000000001 2>/dev/null
echo "$SUDO_PASS" | sudo -S -p '' ip netns del ueransim-imsi-999700000000002 2>/dev/null
echo "$SUDO_PASS" | sudo -S -p '' ip netns del ueransim-imsi-999700000000003 2>/dev/null
echo "$SUDO_PASS" | sudo -S -p '' ip netns del ueransim-imsi-999700000000004 2>/dev/null

# Ensure MongoDB is running and register all 4 subscribers
echo "Ensuring MongoDB is active and subscribers are registered..."
echo "$SUDO_PASS" | sudo -S -p '' systemctl restart mongod 2>/dev/null
sleep 2
mongosh open5gs /home/nischal/register_subscribers.js 2>/dev/null || true

echo "Starting 5G Network with Gilbert-Elliott 2-State Markov Channel Emulation..."

# Start a new tmux session with mouse support enabled
tmux new-session -d -s open5gs -n "Core"
tmux set -g mouse on

# ==============================================================================
# WINDOW 0: Open5GS Core
# ==============================================================================
tmux send-keys -t open5gs:0 "cd /home/nischal/open5gs" C-m
tmux send-keys -t open5gs:0 "./install/bin/open5gs-nrfd & ./install/bin/open5gs-scpd & ./install/bin/open5gs-amfd & ./install/bin/open5gs-smfd & echo '$SUDO_PASS' | sudo -S -p '' ./install/bin/open5gs-upfd & ./install/bin/open5gs-ausfd & ./install/bin/open5gs-udmd & ./install/bin/open5gs-udrd & ./install/bin/open5gs-pcfd" C-m

tmux split-window -h -t open5gs:0
tmux send-keys -t open5gs:0.1 "cd /home/nischal/open5gs/webui" C-m
tmux send-keys -t open5gs:0.1 "npm run dev" C-m

# ==============================================================================
# WINDOW 1: Base Stations (gNB 1 and gNB 2)
# ==============================================================================
tmux new-window -t open5gs -n "gNBs"
tmux send-keys -t open5gs:1.0 "cd /home/nischal/UERANSIM" C-m
tmux send-keys -t open5gs:1.0 "sleep 3 && ./build/nr-gnb -c config/open5gs-gnb.yaml" C-m

tmux split-window -h -t open5gs:1.0
tmux send-keys -t open5gs:1.1 "cd /home/nischal/UERANSIM" C-m
tmux send-keys -t open5gs:1.1 "sleep 3 && ./build/nr-gnb -c config/open5gs-gnb2.yaml" C-m

# ==============================================================================
# WINDOW 2: User Equipments (4 UEs in a clean 2x2 grid)
# ==============================================================================
tmux new-window -t open5gs -n "UEs"

# Capture exact Pane IDs to guarantee proper 2x2 layout
PANE_UE1=$(tmux display-message -p -t open5gs:2 "#{pane_id}")
PANE_UE2=$(tmux split-window -h -t "$PANE_UE1" -P -F "#{pane_id}")
PANE_UE3=$(tmux split-window -v -t "$PANE_UE1" -P -F "#{pane_id}")
PANE_UE4=$(tmux split-window -v -t "$PANE_UE2" -P -F "#{pane_id}")

# Top-Left: UE 1 (gNB 1, IMSI 1, IP: 10.45.0.2)
tmux send-keys -t "$PANE_UE1" "cd /home/nischal/UERANSIM" C-m
tmux send-keys -t "$PANE_UE1" "sleep 7 && echo '$SUDO_PASS' | sudo -S -p '' ./build/nr-ue -c config/open5gs-ue.yaml" C-m

# Top-Right: UE 2 (gNB 2, IMSI 2, IP: 10.45.0.3)
tmux send-keys -t "$PANE_UE2" "cd /home/nischal/UERANSIM" C-m
tmux send-keys -t "$PANE_UE2" "sleep 9 && echo '$SUDO_PASS' | sudo -S -p '' ./build/nr-ue -c config/open5gs-ue2.yaml" C-m

# Bottom-Left: UE 3 (gNB 1, IMSI 3, IP: 10.45.0.4)
tmux send-keys -t "$PANE_UE3" "cd /home/nischal/UERANSIM" C-m
tmux send-keys -t "$PANE_UE3" "sleep 11 && echo '$SUDO_PASS' | sudo -S -p '' ./build/nr-ue -c config/open5gs-ue3.yaml" C-m

# Bottom-Right: UE 4 (gNB 2, IMSI 4, IP: 10.45.0.5)
tmux send-keys -t "$PANE_UE4" "cd /home/nischal/UERANSIM" C-m
tmux send-keys -t "$PANE_UE4" "sleep 13 && echo '$SUDO_PASS' | sudo -S -p '' ./build/nr-ue -c config/open5gs-ue4.yaml" C-m

# ==============================================================================
# WINDOW 3: Test & Gilbert-Elliott Metrics Evaluation
# ==============================================================================
tmux new-window -t open5gs -n "Test"

# Left Pane: Error Detection & Metrics Engine on UE 1 (10.45.0.2:9999)
tmux send-keys -t open5gs:3.0 "sleep 18 && echo '$SUDO_PASS' | sudo -S -p '' ip netns exec ueransim-999700000000001-internet-psi1 python3 /home/nischal/packet_receiver.py" C-m

# Right Pane: Interactive Sender Prompt with Scenario Presets
tmux split-window -h -t open5gs:3.0
tmux send-keys -t open5gs:3.1 "echo '================================================================='" C-m
tmux send-keys -t open5gs:3.1 "echo '  GILBERT-ELLIOTT 2-STATE MARKOV ERROR MODEL - 5G TESTBED'" C-m
tmux send-keys -t open5gs:3.1 "echo '================================================================='" C-m
tmux send-keys -t open5gs:3.1 "echo 'Topology: gNB1 [UE1: 10.45.0.2, UE3: 10.45.0.4] | gNB2 [UE2: 10.45.0.3, UE4: 10.45.0.5]'" C-m
tmux send-keys -t open5gs:3.1 "echo 'Live Web Dashboard: http://localhost:8080 (Open in Browser)'" C-m
tmux send-keys -t open5gs:3.1 "echo ''" C-m
tmux send-keys -t open5gs:3.1 "echo '--- [DEMO 1: INTRA-gNB (UE3 -> UE1 via gNB1)] ---'" C-m
tmux send-keys -t open5gs:3.1 "echo '/home/nischal/send_5g.sh ue3 ue1 1 30'" C-m
tmux send-keys -t open5gs:3.1 "echo ''" C-m
tmux send-keys -t open5gs:3.1 "echo '--- [DEMO 2: INTER-gNB (UE2 -> UE1 via gNB2 -> Core UPF -> gNB1)] ---'" C-m
tmux send-keys -t open5gs:3.1 "echo '/home/nischal/send_5g.sh ue2 ue1 1 30'" C-m
tmux send-keys -t open5gs:3.1 "echo ''" C-m
tmux send-keys -t open5gs:3.1 "echo '--- [SCENARIO SELECTOR: /home/nischal/send_5g.sh <FROM> <TO> <SCENARIO> <COUNT>] ---'" C-m
tmux send-keys -t open5gs:3.1 "echo '  1: Urban High Dense   (p=0.10, q=0.50 | PER: 16.7% | ABEL:   2.0 pkts)'" C-m
tmux send-keys -t open5gs:3.1 "echo '  2: Urban Med Dense    (p=0.05, q=0.20 | PER: 20.0% | ABEL:   5.0 pkts)'" C-m
tmux send-keys -t open5gs:3.1 "echo '  3: Urban Mild Dense   (p=0.08, q=0.40 | PER: 16.7% | ABEL:   2.5 pkts)'" C-m
tmux send-keys -t open5gs:3.1 "echo '  4: Rural High Dense   (p=0.01, q=0.90 | PER:  1.1% | ABEL:   1.1 pkts)'" C-m
tmux send-keys -t open5gs:3.1 "echo '  5: Rural Med Dense    (p=0.04, q=0.10 | PER: 28.6% | ABEL:  10.0 pkts)'" C-m
tmux send-keys -t open5gs:3.1 "echo '  6: Rural Low Dense    (p=0.06, q=0.05 | PER: 54.5% | ABEL:  20.0 pkts)'" C-m
tmux send-keys -t open5gs:3.1 "echo '  7: Baseline Clean     (p=0.00, q=1.00 | PER:  0.0% | ABEL:   1.0 pkt )'" C-m
tmux send-keys -t open5gs:3.1 "echo '  8: Custom Severe 99%  (p=0.99, q=0.01 | PER: 99.0% | ABEL: 100.0 pkts)'" C-m
tmux send-keys -t open5gs:3.1 "echo ''" C-m
tmux send-keys -t open5gs:3.1 "sleep 20" C-m

# ==============================================================================
# WINDOW 4: Dedicated Telemetry Web Dashboard Server (Port 8080)
# ==============================================================================
tmux new-window -t open5gs -n "Dashboard"
tmux send-keys -t open5gs:4 "python3 /home/nischal/telemetry_server.py" C-m

# Select the Test window as the starting window
tmux select-window -t open5gs:3

# Attach to the session
tmux attach-session -t open5gs

