#!/usr/bin/env python3
"""
Simple end-to-end pipeline test script for MarketPulse.

Tests the complete data flow:
1. MockFeed generates raw market data
2. Normalizer converts to Frame (MarketEvent)
3. Recorder persists to disk
4. Publisher exposes via TCP and HTTP API
"""

import requests
import time
import json
import sys

class PipelineTest:
    def __init__(self, base_url="http://localhost:8080"):
        self.base_url = base_url
        
    def test_health(self):
        """Test system health"""
        print("Testing system health...")
        try:
            response = requests.get(f"{self.base_url}/health", timeout=5)
            if response.status_code == 200:
                health = response.json()
                print(f"✓ System is healthy")
                print(f"  MockFeed: {health.get('components', {}).get('mock_feed', {}).get('total_events', 0)} events")
                print(f"  Normalizer: {health.get('components', {}).get('normalizer', {}).get('frames_output', 0)} frames")
                print(f"  Publisher: {health.get('components', {}).get('publisher', {}).get('frames_published', 0)} published")
                print(f"  Recorder: {health.get('components', {}).get('recorder', {}).get('frames_written', 0)} recorded")
                return True
            else:
                print(f"✗ Health check failed: {response.status_code}")
                return False
        except Exception as e:
            print(f"✗ Cannot connect to MarketPulse: {e}")
            return False
    
    def test_symbols(self):
        """Test symbol registry"""
        print("\nTesting symbol registry...")
        try:
            response = requests.get(f"{self.base_url}/symbols", timeout=5)
            if response.status_code == 200:
                symbols = response.json()
                print(f"✓ Found {symbols.get('count', 0)} registered symbols")
                if symbols.get('symbols'):
                    print(f"  Symbols: {', '.join(symbols['symbols'][:5])}")
                return True
            else:
                print(f"✗ Symbol lookup failed: {response.status_code}")
                return False
        except Exception as e:
            print(f"✗ Error: {e}")
            return False
    
    def test_latest_events(self):
        """Test latest event API (end-to-end validation)"""
        print("\nTesting latest event API...")
        
        # Wait a moment for events to flow through
        print("Waiting 2 seconds for data to flow through pipeline...")
        time.sleep(2)
        
        # Test topics to check
        test_topics = ["l1.AAPL", "l2.AAPL", "trade.AAPL", "l1.MSFT"]
        
        success_count = 0
        for topic in test_topics:
            try:
                response = requests.get(f"{self.base_url}/latest/{topic}", timeout=5)
                if response.status_code == 200:
                    event = response.json()
                    print(f"✓ {topic}: {event.get('type')} event at {event.get('timestamp_ns', 0) / 1e9:.3f}s")
                    
                    # Show event details
                    if event.get('type') == 'L1':
                        print(f"    Bid: {event.get('bid_price')} x {event.get('bid_size')}")
                        print(f"    Ask: {event.get('ask_price')} x {event.get('ask_size')}")
                    elif event.get('type') == 'L2':
                        print(f"    {event.get('side')} {event.get('action')} @ level {event.get('level')}")
                        print(f"    Price: {event.get('price')}, Size: {event.get('size')}")
                    elif event.get('type') == 'Trade':
                        print(f"    Price: {event.get('price')}, Size: {event.get('size')}")
                        print(f"    Aggressor: {event.get('aggressor_side')}")
                    
                    success_count += 1
                elif response.status_code == 404:
                    print(f"⚠ {topic}: No data yet")
                else:
                    print(f"✗ {topic}: Error {response.status_code}")
            except Exception as e:
                print(f"✗ {topic}: {e}")
        
        if success_count > 0:
            print(f"\n✓ Successfully retrieved {success_count}/{len(test_topics)} latest events")
            print("✓ END-TO-END PIPELINE VALIDATION: PASSED")
            return True
        else:
            print("\n✗ No events retrieved - pipeline may not be running")
            return False
    
    def test_metrics(self):
        """Test metrics endpoint"""
        print("\nTesting Prometheus metrics...")
        try:
            response = requests.get(f"{self.base_url}/metrics", timeout=5)
            if response.status_code == 200:
                metrics = response.text
                lines = [line for line in metrics.split('\n') if line and not line.startswith('#')]
                print(f"✓ Metrics endpoint active ({len(lines)} metrics)")
                
                # Show some key metrics
                for line in lines[:5]:
                    print(f"  {line}")
                return True
            else:
                print(f"✗ Metrics failed: {response.status_code}")
                return False
        except Exception as e:
            print(f"✗ Error: {e}")
            return False
    
    def run_all_tests(self):
        """Run complete test suite"""
        print("=" * 60)
        print("MarketPulse End-to-End Pipeline Test")
        print("=" * 60)
        
        results = []
        results.append(("Health Check", self.test_health()))
        results.append(("Symbol Registry", self.test_symbols()))
        results.append(("Latest Events API", self.test_latest_events()))
        results.append(("Metrics", self.test_metrics()))
        
        print("\n" + "=" * 60)
        print("Test Summary:")
        print("=" * 60)
        
        for test_name, passed in results:
            status = "✓ PASS" if passed else "✗ FAIL"
            print(f"{status}: {test_name}")
        
        all_passed = all(result for _, result in results)
        
        print("=" * 60)
        if all_passed:
            print("✓ ALL TESTS PASSED - Pipeline is working end-to-end!")
        else:
            print("✗ SOME TESTS FAILED - Check system status")
        print("=" * 60)
        
        return all_passed

def main():
    # Check if custom URL provided
    base_url = "http://localhost:8080"
    if len(sys.argv) > 1:
        base_url = sys.argv[1]
    
    print(f"Testing MarketPulse at {base_url}\n")
    print("Prerequisites:")
    print("1. Start MarketPulse: ./market_pulse_core")
    print("2. Wait 5-10 seconds for initialization")
    print("3. Run this test script\n")
    
    tester = PipelineTest(base_url)
    success = tester.run_all_tests()
    
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
