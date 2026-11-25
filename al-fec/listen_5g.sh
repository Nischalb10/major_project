#!/bin/bash
# 5G Universal Receiver CLI Tool
# Launches the UDP receiver on any chosen UE namespace.

TARGET_UE="${1:-ue1}"
TARGET_UE="${TARGET_UE,,}" # Lowercase

case "$TARGET_UE" in
    ue1|1) NS="ueransim-999700000000001-internet-psi1"; LABEL="UE1 (10.45.0.2 on gNB1)" ;;
    ue2|2) NS="ueransim-999700000000002-internet-psi1"; LABEL="UE2 (10.45.0.3 on gNB2)" ;;
    ue3|3) NS="ueransim-999700000000003-internet-psi1"; LABEL="UE3 (10.45.0.4 on gNB1)" ;;
    ue4|4) NS="ueransim-999700000000004-internet-psi1"; LABEL="UE4 (10.45.0.5 on gNB2)" ;;
    *) echo "Usage: ./listen_5g.sh <ue1|ue2|ue3|ue4>"; exit 1 ;;
esac

echo ">>> Starting 5G UDP Receiver on $LABEL <<<"
echo 'nischal' | sudo -S -p '' ip netns exec "$NS" python3 /home/nischal/packet_receiver.py

