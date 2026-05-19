'use client';

import React, { useState, useEffect } from 'react';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
  BarChart,
  Bar,
  Area,
  AreaChart
} from 'recharts';
import { Clock, Activity, Database, TrendingUp, AlertTriangle } from 'lucide-react';
import { useWebSocket } from '../hooks/useWebSocket';
import MetricsCard from '../components/MetricsCard';

const WS_URL = process.env.NEXT_PUBLIC_WS_URL;

interface MetricsData {
  timestamp_ns: number;
  throughput?: {
    feed_events_ingested_total?: number;
    normalizer_events_total?: number;
    publisher_frames_published_total?: number;
    recorder_frames_total?: number;
    frame_distribution_total?: number;
  };
  latency?: {
    normalizer_event_latency_ns?: { p50?: number; p99?: number };
    publisher_publish_latency_ns?: { p50?: number; p99?: number };
    recorder_write_frame_ns?: { p50?: number; p99?: number };
  };
  queue_depths?: {
    pipeline_feed_queue_approx?: number;
    pipeline_normalizer_to_publisher_queue_approx?: number;
    pipeline_normalizer_to_recorder_queue_approx?: number;
  };
}

interface TimeSeriesData {
  timestamp: number;
  throughput: number;
  latency_p50: number;
  latency_p99: number;
  queue_depth: number;
  connections: number;
}

