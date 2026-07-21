import os
import shutil
import http.server
import socketserver

# Ensure we have the benchmark file
src = "build/runtime_benchmark.json"
dst = "dashboard/runtime_benchmark.json"

if os.path.exists(src):
    shutil.copy(src, dst)
    print(f"Copied telemetry to dashboard.")
else:
    print(f"Warning: {src} not found. Please run the MPIE build & benchmark first.")

os.chdir("dashboard")
PORT = 8080
Handler = http.server.SimpleHTTPRequestHandler

with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print(f"=========================================")
    print(f"MarketPulse Dashboard running!")
    print(f"Open your browser to: http://localhost:{PORT}")
    print(f"=========================================")
    httpd.serve_forever()
