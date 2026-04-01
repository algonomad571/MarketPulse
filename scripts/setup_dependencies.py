#!/usr/bin/env python3
"""Market Pulse - Dependency Setup Script
Downloads and unpacks C++ header-only libraries
"""

import os
import sys
import urllib.request
import zipfile
import shutil
from pathlib import Path


def download_file(url: str, dest_file: str) -> bool:
    """Download a file from URL"""
    try:
        print(f"Downloading: {url}")
        urllib.request.urlretrieve(url, dest_file)
        print(f"Downloaded to: {dest_file}")
        return True
    except Exception as e:
        print(f"Error downloading {url}: {e}", file=sys.stderr)
        return False


def extract_zip(zip_path: str, extract_path: str) -> bool:
    """Extract a zip file"""
    try:
        print(f"Extracting: {zip_path}")
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(extract_path)
        print(f"Extracted to: {extract_path}")
        return True
    except Exception as e:
        print(f"Error extracting {zip_path}: {e}", file=sys.stderr)
        return False


def setup_external_deps(external_dir: str = "external"):
    """Setup all external dependencies"""
    
    external_path = Path(external_dir)
    external_path.mkdir(exist_ok=True)
    print(f"External directory: {external_path}\n")
    
    # Setup spdlog
    print("=== Setting up spdlog ===")
    spdlog_dir = external_path / "spdlog"
    spdlog_include = spdlog_dir / "include"
    if not spdlog_include.exists():
        temp_dir = external_path / "spdlog_temp"
        temp_dir.mkdir(exist_ok=True)
        
        zip_file = external_path / "spdlog.zip"
        if download_file(
            "https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.zip",
            str(zip_file)
        ):
            if extract_zip(str(zip_file), str(temp_dir)):
                # Move include directory
                src = temp_dir / "spdlog-1.14.1" / "include"
                spdlog_dir.mkdir(exist_ok=True)
                if src.exists():
                    shutil.copytree(src, spdlog_include, dirs_exist_ok=True)
                    print("✓ spdlog setup complete\n")
            zip_file.unlink(missing_ok=True)
        shutil.rmtree(temp_dir, ignore_errors=True)
    else:
        print("✓ spdlog already installed\n")
    
    # Setup nlohmann_json
    print("=== Setting up nlohmann_json ===")
    json_dir = external_path / "nlohmann_json"
    json_include = json_dir / "include" / "nlohmann"
    if not json_include.exists():
        json_include.mkdir(parents=True, exist_ok=True)
        
        json_hpp = json_dir / "json.hpp"
        if download_file(
            "https://github.com/nlohmann/json/releases/download/v3.11.2/json.hpp",
            str(json_hpp)
        ):
            # Single header - just move it
            shutil.move(str(json_hpp), str(json_include / "json.hpp"))
            print("✓ nlohmann_json setup complete\n")
    else:
        print("✓ nlohmann_json already installed\n")
    
    # Setup moodycamel ConcurrentQueue
    print("=== Setting up moodycamel ConcurrentQueue ===")
    queue_dir = external_path / "concurrentqueue"
    if not queue_dir.exists():
        temp_dir = external_path / "queue_temp"
        temp_dir.mkdir(exist_ok=True)
        
        zip_file = external_path / "concurrentqueue.zip"
        if download_file(
            "https://github.com/cameron314/concurrentqueue/archive/refs/heads/master.zip",
            str(zip_file)
        ):
            if extract_zip(str(zip_file), str(temp_dir)):
                # Move extracted directory
                src = temp_dir / "concurrentqueue-master"
                if src.exists():
                    shutil.move(str(src), str(queue_dir))
                    print("✓ concurrentqueue setup complete\n")
            zip_file.unlink(missing_ok=True)
        shutil.rmtree(temp_dir, ignore_errors=True)
    else:
        print("✓ concurrentqueue already installed\n")
    
    print("=== Dependency setup complete ===")
    print(f"External libraries installed in: {external_path}")
    return True


if __name__ == "__main__":
    external_dir = sys.argv[1] if len(sys.argv) > 1 else "external"
    success = setup_external_deps(external_dir)
    sys.exit(0 if success else 1)
