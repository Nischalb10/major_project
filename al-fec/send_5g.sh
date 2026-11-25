#!/bin/bash
# 5G Universal Transmission CLI Tool
# Allows sending Gilbert-Elliott test bursts between ANY two UEs across the network.

FROM_UE="${1,,}" # Lowercase
TO_UE="${2,,}"   # Lowercase
SCENARIO="${3:-1}"
COUNT="${4:-30}"
CUSTOM_MSG="${5:-}"

if [ -z "$FROM_UE" ] || [ -z "$TO_UE" ]; then
    echo "========================================================================="
    echo " 5G UNIVERSAL SENDER CLI (with Systematic FEC & CRC-16)"
    echo "========================================================================="
    echo " Usage  : ./send_5g.sh <FROM_UE> <TO_UE> [SCENARIO] [COUNT] [CUSTOM_MSG]"
    echo " Example: ./send_5g.sh ue3 ue1 1 30   (UE3 -> UE1, Urban High, 30 frames)"
    echo " Example: ./send_5g.sh ue3 ue1 1 10 \"HELLO PROFESSOR 5G TEST\""
    echo " Example: ./send_5g.sh ue2 ue1 8 20   (UE2 -> UE1, Custom 99% Severe Fading)"
    echo ""
    echo " Scenarios (1-8):"
    echo "   1: Urban High Dense  (p=0.10, q=0.50 | PER: 16.7% | ABEL:  2.0 pkts)"
    echo "   2: Urban Med Dense   (p=0.05, q=0.20 | PER: 20.0% | ABEL:  5.0 pkts)"
    echo "   3: Urban Mild Dense  (p=0.08, q=0.40 | PER: 16.7% | ABEL:  2.5 pkts)"
    echo "   4: Rural High Dense  (p=0.01, q=0.90 | PER:  1.1% | ABEL:  1.1 pkts)"
    echo "   5: Rural Med Dense   (p=0.04, q=0.10 | PER: 28.6% | ABEL: 10.0 pkts)"
    echo "   6: Rural Low Dense   (p=0.06, q=0.05 | PER: 54.5% | ABEL: 20.0 pkts)"
    echo "   7: Baseline Clean    (p=0.00, q=1.00 | PER:  0.0% | ABEL:  1.0 pkt )"
    echo "   8: Custom Severe 99% (p=0.99, q=0.01 | PER: 99.0% | ABEL: 100.0 pkts)"
    echo "========================================================================="
    exit 1
fi

case "$FROM_UE" in
    ue1|1) NS="ueransim-999700000000001-internet-psi1"; FROM_LABEL="UE1 (10.45.0.2 on gNB1)" ;;
    ue2|2) NS="ueransim-999700000000002-internet-psi1"; FROM_LABEL="UE2 (10.45.0.3 on gNB2)" ;;
    ue3|3) NS="ueransim-999700000000003-internet-psi1"; FROM_LABEL="UE3 (10.45.0.4 on gNB1)" ;;
    ue4|4) NS="ueransim-999700000000004-internet-psi1"; FROM_LABEL="UE4 (10.45.0.5 on gNB2)" ;;
    *) echo "Error: Invalid source UE '$FROM_UE'. Choose ue1, ue2, ue3, or ue4."; exit 1 ;;
esac

case "$TO_UE" in
    ue1|1) TO_NS="ueransim-999700000000001-internet-psi1"; TO_LABEL="UE1"; FALLBACK_IP="10.45.0.2" ;;
    ue2|2) TO_NS="ueransim-999700000000002-internet-psi1"; TO_LABEL="UE2"; FALLBACK_IP="10.45.0.3" ;;
    ue3|3) TO_NS="ueransim-999700000000003-internet-psi1"; TO_LABEL="UE3"; FALLBACK_IP="10.45.0.4" ;;
    ue4|4) TO_NS="ueransim-999700000000004-internet-psi1"; TO_LABEL="UE4"; FALLBACK_IP="10.45.0.5" ;;
    *) echo "Error: Invalid target UE '$TO_UE'. Choose ue1, ue2, ue3, or ue4."; exit 1 ;;
esac

# Dynamically resolve the actual assigned IP address from the destination namespace
DEST_IP=$(echo 'nischal' | sudo -S -p '' ip netns exec "$TO_NS" ip -4 addr show dev uesimtun0 2>/dev/null | grep -oP '(?<=inet\s)\d+(\.\d+){3}')
if [ -z "$DEST_IP" ]; then
    DEST_IP="$FALLBACK_IP"
fi

echo ">>> Transmitting from $FROM_LABEL -> $TO_LABEL (IP: $DEST_IP) <<<"
if [ -n "$CUSTOM_MSG" ]; then
    echo 'nischal' | sudo -S -p '' ip netns exec "$NS" python3 /home/nischal/sender_traffic_gen.py --dest "$DEST_IP" --scenario "$SCENARIO" --count "$COUNT" --msg "$CUSTOM_MSG"
else
    echo 'nischal' | sudo -S -p '' ip netns exec "$NS" python3 /home/nischal/sender_traffic_gen.py --dest "$DEST_IP" --scenario "$SCENARIO" --count "$COUNT"
fi

