import argparse
import subprocess
import logging
from pathlib import Path

logging.basicConfig(level=logging.INFO, format=" %(levelname)s - %(message)s")
logger = logging.getLogger(__name__)

EXECUTABLES = [
    ("01_foundation", ["--headless"]),
    ("02_basic", ["--headless"]),
    ("03_any_hit", ["--headless"]),
    ("04_jitter_camera", ["--headless"]),
    ("05_shadow_miss", ["--headless"]),
    ("06_reflection", ["--headless"]),
    ("07_multi_closest_hit", ["--headless"]),
    ("08_intersection", ["--headless"]),
    ("09_motion_blur", ["--headless"]),
    ("10_position_fetch", ["--headless"]),
    ("11_shader_execution_reorder", ["--headless"]),
    ("12_infinite_plane", ["--headless", "--maxFrames", "100"]),
    ("13_callable_shader", ["--headless"]),
    ("14_animation", ["--headless"]),
    ("15_micro_maps_opacity", ["--headless"]),
    ("16_ray_query", ["--headless"]),
    ("17_ray_query_screenspace", ["--headless"]),
    ("18_swept_spheres", ["--headless"]),
    ("19_ray_differentials", ["--headless"]),
    ("20_wireframe", ["--headless"]),
    ("21_clusters", ["--headless"]),
    ("22_partition_tlas", ["--headless"]),
]

def run_executable(executable_path, args):
    """Run executable and return success status."""
    try:
        logger.info(f"Running: {executable_path}")
        result = subprocess.run([str(executable_path)] + args, 
                              capture_output=True, text=True, check=True)
        logger.info(f"Output: {result.stdout}")
        return True
    except subprocess.CalledProcessError as e:
        logger.error(f"Failed: {e.stderr}")
        return False

def get_testing_time(log_file):
    """Extract testing time from log file."""
    if not log_file.exists():
        return "N/A"
    
    try:
        last_line = log_file.read_text().splitlines()[-1].strip()
        return last_line.split("->")[1].strip() if "->" in last_line else "N/A"
    except (IndexError, FileNotFoundError):
        return "N/A"

def test_executables(selected=None):
    """Test all executables (or a subset) and return results."""
    test_dir = Path("_install")
    if not test_dir.exists():
        logger.error(f"Test directory '{test_dir}' not found")
        return [], False

    executables = EXECUTABLES
    if selected:
        executables = [(name, args) for name, args in EXECUTABLES if name in selected]

    results = []
    all_passed = True

    for executable, args in executables:
        logger.info(f"\nTesting: {executable}")
        executable_path = test_dir / executable
        success = run_executable(executable_path, args)
        testing_time = get_testing_time(test_dir / f"log_{executable}.txt")
        
        results.append((executable, success, testing_time))
        if not success:
            all_passed = False
    
    return results, all_passed

def print_report(results):
    """Print test results in a formatted table."""
    logger.info("\nTest Results:")
    logger.info("-" * 60)
    logger.info(f"{'Executable':<35} | {'Status':<8} | {'Time':<8}")
    logger.info("-" * 60)
    
    for executable, success, time in results:
        status = "PASS" if success else "FAIL"
        logger.info(f"{executable:<35} | {status:<8} | {time:<8}")
    logger.info("-" * 60)

def main():
    available = [name for name, _ in EXECUTABLES]
    parser = argparse.ArgumentParser(description="Test Vulkan raytracing executables")
    parser.add_argument(
        "--test",
        action="store_true",
        help="Run all tests (kept for CI compatibility; this is the default behavior).",
    )
    parser.add_argument(
        "-e", "--executable",
        action="append",
        choices=available,
        metavar="NAME",
        help="Name of an executable to test (repeat to test several). Defaults to all.",
    )
    parser.add_argument(
        "-l", "--list",
        action="store_true",
        help="List available executables and exit.",
    )

    args = parser.parse_args()

    if args.list:
        for name in available:
            print(name)
        return 0

    results, success = test_executables(selected=args.executable)
    if not results:
        logger.error("No matching executables to run.")
        return 1
    print_report(results)
    return 0 if success else 1

if __name__ == "__main__":
    exit(main())