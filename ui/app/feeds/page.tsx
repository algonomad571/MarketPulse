'use client';

import React, { useState } from 'react';
import { Radio, Play, Square, Settings, TrendingUp } from 'lucide-react';
import { useApi, apiPost } from '../hooks/useApi';
import LoadingSpinner from '../components/LoadingSpinner';

interface FeedStats {
  l1_count: number;
  l2_count: number;
  trade_count: number;
  total_events: number;
}

interface Feed {
  name: string;
  active: boolean;
  stats: FeedStats;
}

interface FeedsResponse {
  feeds: Feed[];
}

export default function FeedsPage() {
  const [isStarting, setIsStarting] = useState(false);
  const [isStopping, setIsStopping] = useState(false);
  const [rates, setRates] = useState({
    l1_rate: 50000,
    l2_rate: 30000,
    trade_rate: 5000
  });

  const { data, loading, error, refetch } = useApi<FeedsResponse>('/feeds');

  const handleStartFeed = async () => {
    try {
      setIsStarting(true);
      await apiPost('/feeds/start', {
        action: 'start',
        ...rates
      });
      refetch();
    } catch (error) {
      console.error('Failed to start feed:', error);
      alert('Failed to start feed');
    } finally {
      setIsStarting(false);
    }
  };

  const handleStopFeed = async () => {
    try {
      setIsStopping(true);
      await apiPost('/feeds/stop', {
        action: 'stop'
      });
      refetch();
    } catch (error) {
      console.error('Failed to stop feed:', error);
      alert('Failed to stop feed');
    } finally {
      setIsStopping(false);
    }
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center h-full">
        <LoadingSpinner size="lg" />
      </div>
    );
  }

  if (error) {
    return (
      <div className="flex items-center justify-center h-full">
        <div className="text-center">
          <div className="text-red-500 text-xl mb-2">⚠️</div>
          <h3 className="text-lg font-medium text-gray-900 mb-2">Error Loading Feeds</h3>
          <p className="text-gray-600 mb-4">{error}</p>
          <button onClick={refetch} className="btn-primary">
            Retry
          </button>
        </div>
      </div>
    );
  }

  const feeds = data?.feeds || [];
  const mockFeed = feeds.find(f => f.name === 'mock');

  return (
    <div className="p-8">
      <div className="mb-8">
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-3xl font-bold text-gray-900">Feed Management</h1>
            <p className="text-gray-600 mt-1">
              Control market data feeds and configure data generation rates
            </p>
          </div>
        </div>
      </div>

      {/* Feed Controls */}
      <div className="card mb-8">
        <div className="flex items-center justify-between mb-6">
          <h2 className="text-lg font-semibold text-gray-900 flex items-center">
            <Radio className="h-5 w-5 mr-2 text-primary-600" />
            Mock Feed Controller
          </h2>
          <div className="flex items-center space-x-2">
            <span className={`status-badge ${mockFeed?.active ? 'active' : 'inactive'}`}>
              {mockFeed?.active ? 'Active' : 'Inactive'}
            </span>
          </div>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
          {/* Rate Configuration */}
          <div>
            <h3 className="text-sm font-medium text-gray-700 mb-4 flex items-center">
              <Settings className="h-4 w-4 mr-2" />
              Rate Configuration
            </h3>
            <div className="space-y-4">
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">
                  L1 Messages/sec
                </label>
                <input
                  type="number"
                  value={rates.l1_rate}
                  onChange={(e) => setRates(prev => ({ ...prev, l1_rate: parseInt(e.target.value) }))}
                  className="input"
                  min="0"
                  max="200000"
                />
              </div>
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">
                  L2 Messages/sec
                </label>
                <input
                  type="number"
                  value={rates.l2_rate}
                  onChange={(e) => setRates(prev => ({ ...prev, l2_rate: parseInt(e.target.value) }))}
                  className="input"
                  min="0"
                  max="100000"
                />
              </div>
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">
                  Trade Messages/sec
                </label>
                <input
                  type="number"
                  value={rates.trade_rate}
                  onChange={(e) => setRates(prev => ({ ...prev, trade_rate: parseInt(e.target.value) }))}
                  className="input"
                  min="0"
                  max="50000"
                />
              </div>
            </div>
          </div>

          {/* Feed Actions */}
          <div>
            <h3 className="text-sm font-medium text-gray-700 mb-4">
              Feed Actions
            </h3>
            <div className="space-y-4">
              <div className="bg-gray-50 p-4 rounded-lg">
                <h4 className="font-medium text-gray-900 mb-2">Expected Throughput</h4>
                <div className="text-2xl font-bold text-primary-600 mb-1">
                  {(rates.l1_rate + rates.l2_rate + rates.trade_rate).toLocaleString()} msg/s
                </div>
                <div className="text-sm text-gray-500">
                  Total across all message types
                </div>
              </div>
              
              <div className="flex space-x-3">
                {!mockFeed?.active ? (
                  <button
                    onClick={handleStartFeed}
                    disabled={isStarting}
                    className="btn-primary flex items-center"
                  >
                    {isStarting ? (
                      <LoadingSpinner size="sm" className="mr-2" />
                    ) : (
                      <Play className="h-4 w-4 mr-2" />
                    )}
                    {isStarting ? 'Starting...' : 'Start Feed'}
                  </button>
                ) : (
                  <button
                    onClick={handleStopFeed}
                    disabled={isStopping}
                    className="bg-red-600 text-white px-4 py-2 rounded-lg font-medium hover:bg-red-700 focus:outline-none focus:ring-2 focus:ring-red-500 focus:ring-offset-2 transition-colors flex items-center"
                  >
                    {isStopping ? (
                      <LoadingSpinner size="sm" className="mr-2" />
                    ) : (
                      <Square className="h-4 w-4 mr-2" />
                    )}
                    {isStopping ? 'Stopping...' : 'Stop Feed'}
                  </button>
                )}
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Feed Statistics */}
      {mockFeed?.active && mockFeed.stats && (
        <div className="card mb-8">
          <h2 className="text-lg font-semibold text-gray-900 mb-6 flex items-center">
            <TrendingUp className="h-5 w-5 mr-2 text-primary-600" />
            Live Statistics
          </h2>
          
          <div className="grid grid-cols-1 md:grid-cols-4 gap-6">
            <div className="text-center">
              <div className="text-3xl font-bold text-blue-600 mb-2">
                {mockFeed.stats.l1_count.toLocaleString()}
              </div>
              <div className="text-sm text-gray-500 uppercase tracking-wide">L1 Messages</div>
              <div className="w-full bg-gray-200 rounded-full h-2 mt-2">
                <div 
                  className="bg-blue-500 h-2 rounded-full transition-all duration-300"
                  style={{ 
                    width: `${Math.min(100, (mockFeed.stats.l1_count / mockFeed.stats.total_events) * 100)}%` 
                  }}
                ></div>
              </div>
            </div>
            
            <div className="text-center">
              <div className="text-3xl font-bold text-green-600 mb-2">
                {mockFeed.stats.l2_count.toLocaleString()}
              </div>
              <div className="text-sm text-gray-500 uppercase tracking-wide">L2 Messages</div>
              <div className="w-full bg-gray-200 rounded-full h-2 mt-2">
                <div 
                  className="bg-green-500 h-2 rounded-full transition-all duration-300"
                  style={{ 
                    width: `${Math.min(100, (mockFeed.stats.l2_count / mockFeed.stats.total_events) * 100)}%` 
                  }}
                ></div>
              </div>
            </div>
            
            <div className="text-center">
              <div className="text-3xl font-bold text-purple-600 mb-2">
                {mockFeed.stats.trade_count.toLocaleString()}
              </div>
              <div className="text-sm text-gray-500 uppercase tracking-wide">Trade Messages</div>
              <div className="w-full bg-gray-200 rounded-full h-2 mt-2">
                <div 
                  className="bg-purple-500 h-2 rounded-full transition-all duration-300"
                  style={{ 
                    width: `${Math.min(100, (mockFeed.stats.trade_count / mockFeed.stats.total_events) * 100)}%` 
                  }}
                ></div>
              </div>
            </div>
            
            <div className="text-center">
              <div className="text-3xl font-bold text-indigo-600 mb-2">
                {mockFeed.stats.total_events.toLocaleString()}
              </div>
              <div className="text-sm text-gray-500 uppercase tracking-wide">Total Events</div>
              <div className="w-full bg-gray-200 rounded-full h-2 mt-2">
                <div className="bg-indigo-500 h-2 rounded-full w-full"></div>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Performance Guidelines */}
      <div className="card">
        <h2 className="text-lg font-semibold text-gray-900 mb-4">Performance Guidelines</h2>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
          <div className="bg-green-50 p-4 rounded-lg">
            <h3 className="font-medium text-green-800 mb-2">Optimal Range</h3>
            <ul className="text-sm text-green-700 space-y-1">
              <li>• L1: 40k-80k msg/s</li>
              <li>• L2: 20k-50k msg/s</li>
              <li>• Trade: 3k-10k msg/s</li>
              <li>• Total: &lt;150k msg/s</li>
            </ul>
          </div>
          
          <div className="bg-yellow-50 p-4 rounded-lg">
            <h3 className="font-medium text-yellow-800 mb-2">High Load</h3>
            <ul className="text-sm text-yellow-700 space-y-1">
              <li>• L1: 80k-120k msg/s</li>
              <li>• L2: 50k-80k msg/s</li>
              <li>• Trade: 10k-20k msg/s</li>
              <li>• Total: 150k-200k msg/s</li>
            </ul>
          </div>
          
          <div className="bg-red-50 p-4 rounded-lg">
            <h3 className="font-medium text-red-800 mb-2">Critical Load</h3>
            <ul className="text-sm text-red-700 space-y-1">
              <li>• L1: &gt;120k msg/s</li>
              <li>• L2: &gt;80k msg/s</li>
              <li>• Trade: &gt;20k msg/s</li>
              <li>• Total: &gt;200k msg/s</li>
            </ul>
          </div>
        </div>
      </div>
    </div>
  );
}