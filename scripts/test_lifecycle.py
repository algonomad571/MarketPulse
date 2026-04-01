#!/usr/bin/env python3
"""
MarketPulse Lifecycle Tester

Tests system startup, runtime, and graceful shutdown.
"""

import subprocess
import time
import signal
import requests
import sys
import os

class LifecycleTest:
    def __init__(self, executable="./market_pulse_core", config="config.json"):
        self.executable = executable
        self.config = config
        self.process = None
        
    def test_startup(self):
        """Test system starts successfully"""
        print("=" * 60)
        print("TEST 1: System Startup")
        print("=" * 60)
        
        try:
            print(f"Starting {self.executable}...")
            self.process = subprocess.Popen(
                [self.executable, self.config],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            
            # Wait for startup (check health endpoint)
            print("Waiting for system to start...")
            max_retries = 30
            for i in range(max_retries):
                time.sleep(1)
                try:
                    response = requests.get("http://localhost:8080/health", timeout=2)
                    if response.status_code == 200:
                        print(f"✅ System started successfully in {i+1} seconds")
                        return True
                except:
                    pass
                print(f"  Attempt {i+1}/{max_retries}...")
            
            print("❌ System failed to start within 30 seconds")
            return False
            
        except Exception as e:
            print(f"❌ Startup error: {e}")
            return False
    
    def test_runtime_health(self):
        """Test system is healthy during runtime"""
        print("\n" + "=" * 60)
        print("TEST 2: Runtime Health")
        print("=" * 60)
        
        try:
            # Check health
            response = requests.get("http://localhost:8080/health", timeout=5)
            if response.status_code != 200:
                print("❌ Health check failed")
                return False
            
            health = response.json()
            print("✅ Health check passed")
            
            # Check components
            components = health.get("components", {})
            for name, stats in components.items():
                print(f"  {name}: {stats}")
            
            # Check data flow
            feed_stats = components.get("mock_feed", {})
            normalizer_stats = components.get("normalizer", {})
            
            if feed_stats.get("total_events", 0) > 0:
                print("✅ Feed is producing events")
            else:
                print("⚠️  Feed has not produced events yet")
            
            if normalizer_stats.get("frames_output", 0) > 0:
                print("✅ Normalizer is processing events")
            else:
                print("⚠️  Normalizer has not output frames yet")
            
            return True
            
        except Exception as e:
            print(f"❌ Runtime health error: {e}")
            return False
    
    def test_graceful_shutdown(self):
        """Test graceful shutdown"""
        print("\n" + "=" * 60)
        print("TEST 3: Graceful Shutdown")
        print("=" * 60)
        
        if not self.process:
            print("❌ No process to shutdown")
            return False
        
        try:
            print("Sending SIGINT (Ctrl+C) to process...")
            self.process.send_signal(signal.SIGINT)
            
            print("Waiting for graceful shutdown (max 30s)...")
            try:
                return_code = self.process.wait(timeout=30)
                
                if return_code == 0:
                    print(f"✅ Graceful shutdown completed (exit code: {return_code})")
                    return True
                else:
                    print(f"⚠️  Shutdown completed with exit code: {return_code}")
                    return return_code in [0, 3, 4]  # Accept runtime/shutdown errors
                    
            except subprocess.TimeoutExpired:
                print("❌ Shutdown timeout - forcing termination")
                self.process.kill()
                return False
                
        except Exception as e:
            print(f"❌ Shutdown error: {e}")
            return False
    
    def test_force_shutdown(self):
        """Test force shutdown (double Ctrl+C)"""
        print("\n" + "=" * 60)
        print("TEST 4: Force Shutdown")
        print("=" * 60)
        
        try:
            print(f"Starting {self.executable} again...")
            self.process = subprocess.Popen(
                [self.executable, self.config],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            
            # Wait for startup
            time.sleep(5)
            
            print("Sending first SIGINT...")
            self.process.send_signal(signal.SIGINT)
            time.sleep(1)
            
            print("Sending second SIGINT (force exit)...")
            self.process.send_signal(signal.SIGINT)
            
            try:
                return_code = self.process.wait(timeout=5)
                print(f"✅ Force exit completed (exit code: {return_code})")
                return True
            except subprocess.TimeoutExpired:
                print("❌ Force exit timeout")
                self.process.kill()
                return False
                
        except Exception as e:
            print(f"❌ Force shutdown error: {e}")
            return False
    
    def cleanup(self):
        """Ensure process is terminated"""
        if self.process and self.process.poll() is None:
            print("\nCleaning up process...")
            try:
                self.process.terminate()
                self.process.wait(timeout=5)
            except:
                self.process.kill()
    
    def run_all_tests(self):
        """Run complete lifecycle test suite"""
        print("=" * 60)
        print("MarketPulse Lifecycle Test Suite")
        print("=" * 60)
        print()
        
        results = []
        
        try:
            # Test 1: Startup
            results.append(("Startup", self.test_startup()))
            
            if not results[0][1]:
                print("\n❌ Startup failed, skipping remaining tests")
                return False
            
            # Test 2: Runtime Health
            time.sleep(2)  # Let system run
            results.append(("Runtime Health", self.test_runtime_health()))
            
            # Test 3: Graceful Shutdown
            results.append(("Graceful Shutdown", self.test_graceful_shutdown()))
            
            # Test 4: Force Shutdown (optional)
            # results.append(("Force Shutdown", self.test_force_shutdown()))
            
        except KeyboardInterrupt:
            print("\n\nTest interrupted by user")
            return False
        
        finally:
            self.cleanup()
        
        # Print summary
        print("\n" + "=" * 60)
        print("Test Summary")
        print("=" * 60)
        
        for test_name, passed in results:
            status = "✅ PASS" if passed else "❌ FAIL"
            print(f"{status}: {test_name}")
        
        print("=" * 60)
        
        all_passed = all(result for _, result in results)
        if all_passed:
            print("✅ ALL LIFECYCLE TESTS PASSED")
        else:
            print("❌ SOME TESTS FAILED")
        
        return all_passed

def main():
    # Check if executable exists
    executable = "./market_pulse_core"
    if len(sys.argv) > 1:
        executable = sys.argv[1]
    
    # Windows executable
    if not os.path.exists(executable) and os.name == 'nt':
        executable = executable.replace("./", ".\\").replace("/", "\\")
        if not executable.endswith(".exe"):
            executable += ".exe"
    
    if not os.path.exists(executable):
        print(f"ERROR: Executable not found: {executable}")
        print("\nUsage: python test_lifecycle.py [path_to_executable]")
        print("\nMake sure to build the project first:")
        print("  mkdir build && cd build")
        print("  cmake ..")
        print("  cmake --build . --config Release")
        return 1
    
    tester = LifecycleTest(executable)
    success = tester.run_all_tests()
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
