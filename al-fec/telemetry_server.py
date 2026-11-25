#!/usr/bin/env python3
"""
5G Telemetry & Visualization Server
Lightweight HTTP/REST service running on port 8080 (zero external pip packages).
Ingests live metrics from the receiver subsystem and serves the real-time web dashboard.
"""

import sys
import os
import json
import time
import threading
from http.server import HTTPServer, SimpleHTTPRequestHandler
from socketserver import ThreadingMixIn

PORT = 8080
WEB_DIR = "/home/nischal/dashboard/web"

class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True

class TelemetryDataStore:
    def __init__(self):
        self.lock = threading.Lock()
        self.reset()

    def reset(self):
        global LAST_MTIME
        with self.lock:
            self.current_metrics = {
                "scenario_tag": "5G_GE_URBAN_HIGH",
                "scenario_name": "Urban High Dense",
                "scenario_desc": ">1000 UEs/km^2, dense microcells, frequent collisions",
                "p": 0.10,
                "q": 0.50,
                "theo_per": 16.67,
                "theo_abel": 2.0,
                "total_sent": 0,
                "clean_frames": 0,
                "corrected_frames": 0,
                "uncorrectable_frames": 0,
                "total_delivered": 0,
                "total_dropped": 0,
                "total_bit_errors": 0,
                "total_bit_corrected": 0,
                "ber": 0.0,
                "empirical_per": 0.0,
                "correction_rate": 100.0,
                "total_bursts": 0,
                "empirical_abel": 0.0,
                "max_burst": 0,
                "convergence": 100.0,
                "burst_histogram": {},
                "goodput_kbps": 0.0,
                "recent_messages": []
            }
            self.history = []
            LAST_MTIME = 0

    def update(self, snapshot):
        with self.lock:
            new_sent = snapshot.get("total_sent", 0)
            old_sent = self.current_metrics.get("total_sent", 0)
            new_tag = snapshot.get("scenario_tag")
            old_tag = self.current_metrics.get("scenario_tag")

            # Clean start for a new test batch or scenario switch
            if new_sent < old_sent or (new_tag and new_tag != old_tag and new_sent <= 1):
                self.history = []

            self.current_metrics.update(snapshot)
            if new_sent > 0:
                self.history.append({
                    "time": round(time.time(), 2),
                    "sent": new_sent,
                    "clean": snapshot.get("clean_frames", 0),
                    "corrected": snapshot.get("corrected_frames", 0),
                    "uncorrectable": snapshot.get("uncorrectable_frames", 0),
                    "emp_per": snapshot.get("empirical_per", 0.0),
                    "theo_per": snapshot.get("theo_per", 0.0),
                    "correction_rate": snapshot.get("correction_rate", 100.0),
                    "ber": snapshot.get("ber", 0.0),
                    "bit_errors": snapshot.get("total_bit_errors", 0),
                    "goodput_kbps": snapshot.get("goodput_kbps", 0.0)
                })
                if len(self.history) > 100:
                    self.history.pop(0)

    def get_snapshot(self):
        with self.lock:
            return dict(self.current_metrics)

    def get_history(self):
        with self.lock:
            return list(self.history)

DATA_STORE = TelemetryDataStore()
TELEMETRY_FILE = "/tmp/5g_telemetry.json"
LAST_MTIME = 0

def sync_from_ipc():
    global LAST_MTIME
    if os.path.exists(TELEMETRY_FILE):
        try:
            mtime = os.stat(TELEMETRY_FILE).st_mtime_ns
            if mtime > LAST_MTIME:
                LAST_MTIME = mtime
                with open(TELEMETRY_FILE, "r") as f:
                    snapshot = json.load(f)
                DATA_STORE.update(snapshot)
        except Exception:
            pass

def background_sync_worker():
    while True:
        try:
            sync_from_ipc()
            time.sleep(0.04)
        except Exception:
            time.sleep(0.05)

class TelemetryHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_DIR, **kwargs)

    def do_GET(self):
        sync_from_ipc()
        if self.path == "/api/metrics":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            data = json.dumps(DATA_STORE.get_snapshot())
            self.wfile.write(data.encode('utf-8'))
        elif self.path == "/api/history":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            data = json.dumps(DATA_STORE.get_history())
            self.wfile.write(data.encode('utf-8'))
        else:
            # Serve static files from WEB_DIR
            super().do_GET()

    def do_POST(self):
        if self.path == "/api/telemetry":
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            try:
                snapshot = json.loads(body.decode('utf-8'))
                DATA_STORE.update(snapshot)
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(b'{"status":"ok"}')
            except Exception as e:
                self.send_response(400)
                self.end_headers()
                self.wfile.write(str(e).encode('utf-8'))
        elif self.path == "/api/reset":
            DATA_STORE.reset()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"status":"reset"}')
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        # Silence routine access logs to keep terminal clean
        return

def main():
    os.chdir(WEB_DIR)
    threading.Thread(target=background_sync_worker, daemon=True).start()
    server = ThreadedHTTPServer(("0.0.0.0", PORT), TelemetryHandler)
    print("=========================================================================")
    print(f" [5G TELEMETRY & DASHBOARD SERVER ACTIVE] Port: {PORT}")
    print(f" Web UI Access: http://localhost:{PORT}")
    print(f" Serving dashboard from: {WEB_DIR}")
    print("=========================================================================\n")
    sys.stdout.flush()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nTelemetry server stopped by user.")
        server.server_close()

if __name__ == "__main__":
    main()

