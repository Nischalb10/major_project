#!/usr/bin/env python3
"""
Backwards-compatibility launcher: forwards directly to the modular packet_receiver.py
"""
import sys
import os

if __name__ == "__main__":
    os.execv("/usr/bin/python3", ["python3", "/home/nischal/packet_receiver.py"] + sys.argv[1:])
