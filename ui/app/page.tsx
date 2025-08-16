'use client';

import React, { useState, useEffect } from 'react';
import { 
  Database, 
  Radio, 
  Activity, 
  HardDrive, 
  Clock,
  Users
} from 'lucide-react';
import MetricsCard from './components/MetricsCard';
import LoadingSpinner from './components/LoadingSpinner';
import { useWebSocket } from './hooks/useWebSocket';
import { useApi } from './hooks/useApi';

interface HealthData {
  status: string;
  timestamp: number;
  components: {
    mock_feed?: {
      l1_count: number;
      l2_count: number;
      trade_count: number;
      total_events: number;
    };
    normalizer?: {
      events_processed: number;
      frames_output: number;
      errors: number;
    };
    publisher?: {
      total_connections: number;
      active_connections: number;
      frames_published: number;
      frames_dropped: number;
    };
    recorder?: {
      frames_written: number;
      bytes_written: number;
      is_recording: boolean;
    };
  };
}

interface MetricsData {
  timestamp_ns: number;
  counters: Record<string, number>;
  gauges: Record<string, number>;
  histograms: Record<string, {
    p50: number;
    p95: number;
    p99: number;
    p999: number;
    max: number;
    count: number;
  }>;
}

export default function OverviewPage() {
  const [metricsData, setMetricsData] = useState<MetricsData | null>(null);
  const { data: healthData, loading: healthLoading, error: healthError } = useApi<HealthData>('/health');
  
  // WebSocket connection for real-time metrics (would connect to ws://localhost:8081/ws/metrics)
  const { lastMessage, isConnected } = useWebSocket('ws://localhost:8081/ws/metrics', {
    onMessage: (data: MetricsData) => {
      setMetricsData(data);
    },
    onError: (error) => {
      console.error('WebSocket error:', error);
    }
  });

  // Calculate throughput from counters
  const getThroughput = () => {
    if (!metricsData) return { l1: 0, l2: 0, trade: 0, total: 0 };
    
    const l1 = metricsData.counters['mock_feed_l1_total'] || 0;
    const l2 = metricsData.counters['mock_feed_l2_total'] || 0;
    const trade = metricsData.counters['mock_feed_trade_total'] || 0;
    
    return {
      l1,
      l2,
      trade,
      total: l1 + l2 + trade
    };
  };

  // Get latency metrics
  const getLatencyMetrics = () => {
    if (!metricsData?.histograms) return { p50: 0, p95: 0, p99: 0 };
    
    const ingestNorm = metricsData.histograms['normalize_event_ns'];
    return {
      p50: ingestNorm?.p50 ? Math.round(ingestNorm.p50 / 1000) : 0, // Convert ns to μs
      p95: ingestNorm?.p95 ? Math.round(ingestNorm.p95 / 1000) : 0,
      p99: ingestNorm?.p99 ? Math.round(ingestNorm.p99 / 1000) : 0,
    };
  };

  if (healthLoading) {
    return (
      <div className="flex items-center justify-center h-full">
        <LoadingSpinner size="lg" />
      </div>
    );
  }

  if (healthError) {
    return (
      <div className="flex items-center justify-center h-full">
        <div className="text-center">
          <div className="text-red-500 text-xl mb-2">⚠️</div>
          <h3 className="text-lg font-medium text-gray-900 mb-2">Connection Error</h3>
          <p className="text-gray-600">{healthError}</p>
        </div>
      </div>
    );
  }

  const throughput = getThroughput();
  const latency = getLatencyMetrics();
  const components = healthData?.components || {};

  return (
    <div className="p-8">
      <div className="mb-8">
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-3xl font-bold text-gray-900">System Overview</h1>
            <p className="text-gray-600 mt-1">
              Real-time monitoring of market data feed handler performance
            </p>
          </div>
          <div className="flex items-center space-x-3">
            <div className={`h-3 w-3 rounded-full ${isConnected ? 'bg-green-500' : 'bg-red-500'} animate-pulse`}></div>
            <span className="text-sm text-gray-600">
              {isConnected ? 'Live Data' : 'Disconnected'}
            </span>
          </div>
        </div>
      </div>

      {/* Key Metrics Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6 mb-8">
        <MetricsCard
          title="Total Messages/sec"
          value={throughput.total}
          unit="msg/s"
          icon={<Activity className="h-8 w-8" />}
          color="blue"
        />
        
        <MetricsCard
          title="P99 Latency"
          value={latency.p99}
          unit="μs"
          icon={<Clock className="h-8 w-8" />}
          color="green"
        />
        
        <MetricsCard
          title="Active Connections"
          value={components.publisher?.active_connections || 0}
          icon={<Users className="h-8 w-8" />}
          color="purple"
        />
        
        <MetricsCard
          title="Data Written"
          value={
            components.recorder?.bytes_written 
              ? Math.round(components.recorder.bytes_written / (1024 * 1024))
              : 0
          }
          unit="MB"
          icon={<HardDrive className="h-8 w-8" />}
          color="indigo"
        />
      </div>

      {/* Component Status */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-8 mb-8">
        {/* Feed Status */}
        <div className="card">
          <h2 className="text-lg font-semibold text-gray-900 mb-4 flex items-center">
            <Radio className="h-5 w-5 mr-2 text-primary-600" />
            Feed Status
          </h2>
          <div className="space-y-4">
            <div className="flex justify-between items-center p-3 bg-gray-50 rounded-lg">
              <span className="font-medium text-gray-700">Mock Feed</span>
              <span className="status-badge active">Active</span>
            </div>
            <div className="grid grid-cols-3 gap-4 text-center">
              <div>
                <div className="text-2xl font-bold text-blue-600">
                  {components.mock_feed?.l1_count || 0}
                </div>
                <div className="text-xs text-gray-500 uppercase">L1 Messages</div>
              </div>
              <div>
                <div className="text-2xl font-bold text-green-600">
                  {components.mock_feed?.l2_count || 0}
                </div>
                <div className="text-xs text-gray-500 uppercase">L2 Messages</div>
              </div>
              <div>
                <div className="text-2xl font-bold text-purple-600">
                  {components.mock_feed?.trade_count || 0}
                </div>
                <div className="text-xs text-gray-500 uppercase">Trades</div>
              </div>
            </div>
          </div>
        </div>

        {/* Processing Pipeline */}
        <div className="card">
          <h2 className="text-lg font-semibold text-gray-900 mb-4 flex items-center">
            <Database className="h-5 w-5 mr-2 text-primary-600" />
            Processing Pipeline
          </h2>
          <div className="space-y-4">
            <div className="flex justify-between items-center">
              <span className="text-gray-700">Events Processed</span>
              <span className="font-bold text-gray-900">
                {(components.normalizer?.events_processed || 0).toLocaleString()}
              </span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-gray-700">Frames Published</span>
              <span className="font-bold text-gray-900">
                {(components.publisher?.frames_published || 0).toLocaleString()}
              </span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-gray-700">Frames Recorded</span>
              <span className="font-bold text-gray-900">
                {(components.recorder?.frames_written || 0).toLocaleString()}
              </span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-gray-700">Processing Errors</span>
              <span className="font-bold text-red-600">
                {components.normalizer?.errors || 0}
              </span>
            </div>
          </div>
        </div>
      </div>

      {/* Performance Indicators */}
      <div className="card">
        <h2 className="text-lg font-semibold text-gray-900 mb-4">
          Performance Indicators
        </h2>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
          <div className="text-center">
            <div className="text-3xl font-bold text-green-600 mb-2">
              {latency.p50}μs
            </div>
            <div className="text-sm text-gray-500">P50 Latency</div>
            <div className="w-full bg-gray-200 rounded-full h-2 mt-2">
              <div 
                className="bg-green-500 h-2 rounded-full transition-all duration-300"
                style={{ width: `${Math.min(100, (latency.p50 / 10000) * 100)}%` }}
              ></div>
            </div>
          </div>
          
          <div className="text-center">
            <div className="text-3xl font-bold text-yellow-600 mb-2">
              {latency.p95}μs
            </div>
            <div className="text-sm text-gray-500">P95 Latency</div>
            <div className="w-full bg-gray-200 rounded-full h-2 mt-2">
              <div 
                className="bg-yellow-500 h-2 rounded-full transition-all duration-300"
                style={{ width: `${Math.min(100, (latency.p95 / 10000) * 100)}%` }}
              ></div>
            </div>
          </div>
          
          <div className="text-center">
            <div className="text-3xl font-bold text-red-600 mb-2">
              {latency.p99}μs
            </div>
            <div className="text-sm text-gray-500">P99 Latency</div>
            <div className="w-full bg-gray-200 rounded-full h-2 mt-2">
              <div 
                className="bg-red-500 h-2 rounded-full transition-all duration-300"
                style={{ width: `${Math.min(100, (latency.p99 / 10000) * 100)}%` }}
              ></div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}