export default function MetricsPage() {
  const [metricsHistory, setMetricsHistory] = useState<TimeSeriesData[]>([]);
  const [currentMetrics, setCurrentMetrics] = useState<MetricsData | null>(null);
  const [timeRange, setTimeRange] = useState<'5m' | '15m' | '1h' | '4h'>('15m');
  
  const { isConnected } = useWebSocket(WS_URL, {
    onMessage: (data: MetricsData) => {
      setCurrentMetrics(data);
      
      const throughput = data.throughput || {};
      const latency = data.latency?.normalizer_event_latency_ns || {};
      const queueDepths = data.queue_depths || {};

      // Add to history
      const newDataPoint: TimeSeriesData = {
        timestamp: Date.now(),
        throughput: throughput.feed_events_ingested_total || 0,
        latency_p50: latency.p50 ? Math.round(latency.p50 / 1000) : 0,
        latency_p99: latency.p99 ? Math.round(latency.p99 / 1000) : 0,
        queue_depth: queueDepths.pipeline_feed_queue_approx || 0,
        connections: queueDepths.pipeline_normalizer_to_publisher_queue_approx || 0
      };
      
      setMetricsHistory(prev => {
        const updated = [...prev, newDataPoint].slice(-200); // Keep last 200 points
        return updated;
      });
    }
  });

  const formatTimestamp = (timestamp: number) => {
    return new Date(timestamp).toLocaleTimeString();
  };

  const formatNumber = (num: number) => {
    if (num >= 1000000) return `${(num / 1000000).toFixed(1)}M`;
    if (num >= 1000) return `${(num / 1000).toFixed(1)}K`;
    return num.toString();
  };

  const getLatencyColor = (latency: number) => {
    if (latency < 1000) return 'text-green-600'; // < 1ms
    if (latency < 5000) return 'text-yellow-600'; // < 5ms
    return 'text-red-600'; // >= 5ms
  };

  const currentThroughput = currentMetrics?.throughput?.feed_events_ingested_total || 0;

  const currentLatency = currentMetrics?.latency?.normalizer_event_latency_ns;
  const currentQueueDepths = currentMetrics?.queue_depths || {};

  return (
    <div className="p-8">
      <div className="mb-8">
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-3xl font-bold text-gray-900">Performance Metrics</h1>
            <p className="text-gray-600 mt-1">
              Real-time monitoring of system performance and throughput
            </p>
          </div>
          <div className="flex items-center space-x-4">
            <div className="flex items-center space-x-2">
              <div className={`h-3 w-3 rounded-full ${isConnected ? 'bg-green-500' : 'bg-red-500'} animate-pulse`}></div>
              <span className="text-sm text-gray-600">
                {isConnected ? 'Live Data' : 'Disconnected'}
              </span>
            </div>
            <select
              value={timeRange}
              onChange={(e) => setTimeRange(e.target.value as any)}
              className="input text-sm"
            >
              <option value="5m">Last 5 minutes</option>
              <option value="15m">Last 15 minutes</option>
              <option value="1h">Last 1 hour</option>
              <option value="4h">Last 4 hours</option>
            </select>
          </div>
        </div>
      </div>

      {/* Key Metrics Cards */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6 mb-8">
        <MetricsCard
          title="Throughput"
          value={formatNumber(currentThroughput)}
          unit="msg/s"
          icon={<Activity className="h-8 w-8" />}
          color="blue"
        />
        
        <MetricsCard
          title="P50 Latency"
          value={currentLatency?.p50 ? Math.round(currentLatency.p50 / 1000) : 0}
          unit="μs"
          icon={<Clock className="h-8 w-8" />}
          color="green"
        />
        
        <MetricsCard
          title="P99 Latency"
          value={currentLatency?.p99 ? Math.round(currentLatency.p99 / 1000) : 0}
          unit="μs"
          icon={<AlertTriangle className="h-8 w-8" />}
          color={currentLatency?.p99 && currentLatency.p99 > 10000000 ? "red" : "yellow"}
        />
        
        <MetricsCard
          title="Queue Depth"
          value={currentQueueDepths.pipeline_feed_queue_approx || 0}
          icon={<Database className="h-8 w-8" />}
          color="purple"
        />
      </div>

      {/* Charts Grid */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-8 mb-8">
        {/* Throughput Chart */}
        <div className="card">
          <h3 className="text-lg font-semibold text-gray-900 mb-4 flex items-center">
            <TrendingUp className="h-5 w-5 mr-2 text-primary-600" />
            Message Throughput
          </h3>
          <ResponsiveContainer width="100%" height={300}>
            <AreaChart data={metricsHistory}>
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis
                dataKey="timestamp"
                tickFormatter={formatTimestamp}
                fontSize={12}
              />
              <YAxis tickFormatter={formatNumber} fontSize={12} />
              <Tooltip
                labelFormatter={formatTimestamp}
                formatter={(value: number) => [formatNumber(value), 'Messages/sec']}
              />
              <Area
                type="monotone"
                dataKey="throughput"
                stroke="#3b82f6"
                fill="#3b82f6"
                fillOpacity={0.3}
              />
            </AreaChart>
          </ResponsiveContainer>
        </div>

        {/* Latency Chart */}
        <div className="card">
          <h3 className="text-lg font-semibold text-gray-900 mb-4 flex items-center">
            <Clock className="h-5 w-5 mr-2 text-primary-600" />
            Processing Latency
          </h3>
          <ResponsiveContainer width="100%" height={300}>
            <LineChart data={metricsHistory}>
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis
                dataKey="timestamp"
                tickFormatter={formatTimestamp}
                fontSize={12}
              />
              <YAxis 
                tickFormatter={(value) => `${value}μs`}
                fontSize={12}
              />
              <Tooltip
                labelFormatter={formatTimestamp}
                formatter={(value: number) => [`${value}μs`, '']}
              />
              <Legend />
              <Line
                type="monotone"
                dataKey="latency_p50"
                stroke="#10b981"
                strokeWidth={2}
                name="P50"
                dot={false}
              />
              <Line
                type="monotone"
                dataKey="latency_p99"
                stroke="#ef4444"
                strokeWidth={2}
                name="P99"
                dot={false}
              />
            </LineChart>
          </ResponsiveContainer>
        </div>
      </div>

      {/* Detailed Metrics Tables */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
        {/* Throughput */}
        <div className="card">
          <h3 className="text-lg font-semibold text-gray-900 mb-4">
            Throughput
          </h3>
          <div className="space-y-3">
            {currentMetrics?.throughput && Object.entries(currentMetrics.throughput).map(([key, value]) => (
              <div key={key} className="flex justify-between items-center py-2 border-b border-gray-100 last:border-b-0">
                <span className="text-sm text-gray-600">
                  {key.replace(/_/g, ' ').replace(/\b\w/g, l => l.toUpperCase())}
                </span>
                <span className="font-mono font-medium text-gray-900">
                  {value.toLocaleString()}
                </span>
              </div>
            ))}
          </div>
        </div>

        {/* Queue Depths */}
        <div className="card">
          <h3 className="text-lg font-semibold text-gray-900 mb-4">
            Queue Depths
          </h3>
          <div className="space-y-3">
            {currentMetrics?.queue_depths && Object.entries(currentMetrics.queue_depths).map(([key, value]) => (
              <div key={key} className="border border-gray-200 rounded-lg p-3">
                <h4 className="font-medium text-gray-900 mb-2">
                  {key.replace(/_/g, ' ').replace(/\b\w/g, l => l.toUpperCase())}
                </h4>
                <div className="flex justify-between text-sm">
                  <span className="text-gray-600">Depth:</span>
                  <span className="font-mono text-gray-900">
                    {value.toLocaleString()}
                  </span>
                </div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}