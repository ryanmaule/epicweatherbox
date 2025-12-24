#!/usr/bin/env python3
"""
EpicWeatherBox Release Validation Script

Pre-flash validation to prevent bricking the SmallTV-Ultra device.
Since this device has no USB data connection (power only), recovery from
a bad flash requires hardware modifications. This script validates firmware
before OTA updates to minimize risk.

Usage:
    python scripts/release_validate.py [--verbose] [--strict]

Exit codes:
    0 - All checks passed, safe to flash
    1 - Critical failure, DO NOT flash
    2 - Warnings present, review before flashing
"""

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Tuple, Optional

# ESP8266 memory limits
ESP8266_FLASH_SIZE = 4 * 1024 * 1024  # 4MB flash
ESP8266_FLASH_APP_MAX = 1024 * 1024   # 1MB for application (with 1MB LittleFS)
ESP8266_RAM_TOTAL = 81920             # 80KB total RAM
ESP8266_RAM_HEAP_MIN = 20480          # Minimum 20KB free heap recommended
ESP8266_RAM_HEAP_CRITICAL = 15360     # Critical: below 15KB causes instability

# Safe operating thresholds
FLASH_USAGE_WARN = 0.85    # Warn if >85% flash used
FLASH_USAGE_CRITICAL = 0.95 # Critical if >95% flash used
RAM_USAGE_WARN = 0.70      # Warn if >70% RAM used at compile time
RAM_USAGE_CRITICAL = 0.85  # Critical if >85% RAM used


@dataclass
class ValidationResult:
    """Result of a single validation check."""
    name: str
    passed: bool
    message: str
    severity: str = "info"  # info, warning, critical
    details: List[str] = field(default_factory=list)


@dataclass
class ValidationReport:
    """Complete validation report."""
    results: List[ValidationResult] = field(default_factory=list)
    firmware_path: Optional[Path] = None
    firmware_size: int = 0

    def add(self, result: ValidationResult):
        self.results.append(result)

    @property
    def passed(self) -> bool:
        return not any(r.severity == "critical" and not r.passed for r in self.results)

    @property
    def has_warnings(self) -> bool:
        return any(r.severity == "warning" and not r.passed for r in self.results)

    @property
    def critical_failures(self) -> List[ValidationResult]:
        return [r for r in self.results if r.severity == "critical" and not r.passed]

    @property
    def warnings(self) -> List[ValidationResult]:
        return [r for r in self.results if r.severity == "warning" and not r.passed]


