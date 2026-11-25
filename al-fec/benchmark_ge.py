#!/usr/bin/env python3
"""
Backwards-compatibility launcher: forwards directly to the modular sender_traffic_gen.py
"""
import sys
import os

if __name__ == "__main__":
    os.execv("/usr/bin/python3", ["python3", "/home/nischal/sender_traffic_gen.py"] + sys.argv[1:])
