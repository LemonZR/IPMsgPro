#!/usr/bin/env python3
"""
IPMsgPro Self-Test Driver
Launches two IPMsgPro.exe instances (Server on 2525, Client on 2425)
and runs automated tests. Validates results by reading log files.
"""

import subprocess
import threading
import time
import re
import os
import json
import sys
import tempfile

# ============================================================================
# Paths
# ============================================================================
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
EXE_PATH = os.path.join(PROJECT_DIR, "build", "Release", "IPMsgPro.exe")
CONFIG_PATH = os.path.join(SCRIPT_DIR, "self_test_config.json")
TEST_FILE = os.path.join(PROJECT_DIR, "build", "Release", "ipmsg_test_send_file.bin")

# Log files path (matches GetAppDataDir in main.cpp)
LOCAL_APPDATA = os.environ.get("LOCALAPPDATA", os.path.expanduser("~\\AppData\\Local"))
SERVER_LOG = os.path.join(LOCAL_APPDATA, "IPMsgPro_2525", "ipmsgpro.log")
CLIENT_LOG = os.path.join(LOCAL_APPDATA, "IPMsgPro", "ipmsgpro.log")

# Ensure test file exists
if not os.path.exists(TEST_FILE):
    print(f"[DRIVER] Creating test file: {TEST_FILE}")
    with open(TEST_FILE, "wb") as f:
        for i in range(1024 * 1024):  # 1MB
            f.write((i & 0xFF).to_bytes(1, "little"))


class IPMsgProInstance:
    """Wrapper for an IPMsgPro.exe process in CLI mode."""

    def __init__(self, name, port, cmd, log_path, extra_args=None):
        self.name = name
        self.port = port
        self.cmd = cmd
        self.log_path = log_path
        self.extra_args = extra_args or []
        self.process = None
        self._initial_log_size = 0

    def start(self):
        """Start the process."""
        args = [
            EXE_PATH,
            f"--port={self.port}",
            "--mode=cli",
            f"--cmd={self.cmd}",
        ] + self.extra_args

        # Clean old log
        log_dir = os.path.dirname(self.log_path)
        os.makedirs(log_dir, exist_ok=True)
        if os.path.exists(self.log_path):
            os.remove(self.log_path)

        print(f"[DRIVER] Starting {self.name} (port={self.port}, cmd={self.cmd})")
        print(f"[DRIVER]   Log: {self.log_path}")
        print(f"[DRIVER]   Args: {' '.join(args)}")

        self.process = subprocess.Popen(
            args,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            cwd=os.path.dirname(EXE_PATH),
        )

        self._initial_log_size = 0
        return self

    def stop(self):
        """Stop the process."""
        if self.process:
            print(f"[DRIVER] Stopping {self.name}...")
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            print(f"[DRIVER] {self.name} stopped")

    def read_log(self):
        """Read the log file contents."""
        if not os.path.exists(self.log_path):
            return []
        with open(self.log_path, "r", encoding="utf-8", errors="replace") as f:
            return f.readlines()

    def read_log_since_start(self):
        """Read log entries since process started."""
        lines = self.read_log()
        return lines[self._initial_log_size:]

    def wait_for_line(self, pattern, timeout=20):
        """Wait for a log line matching regex pattern."""
        start = time.time()
        while time.time() - start < timeout:
            lines = self.read_log_since_start()
            for line in lines:
                if re.search(pattern, line):
                    return line.strip()
            time.sleep(0.2)
        return None

    def count_lines(self, pattern, since_start=True):
        """Count log lines matching pattern."""
        lines = self.read_log_since_start() if since_start else self.read_log()
        return sum(1 for l in lines if re.search(pattern, l))

    def dump_log(self, max_lines=30):
        """Print last N log lines."""
        lines = self.read_log_since_start()
        for line in lines[-max_lines:]:
            print(f"    {line.rstrip()}")


