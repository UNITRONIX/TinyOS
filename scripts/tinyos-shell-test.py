#!/usr/bin/env python3
"""Drive the TinyOS shell over a serial console and verify its responses.

This is the first automated test of shell *behaviour* in the project. Before it,
the only way to know a command worked was to boot QEMU, read the screen and type
by hand, which is why several commands shipped that printed
"not wired to a create path yet" while their manifest entry claimed Ready.

The script starts QEMU with the shell in serial-input mode, waits for the boot
marker, sends commands one at a time and asserts on the output that follows.

Usage:
    scripts/tinyos-shell-test.py <iso> [--timeout SECONDS]

Exit codes: 0 all expectations met, 1 a check failed, 2 the shell never started.
"""

import argparse
import queue
import re
import subprocess
import sys
import threading
import time


class SerialShell:
    """QEMU serial console wrapper with a background reader thread."""

    def __init__(self, iso_path: str, qemu: str = "qemu-system-i386"):
        self.iso_path = iso_path
        self.qemu = qemu
        self.process = None
        self.lines = queue.Queue()
        self._buffer = ""
        self._reader = None

    def start(self) -> None:
        self.process = subprocess.Popen(
            [
                self.qemu,
                "-cdrom", self.iso_path,
                "-display", "none",
                "-serial", "stdio",
                "-no-reboot",
                "-no-shutdown",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self) -> None:
        """Collects characters and splits them into lines for the waiter."""
        while True:
            chunk = self.process.stdout.read(1)
            if not chunk:
                break

            character = chunk.decode("ascii", errors="replace")
            if character == "\r":
                continue

            if character == "\n":
                self.lines.put(self._buffer)
                self._buffer = ""
                continue

            self._buffer += character

    def wait_for(self, needle: str, timeout: float) -> str:
        """Waits until a line contains `needle`. Returns all text seen so far."""
        deadline = time.time() + timeout
        seen = []

        while time.time() < deadline:
            try:
                line = self.lines.get(timeout=0.2)
            except queue.Empty:
                continue

            seen.append(line)
            if needle in line:
                return "\n".join(seen)

        return "\n".join(seen)

    def drain(self, timeout: float) -> str:
        """Collects whatever arrives until the timeout expires."""
        deadline = time.time() + timeout
        seen = []

        while time.time() < deadline:
            try:
                line = self.lines.get(timeout=0.1)
            except queue.Empty:
                continue
            seen.append(line)

        return "\n".join(seen)

    def send(self, command: str) -> None:
        self.process.stdin.write((command + "\n").encode("ascii"))
        self.process.stdin.flush()

    def stop(self) -> None:
        if self.process is None:
            return

        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()


class Results:
    def __init__(self):
        self.checks = 0
        self.failures = 0

    def check(self, condition: bool, description: str, detail: str = "") -> None:
        self.checks += 1
        if condition:
            print(f"  ok    {description}")
            return

        self.failures += 1
        print(f"  FAIL  {description}")
        if detail:
            for line in detail.splitlines():
                print(f"        | {line}")

    def contains(self, text: str, needle: str, description: str) -> None:
        self.check(needle in text, description, text if needle not in text else "")

    def missing(self, text: str, needle: str, description: str) -> None:
        self.check(needle not in text, description, text if needle in text else "")


def run(iso_path: str, startup_timeout: float, command_timeout: float) -> int:
    results = Results()
    shell = SerialShell(iso_path)

    print(f"Starting QEMU with {iso_path}")
    shell.start()

    try:
        # The shell prints its prompt only after the whole boot sequence, so the
        # prompt is the readiness signal rather than the boot marker.
        boot = shell.wait_for("TinyOS shell ready.", startup_timeout)
        if "TinyOS shell ready." not in boot:
            print("FAILED: the shell never started.")
            print(boot)
            return 2

        results.check(True, "shell started")

        # Give the prompt a moment to be emitted after the log line.
        shell.drain(0.5)

        print("Checking command execution")

        # echo proves parsing, argument delivery and output routing.
        shell.send("echo serial-works")
        output = shell.wait_for("serial-works", command_timeout)
        results.contains(output, "serial-works", "echo returns its argument")

        # help reads the registry; if the registry were broken this would be
        # empty or produce the old hard-coded list.
        shell.send("help")
        output = shell.wait_for("files", command_timeout)
        results.contains(output, "files", "help lists commands by category")
        results.contains(output, "shell", "help groups by category")

        # tools must report availability counts, which only exist because the
        # registry distinguishes Real from Needs*.
        shell.send("tools")
        output = shell.wait_for("Available", command_timeout)
        results.contains(output, "Available", "tools reports availability")

        # A command that needs networking must refuse with a reason instead of
        # pretending to work. It must also be *registered*: a planned command
        # that is missing from the registry reports "command not found", which
        # hides the real blocker.
        shell.send("netconfig")
        output = shell.wait_for("not available", command_timeout)
        results.contains(output, "not available", "unavailable command refuses")
        results.contains(output, "needs-networking", "refusal names the missing capability")
        results.missing(output, "command not found", "unavailable command is registered")

        # A genuinely unknown command must be reported as such.
        shell.send("definitelynotacommand")
        output = shell.wait_for("not found", command_timeout)
        results.contains(output, "not found", "unknown command reported")

        # File operations are the commands that were real before the rewrite.
        # They need file-write capability, so this session must be authenticated
        # first: the unauthenticated default refuses writes by design.
        shell.send("login root tinyos")
        shell.wait_for("Logged in as root", command_timeout)

        shell.send("touch /selftest.txt")
        shell.drain(0.4)
        shell.send("write /selftest.txt hello-from-test")
        shell.drain(0.4)
        shell.send("show /selftest.txt")
        output = shell.wait_for("hello-from-test", command_timeout)
        results.contains(output, "hello-from-test", "write then show round-trip")

        shell.send("remove /selftest.txt")
        shell.drain(0.4)
        shell.send("show /selftest.txt")
        output = shell.wait_for("not readable", command_timeout)
        results.contains(output, "not readable", "removed file is gone")

        # `cd` must actually change the session directory and be reflected in
        # the prompt and in pwd. It used to refuse with "needs the session
        # object"; Context now carries a Session pointer.
        shell.send("cd /system")
        output = shell.wait_for("root@tinyos:/system", command_timeout)
        results.contains(output, "/system", "cd changes the prompt directory")

        shell.send("pwd")
        output = shell.wait_for("/system", command_timeout)
        results.contains(output, "/system", "pwd reports the new directory")

        # The generated catalogue must list every registered command and end with
        # the summary, not stop mid-line. The first version used a 2 KiB buffer
        # and silently cut the file at ~24 entries, which is the exact truncation
        # failure this rewrite exists to remove.
        shell.send("show /system/tools.txt")
        output = shell.wait_for("ready=", command_timeout)
        results.contains(output, "format=tinyos-tools-v1", "tools.txt has a format marker")
        results.contains(output, "source=libshell-registry", "tools.txt declares its source")
        results.missing(output, "truncated=yes", "tools.txt is not truncated")
        results.contains(output, "total=", "tools.txt reports a total")
        results.contains(output, "ready=", "tools.txt reports availability")

        # whoami must report the session identity, not a hard-coded "root". After
        # the login above the session is authenticated, so no suffix is expected;
        # the unauthenticated case is verified separately below.
        shell.send("whoami")
        output = shell.wait_for("root@tinyos", command_timeout)
        results.contains(output, "root", "whoami reports the logged-in identity")
        results.missing(output, "unauthenticated", "logged-in session is not marked unauthenticated")

        # id exposes the uid the session actually carries.
        shell.send("id")
        output = shell.wait_for("uid=", command_timeout)
        results.contains(output, "uid=0", "id reports the real uid")

        # Environment mutation must round-trip.
        shell.send("set TESTVAR hello")
        shell.drain(0.3)
        shell.send("echo $TESTVAR")
        output = shell.wait_for("hello", command_timeout)
        results.contains(output, "hello", "set then expand round-trip")

        shell.send("unset TESTVAR")
        shell.drain(0.3)
        shell.send("echo [$TESTVAR]")
        output = shell.wait_for("[]", command_timeout)
        results.contains(output, "[]", "unset removes the variable")

        # Authentication must actually gate privileges. An unauthenticated
        # session that can create accounts is a privilege-escalation hole, not a
        # convenience, and the first build had exactly that (defect D48).
        shell.send("logout")
        shell.drain(0.4)
        shell.send("whoami")
        output = shell.wait_for("nobody", command_timeout)
        results.contains(output, "nobody", "logout drops the identity")

        shell.send("useradd mallory evil")
        output = shell.wait_for("permission denied", command_timeout)
        results.contains(output, "permission denied", "unauthenticated session cannot create accounts")

        shell.send("write /notes.txt x")
        output = shell.wait_for("permission denied", command_timeout)
        results.contains(output, "permission denied", "unauthenticated session cannot write files")

        # Logging in must restore administrative capability.
        shell.send("login root tinyos")
        output = shell.wait_for("Logged in as root", command_timeout)
        results.contains(output, "Logged in as root", "login authenticates against the store")

        shell.send("whoami")
        output = shell.wait_for("root@tinyos", command_timeout)
        results.contains(output, "root", "identity reported after login")

        shell.send("useradd mallory evil")
        output = shell.wait_for("Created", command_timeout)
        results.contains(output, "Created", "authenticated admin can create accounts")

        # Password hashing must be PBKDF2, not the old single-round FNV.
        shell.send("accounts")
        output = shell.wait_for("iterations=", command_timeout)
        results.contains(output, "iterations=", "accounts report a KDF iteration count")

    finally:
        shell.stop()

    print()
    print(f"{results.checks} checks, {results.failures} failures")
    return 0 if results.failures == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", help="path to the TinyOS ISO")
    parser.add_argument("--startup-timeout", type=float, default=30.0,
                        help="seconds to wait for the shell prompt")
    parser.add_argument("--command-timeout", type=float, default=6.0,
                        help="seconds to wait for each command's output")
    args = parser.parse_args()

    return run(args.iso, args.startup_timeout, args.command_timeout)


if __name__ == "__main__":
    sys.exit(main())