class ReleaseValidator:
    """Validates firmware for safe OTA deployment."""

    def __init__(self, project_dir: Path, verbose: bool = False):
        self.project_dir = project_dir
        self.verbose = verbose
        self.pio_path = Path.home() / "Library/Python/3.9/bin/pio"
        self.report = ValidationReport()

    def log(self, msg: str):
        if self.verbose:
            print(f"  [DEBUG] {msg}")

    def run_all_checks(self) -> ValidationReport:
        """Run all validation checks."""
        print("\n" + "=" * 60)
        print("EpicWeatherBox Release Validation")
        print("=" * 60 + "\n")

        # Phase 1: Pre-build checks
        print("[Phase 1] Pre-build validation...")
        self.check_source_files()
        self.check_required_features()
        self.check_watchdog_timer()
        self.check_ota_handler()
        self.check_safe_mode()

        # Phase 2: Build and analyze
        print("\n[Phase 2] Building firmware...")
        build_success = self.build_firmware()

        if build_success:
            # Phase 3: Binary analysis
            print("\n[Phase 3] Binary analysis...")
            self.analyze_firmware_size()
            self.analyze_memory_usage()
            self.check_string_safety()

            # Phase 4: Static analysis
            print("\n[Phase 4] Static analysis...")
            self.check_infinite_loops()
            self.check_blocking_code()
            self.check_memory_allocations()

        return self.report

    def check_source_files(self):
        """Verify all required source files exist."""
        required_files = [
            "src/main.cpp",
            "src/config.h",
            "platformio.ini",
        ]

        missing = []
        for f in required_files:
            if not (self.project_dir / f).exists():
                missing.append(f)

        self.report.add(ValidationResult(
            name="Source Files",
            passed=len(missing) == 0,
            message=f"All {len(required_files)} required files present" if not missing else f"Missing: {', '.join(missing)}",
            severity="critical" if missing else "info"
        ))

    def check_required_features(self):
        """Check that critical safety features are enabled in config."""
        config_path = self.project_dir / "src/config.h"
        if not config_path.exists():
            return

        config = config_path.read_text()

        # Check for OTA support
        ota_enabled = "FEATURE_OTA_UPDATE 1" in config or "FEATURE_OTA_UPDATE  1" in config

        self.report.add(ValidationResult(
            name="OTA Update Feature",
            passed=ota_enabled,
            message="OTA updates enabled" if ota_enabled else "OTA updates DISABLED - recovery impossible without hardware mod!",
            severity="critical" if not ota_enabled else "info"
        ))

    def check_watchdog_timer(self):
        """Verify watchdog timer is implemented."""
        main_cpp = self.project_dir / "src/main.cpp"
        if not main_cpp.exists():
            return

        code = main_cpp.read_text()

        # Check for watchdog patterns
        has_wdt_enable = "ESP.wdtEnable" in code or "wdt_enable" in code
        has_wdt_feed = "ESP.wdtFeed" in code or "wdt_feed" in code or "yield()" in code
        has_wdt_disable = "ESP.wdtDisable" in code

        # yield() also feeds the watchdog on ESP8266
        yield_count = code.count("yield()")
        delay_count = code.count("delay(")

        details = []
        if has_wdt_enable:
            details.append("WDT enable found")
        if yield_count > 0:
            details.append(f"yield() calls: {yield_count}")
        if delay_count > 0:
            details.append(f"delay() calls: {delay_count}")

        # Watchdog is fed by delay() and yield() on ESP8266
        watchdog_safe = (yield_count + delay_count) >= 5  # Should have regular yields/delays

        self.report.add(ValidationResult(
            name="Watchdog Timer",
            passed=watchdog_safe,
            message="Watchdog timer properly serviced" if watchdog_safe else "Insufficient yield/delay calls - risk of WDT reset",
            severity="warning" if not watchdog_safe else "info",
            details=details
        ))

    def check_ota_handler(self):
        """Verify OTA update handler is present and safe."""
        main_cpp = self.project_dir / "src/main.cpp"
        if not main_cpp.exists():
            return

        code = main_cpp.read_text()

        checks = {
            "ArduinoOTA": "ArduinoOTA" in code,
            "Web OTA endpoint": '"/update"' in code or "handleUpdate" in code,
            "OTA begin": "ArduinoOTA.begin()" in code or "ArduinoOTA.handle()" in code,
        }

        all_present = all(checks.values())
        details = [f"{'[OK]' if v else '[MISSING]'} {k}" for k, v in checks.items()]

        self.report.add(ValidationResult(
            name="OTA Handler",
            passed=all_present,
            message="OTA handler properly configured" if all_present else "OTA handler incomplete",
            severity="critical" if not all_present else "info",
            details=details
        ))

    def check_safe_mode(self):
        """Check for emergency safe mode or recovery mechanism."""
        main_cpp = self.project_dir / "src/main.cpp"
        if not main_cpp.exists():
            return

        code = main_cpp.read_text()

        # Look for safe mode patterns
        has_safe_mode = (
            "safe" in code.lower() and "mode" in code.lower()
        ) or "emergency" in code.lower() or "recovery" in code.lower()

        has_reset_handler = "ESP.restart()" in code or "ESP.reset()" in code
        has_config_reset = "LittleFS.format()" in code or "reset=1" in code

        safe_recovery = has_safe_mode or has_config_reset

        self.report.add(ValidationResult(
            name="Safe Mode/Recovery",
            passed=safe_recovery,
            message="Safe mode/recovery mechanism present" if safe_recovery else "No safe mode detected - consider adding emergency recovery",
            severity="warning" if not safe_recovery else "info"
        ))

    def build_firmware(self) -> bool:
        """Build firmware and capture size information."""
        try:
            result = subprocess.run(
                [str(self.pio_path), "run", "-e", "esp8266"],
                cwd=self.project_dir,
                capture_output=True,
                text=True,
                timeout=300
            )

            build_success = result.returncode == 0

            # Parse build output for size info
            size_info = self._parse_build_output(result.stdout + result.stderr)

            self.report.add(ValidationResult(
                name="Build",
                passed=build_success,
                message="Build successful" if build_success else "Build FAILED",
                severity="critical" if not build_success else "info",
                details=size_info if size_info else ["No size info available"]
            ))

            return build_success

        except subprocess.TimeoutExpired:
            self.report.add(ValidationResult(
                name="Build",
                passed=False,
                message="Build timed out after 5 minutes",
                severity="critical"
            ))
            return False
        except Exception as e:
            self.report.add(ValidationResult(
                name="Build",
                passed=False,
                message=f"Build error: {str(e)}",
                severity="critical"
            ))
            return False

    def _parse_build_output(self, output: str) -> List[str]:
        """Parse PlatformIO build output for memory usage."""
        info = []

        # Look for RAM usage
        ram_match = re.search(r"RAM:\s+\[=*\s*\]\s+(\d+\.?\d*)%\s+\(used\s+(\d+)\s+bytes", output)
        if ram_match:
            ram_pct = float(ram_match.group(1))
            ram_used = int(ram_match.group(2))
            info.append(f"RAM: {ram_pct}% ({ram_used} bytes)")

        # Look for Flash usage
        flash_match = re.search(r"Flash:\s+\[=*\s*\]\s+(\d+\.?\d*)%\s+\(used\s+(\d+)\s+bytes", output)
        if flash_match:
            flash_pct = float(flash_match.group(1))
            flash_used = int(flash_match.group(2))
            info.append(f"Flash: {flash_pct}% ({flash_used} bytes)")

        return info

    def analyze_firmware_size(self):
        """Analyze the built firmware binary."""
        firmware_path = self.project_dir / ".pio/build/esp8266/firmware.bin"

        if not firmware_path.exists():
            self.report.add(ValidationResult(
                name="Firmware Binary",
                passed=False,
                message="Firmware binary not found",
                severity="critical"
            ))
            return

        size = firmware_path.stat().st_size
        self.report.firmware_path = firmware_path
        self.report.firmware_size = size

        size_kb = size / 1024
        size_pct = size / ESP8266_FLASH_APP_MAX

        if size_pct >= FLASH_USAGE_CRITICAL:
            severity = "critical"
            passed = False
            msg = f"Firmware too large: {size_kb:.1f}KB ({size_pct*100:.1f}% of {ESP8266_FLASH_APP_MAX/1024}KB limit)"
        elif size_pct >= FLASH_USAGE_WARN:
            severity = "warning"
            passed = True
            msg = f"Firmware size warning: {size_kb:.1f}KB ({size_pct*100:.1f}%)"
        else:
            severity = "info"
            passed = True
            msg = f"Firmware size OK: {size_kb:.1f}KB ({size_pct*100:.1f}%)"

        self.report.add(ValidationResult(
            name="Firmware Size",
            passed=passed,
            message=msg,
            severity=severity,
            details=[
                f"Binary: {firmware_path.name}",
                f"Size: {size:,} bytes",
                f"Max allowed: {ESP8266_FLASH_APP_MAX:,} bytes"
            ]
        ))

    def analyze_memory_usage(self):
        """Analyze RAM usage from build output."""
        # Re-run with verbose to get detailed memory info
        try:
            result = subprocess.run(
                [str(self.pio_path), "run", "-e", "esp8266", "-v"],
                cwd=self.project_dir,
                capture_output=True,
                text=True,
                timeout=300
            )

            output = result.stdout + result.stderr

            # Parse data segment sizes
            data_match = re.search(r"DATA:\s+(\d+)", output)
            bss_match = re.search(r"BSS:\s+(\d+)", output)

            # Alternative pattern for RAM usage
            ram_match = re.search(r"RAM:\s+\[.*?\]\s+(\d+\.?\d*)%", output)

            details = []
            if ram_match:
                ram_pct = float(ram_match.group(1))
                details.append(f"Compile-time RAM: {ram_pct}%")

                if ram_pct >= RAM_USAGE_CRITICAL * 100:
                    severity = "critical"
                    passed = False
                    msg = f"RAM usage critical: {ram_pct}%"
                elif ram_pct >= RAM_USAGE_WARN * 100:
                    severity = "warning"
                    passed = True
                    msg = f"RAM usage high: {ram_pct}%"
                else:
                    severity = "info"
                    passed = True
                    msg = f"RAM usage OK: {ram_pct}%"

                self.report.add(ValidationResult(
                    name="RAM Usage",
                    passed=passed,
                    message=msg,
                    severity=severity,
                    details=details
                ))

        except Exception as e:
            self.log(f"Memory analysis error: {e}")

    def check_string_safety(self):
        """Check for PROGMEM usage for large strings."""
        main_cpp = self.project_dir / "src/main.cpp"
        admin_h = self.project_dir / "src/admin_html.h"

        issues = []

        if main_cpp.exists():
            code = main_cpp.read_text()

            # Look for large string literals not in PROGMEM
            # Find quoted strings > 100 chars that aren't PROGMEM
            large_strings = re.findall(r'"[^"]{100,}"', code)
            for s in large_strings:
                if "PROGMEM" not in code[max(0, code.find(s)-50):code.find(s)]:
                    issues.append(f"Large string ({len(s)} chars) may not be in PROGMEM")

        if admin_h.exists():
            code = admin_h.read_text()
            if "PROGMEM" not in code:
                issues.append("admin_html.h should use PROGMEM for HTML content")

        self.report.add(ValidationResult(
            name="PROGMEM Usage",
            passed=len(issues) == 0,
            message="String storage optimized" if not issues else f"{len(issues)} potential RAM issues",
            severity="warning" if issues else "info",
            details=issues[:5]  # Limit to first 5 issues
        ))

    def check_infinite_loops(self):
        """Check for potential infinite loops that could cause WDT reset."""
        main_cpp = self.project_dir / "src/main.cpp"
        if not main_cpp.exists():
            return

        code = main_cpp.read_text()

        # Look for while loops without yield/delay inside
        issues = []

        # Simple pattern matching for dangerous while(true) without yield
        while_true_pattern = r'while\s*\(\s*(?:true|1)\s*\)\s*\{[^}]*\}'
        for match in re.finditer(while_true_pattern, code, re.DOTALL):
            loop_body = match.group(0)
            if "yield()" not in loop_body and "delay(" not in loop_body and "break" not in loop_body:
                line_num = code[:match.start()].count('\n') + 1
                issues.append(f"Line ~{line_num}: while(true) without yield/delay")

        self.report.add(ValidationResult(
            name="Infinite Loop Check",
            passed=len(issues) == 0,
            message="No unsafe infinite loops detected" if not issues else f"{len(issues)} potential infinite loops",
            severity="warning" if issues else "info",
            details=issues[:5]
        ))

    def check_blocking_code(self):
        """Check for long blocking operations."""
        main_cpp = self.project_dir / "src/main.cpp"
        if not main_cpp.exists():
            return

        code = main_cpp.read_text()

        issues = []

        # Check for very long delays
        for match in re.finditer(r'delay\((\d+)\)', code):
            delay_ms = int(match.group(1))
            if delay_ms > 5000:  # > 5 seconds is concerning
                line_num = code[:match.start()].count('\n') + 1
                issues.append(f"Line ~{line_num}: delay({delay_ms}ms) - very long blocking delay")

        # Check for blocking HTTP without timeout
        if "http.GET()" in code and "setTimeout" not in code and "setConnectTimeout" not in code:
            issues.append("HTTP requests may lack timeout - could block indefinitely")

        self.report.add(ValidationResult(
            name="Blocking Code",
            passed=len(issues) == 0,
            message="No problematic blocking code" if not issues else f"{len(issues)} blocking issues",
            severity="warning" if issues else "info",
            details=issues[:5]
        ))

    def check_memory_allocations(self):
        """Check for potentially dangerous memory allocations."""
        main_cpp = self.project_dir / "src/main.cpp"
        if not main_cpp.exists():
            return

        code = main_cpp.read_text()

        issues = []

        # Check for large stack allocations
        large_array_pattern = r'\b(?:char|uint8_t|byte)\s+\w+\[(\d+)\]'
        for match in re.finditer(large_array_pattern, code):
            size = int(match.group(1))
            if size > 2048:  # > 2KB on stack is risky on ESP8266
                line_num = code[:match.start()].count('\n') + 1
                issues.append(f"Line ~{line_num}: Large stack array ({size} bytes)")

        # Check for malloc/new without free/delete nearby
        malloc_count = code.count("malloc(") + code.count(" new ")
        free_count = code.count("free(") + code.count("delete ")

        if malloc_count > free_count + 2:  # Allow some imbalance for static allocations
            issues.append(f"Potential memory leak: {malloc_count} allocations vs {free_count} frees")

        # Check for proper JSON document sizes
        json_large = re.findall(r'JsonDocument\s+\w+\s*(?:;|\((\d+)\))', code)

        self.report.add(ValidationResult(
            name="Memory Allocations",
            passed=len(issues) == 0,
            message="Memory allocation patterns OK" if not issues else f"{len(issues)} memory concerns",
            severity="warning" if issues else "info",
            details=issues[:5]
        ))

    def print_report(self):
        """Print the validation report."""
        print("\n" + "=" * 60)
        print("VALIDATION REPORT")
        print("=" * 60)

        # Group by severity
        for result in self.report.results:
            icon = "[OK]" if result.passed else "[FAIL]"
            if result.severity == "critical" and not result.passed:
                icon = "[CRITICAL]"
            elif result.severity == "warning" and not result.passed:
                icon = "[WARNING]"

            print(f"\n{icon} {result.name}")
            print(f"    {result.message}")

            if self.verbose and result.details:
                for detail in result.details:
                    print(f"      - {detail}")

        # Summary
        print("\n" + "-" * 60)
        print("SUMMARY")
        print("-" * 60)

        if self.report.firmware_size:
            print(f"Firmware size: {self.report.firmware_size:,} bytes ({self.report.firmware_size/1024:.1f} KB)")

        passed = len([r for r in self.report.results if r.passed])
        total = len(self.report.results)
        print(f"Checks passed: {passed}/{total}")

        if self.report.critical_failures:
            print(f"\nCRITICAL FAILURES: {len(self.report.critical_failures)}")
            for f in self.report.critical_failures:
                print(f"  - {f.name}: {f.message}")

        if self.report.warnings:
            print(f"\nWARNINGS: {len(self.report.warnings)}")
            for w in self.report.warnings:
                print(f"  - {w.name}: {w.message}")

        print("\n" + "=" * 60)

        if not self.report.passed:
            print("RESULT: FAILED - DO NOT FLASH THIS FIRMWARE")
            print("Critical issues must be resolved before deployment.")
            return 1
        elif self.report.has_warnings:
            print("RESULT: PASSED WITH WARNINGS")
            print("Review warnings before flashing. Proceed with caution.")
            return 2
        else:
            print("RESULT: PASSED - Safe to flash")
            return 0


def main():
    parser = argparse.ArgumentParser(
        description="Validate EpicWeatherBox firmware before OTA deployment",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Exit codes:
  0 - All checks passed, safe to flash
  1 - Critical failure, DO NOT flash
  2 - Warnings present, review before flashing

Examples:
  python scripts/release_validate.py
  python scripts/release_validate.py --verbose
  python scripts/release_validate.py --strict
        """
    )
    parser.add_argument("--verbose", "-v", action="store_true", help="Show detailed output")
    parser.add_argument("--strict", action="store_true", help="Treat warnings as failures")
    args = parser.parse_args()

    # Find project root
    script_dir = Path(__file__).parent
    project_dir = script_dir.parent

    if not (project_dir / "platformio.ini").exists():
        print("Error: Must be run from project directory or scripts/ subdirectory")
        sys.exit(1)

    validator = ReleaseValidator(project_dir, verbose=args.verbose)
    validator.run_all_checks()
    exit_code = validator.print_report()

    if args.strict and exit_code == 2:
        print("\n--strict mode: Treating warnings as failures")
        exit_code = 1

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