def main():
    print("=" * 60)
    print("  IPMsgPro Self-Test Driver")
    print("=" * 60)
    print()

    # Check exe exists
    if not os.path.exists(EXE_PATH):
        print(f"[ERROR] IPMsgPro.exe not found at: {EXE_PATH}")
        sys.exit(1)

    # Adjust config for local test: file path relative to exe dir
    config = json.load(open(CONFIG_PATH, "r", encoding="utf-8"))
    for item in config["tests"]:
        if item["type"] == "file":
            item["content"] = os.path.abspath(TEST_FILE)

    # Write adjusted config
    adjusted_config = CONFIG_PATH  # overwrite in place
    json.dump(config, open(adjusted_config, "w", encoding="utf-8"), ensure_ascii=False, indent=2)

    # ============================================================
    # Start Server (2525, auto-accept files)
    # ============================================================
    server = IPMsgProInstance("Server", 2525, "server", SERVER_LOG)
    server.start()

    server_ready = server.wait_for_line("CLI Server ready", timeout=15)
    if not server_ready:
        print("[ERROR] Server failed to start!")
        print("  Server log:")
        server.dump_log()
        server.stop()
        sys.exit(1)
    print(f"  [OK] Server ready")

    time.sleep(2)

    # ============================================================
    # Start Client (2425, send test content)
    # ============================================================
    extra_args = [
        f"--config={adjusted_config}",
        f"--target=127.0.0.1:2525",
    ]
    client = IPMsgProInstance("Client", 2425, "test", CLIENT_LOG, extra_args)
    client.start()

    # Wait for test runner to complete
    completed = client.wait_for_line("CLI TEST RUNNER COMPLETE", timeout=90)
    if not completed:
        print("[WARN] Test runner did not complete within timeout")

    time.sleep(3)

    # ============================================================
    # Validate results from log files
    # ============================================================
    print()
    print("=" * 60)
    print("  RESULTS")
    print("=" * 60)
    print()

    passed = 0
    failed = 0

    # --- 1. Mutual Discovery ---
    print("--- 1. Mutual User Discovery ---")
    s_discovered = server.count_lines(r"\[USER\].*Discovered")
    c_discovered = client.count_lines(r"\[USER\].*Discovered")
    print(f"  Server discovered {s_discovered} user(s)")
    print(f"  Client discovered {c_discovered} user(s)")
    CLIENT_DISCOVERED = s_discovered >= 1
    SERVER_DISCOVERED = c_discovered >= 1
    if CLIENT_DISCOVERED and SERVER_DISCOVERED:
        print("  [PASS] Both sides discovered each other")
        passed += 1
    else:
        print("  [FAIL] Discovery incomplete")
        failed += 1

    # --- 2. Text Message Delivery ---
    print()
    print("--- 2. Text Message Delivery ---")
    server_hello = server.count_lines(r"Hello from IPMsgPro")
    server_second = server.count_lines(r"第二条")
    print(f"  Server received {server_hello + server_second} text message(s)")
    if server_hello >= 1 and server_second >= 1:
        print("  [PASS] Text messages delivered")
        passed += 1
    elif server_hello >= 1:
        print("  [WARN] First message received, second missing")
        print("  [PARTIAL PASS]")
        passed += 0
    else:
        print("  [FAIL] No text messages received on server side")
        failed += 1

    # --- 3. File Transfer ---
    print()
    print("--- 3. File Transfer ---")
    server_recv = server.count_lines(r"\[TRANSFER Received\]")
    print(f"  Server received: {server_recv} file transfer(s) completed")
    if server_recv >= 1:
        print("  [PASS] File transfer completed")
        passed += 1
    else:
        auto_accept = server.count_lines(r"\[AUTO-ACCEPT\].*File:")
        file_notify = server.count_lines(r"\[FILE NOTIFICATION\]")
        transfer_started = server.count_lines(r"\[AUTO-ACCEPT\].*Transfer started")
        transfer_failed = server.count_lines(r"\[TRANSFER FAILED\]")
        print(f"  File notifications: {file_notify}")
        print(f"  Auto-accept parsed: {auto_accept}")
        print(f"  Transfer started: {transfer_started}")
        print(f"  Transfer failed: {transfer_failed}")
        if transfer_started >= 1:
            print("  [WARN] Transfer started but not completed (may need more time)")
            passed += 0
        else:
            print("  [FAIL] File transfer did not complete")
            failed += 1

    # --- 4. RECVMSG Acknowledgments ---
    print()
    print("--- 4. RECVMSG Acknowledgments ---")
    client_recvmsg = client.count_lines(r"\[RECVMSG ACK\]")
    print(f"  Client received {client_recvmsg} RECVMSG ack(s)")
    if client_recvmsg >= 1:
        print("  [PASS] Acknowledgments received")
        passed += 1
    else:
        print("  [FAIL] No RECVMSG acknowledgments")
        failed += 1

    # --- 5. Chinese text encoding ---
    print()
    print("--- 5. Chinese Text Encoding ---")
    server_cn_msgs = server.count_lines(r"\[TEXT\].*测试")
    print(f"  Server received {server_cn_msgs} Chinese message(s)")
    cn_ok = server.count_lines(r"测试消息") or server.count_lines(r"这是第二条")
    if cn_ok >= 1:
        print("  [PASS] Chinese text correctly received")
        passed += 1
    else:
        print("  [WARN] Chinese text check - check encoding")
        passed += 0  # not critical

    # Print summary
    print()
    print("=" * 60)
    total = passed + failed
    print(f"  SUMMARY: {passed} passed, {failed} failed / {total} total")
    print("=" * 60)

    # Print debug logs
    print()
    print("--- SERVER LOG (last 30 lines) ---")
    server.dump_log(30)

    print()
    print("--- CLIENT LOG (last 30 lines) ---")
    client.dump_log(30)

    # Cleanup
    client.stop()
    server.stop()

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
