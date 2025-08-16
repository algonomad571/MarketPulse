'use client';

import React, { useState, useEffect } from 'react';
import { Play, Pause, Square, SkipForward, Clock, Database } from 'lucide-react';
import { apiPost } from '../hooks/useApi';
import LoadingSpinner from '../components/LoadingSpinner';
import { format } from 'date-fns';

interface ReplaySession {
  session_id: string;
  start_ts_ns: number;
  end_ts_ns: number;
  current_ts_ns: number;
  rate_multiplier: number;
  running: boolean;
  paused: boolean;
  frames_sent: number;
  topics: string[];
}

export default function ReplayPage() {
  const [sessions, setSessions] = useState<ReplaySession[]>([]);
  const [isCreating, setIsCreating] = useState(false);
  const [showCreateForm, setShowCreateForm] = useState(false);
  const [formData, setFormData] = useState({
    start_time: '',
    end_time: '',
    rate: 1.0,
    topics: ['*']
  });

  const refreshSessions = async () => {
    // In a real implementation, this would fetch from the API
    // For now, we'll simulate some data
  };

  useEffect(() => {
    refreshSessions();
    const interval = setInterval(refreshSessions, 2000);
    return () => clearInterval(interval);
  }, []);

  const handleCreateSession = async () => {
    try {
      setIsCreating(true);
      
      // Convert datetime-local to nanoseconds
      const startTs = new Date(formData.start_time).getTime() * 1000000;
      const endTs = new Date(formData.end_time).getTime() * 1000000;
      
      const result = await apiPost('/replay/start', {
        action: 'start',
        from_ts_ns: startTs,
        to_ts_ns: endTs,
        rate: formData.rate,
        topics: formData.topics
      });
      
      console.log('Replay session started:', result);
      setShowCreateForm(false);
      refreshSessions();
      
    } catch (error) {
      console.error('Failed to create replay session:', error);
      alert('Failed to create replay session');
    } finally {
      setIsCreating(false);
    }
  };

  const handleSessionAction = async (sessionId: string, action: string) => {
    try {
      await apiPost(`/replay/${action}`, {
        action,
        session_id: sessionId
      });
      refreshSessions();
    } catch (error) {
      console.error(`Failed to ${action} session:`, error);
      alert(`Failed to ${action} session`);
    }
  };

  const formatTimestamp = (ts_ns: number) => {
    return format(new Date(ts_ns / 1000000), 'MMM dd, HH:mm:ss');
  };

  const getProgress = (session: ReplaySession) => {
    const total = session.end_ts_ns - session.start_ts_ns;
    const current = session.current_ts_ns - session.start_ts_ns;
    return Math.max(0, Math.min(100, (current / total) * 100));
  };

  return (
    <div className="p-8">
      <div className="mb-8">
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-3xl font-bold text-gray-900">Replay Sessions</h1>
            <p className="text-gray-600 mt-1">
              Replay historical market data at configurable speeds
            </p>
          </div>
          <button
            onClick={() => setShowCreateForm(true)}
            className="btn-primary flex items-center"
          >
            <Play className="h-4 w-4 mr-2" />
            New Replay Session
          </button>
        </div>
      </div>

      {/* Create Session Modal */}
      {showCreateForm && (
        <div className="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50">
          <div className="bg-white rounded-lg shadow-xl p-6 w-full max-w-md">
            <h2 className="text-lg font-semibold text-gray-900 mb-4">
              Create Replay Session
            </h2>
            
            <div className="space-y-4">
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">
                  Start Time
                </label>
                <input
                  type="datetime-local"
                  value={formData.start_time}
                  onChange={(e) => setFormData(prev => ({ ...prev, start_time: e.target.value }))}
                  className="input w-full"
                />
              </div>
              
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">
                  End Time
                </label>
                <input
                  type="datetime-local"
                  value={formData.end_time}
                  onChange={(e) => setFormData(prev => ({ ...prev, end_time: e.target.value }))}
                  className="input w-full"
                />
              </div>
              
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">
                  Playback Rate
                </label>
                <select
                  value={formData.rate}
                  onChange={(e) => setFormData(prev => ({ ...prev, rate: parseFloat(e.target.value) }))}
                  className="input w-full"
                >
                  <option value={0.1}>0.1x (Slow)</option>
                  <option value={0.5}>0.5x</option>
                  <option value={1.0}>1x (Real-time)</option>
                  <option value={10.0}>10x</option>
                  <option value={50.0}>50x</option>
                  <option value={100.0}>100x (Fast)</option>
                </select>
              </div>
              
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">
                  Topics (comma-separated)
                </label>
                <input
                  type="text"
                  value={formData.topics.join(', ')}
                  onChange={(e) => setFormData(prev => ({ 
                    ...prev, 
                    topics: e.target.value.split(',').map(t => t.trim()) 
                  }))}
                  placeholder="l1.*, trade.BTCUSDT"
                  className="input w-full"
                />
              </div>
            </div>
            
            <div className="flex justify-end space-x-3 mt-6">
              <button
                onClick={() => setShowCreateForm(false)}
                className="btn-secondary"
              >
                Cancel
              </button>
              <button
                onClick={handleCreateSession}
                disabled={isCreating}
                className="btn-primary flex items-center"
              >
                {isCreating ? (
                  <LoadingSpinner size="sm" className="mr-2" />
                ) : (
                  <Play className="h-4 w-4 mr-2" />
                )}
                {isCreating ? 'Starting...' : 'Start Replay'}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Active Sessions */}
      <div className="space-y-6">
        {sessions.length === 0 ? (
          <div className="card">
            <div className="text-center py-12">
              <Database className="h-12 w-12 text-gray-300 mx-auto mb-4" />
              <h3 className="text-lg font-medium text-gray-900 mb-2">No active replay sessions</h3>
              <p className="text-gray-500 mb-4">
                Create a new replay session to start streaming historical data
              </p>
              <button
                onClick={() => setShowCreateForm(true)}
                className="btn-primary"
              >
                Create Replay Session
              </button>
            </div>
          </div>
        ) : (
          sessions.map((session) => (
            <div key={session.session_id} className="card">
              <div className="flex items-center justify-between mb-4">
                <div>
                  <h3 className="text-lg font-semibold text-gray-900">
                    Session {session.session_id.slice(-8)}
                  </h3>
                  <p className="text-sm text-gray-600">
                    {formatTimestamp(session.start_ts_ns)} → {formatTimestamp(session.end_ts_ns)}
                  </p>
                </div>
                <div className="flex items-center space-x-2">
                  <span className={`status-badge ${
                    session.running ? (session.paused ? 'inactive' : 'active') : 'error'
                  }`}>
                    {session.running ? (session.paused ? 'Paused' : 'Running') : 'Stopped'}
                  </span>
                  <span className="text-sm text-gray-500">
                    {session.rate_multiplier}x speed
                  </span>
                </div>
              </div>

              {/* Progress Bar */}
              <div className="mb-4">
                <div className="flex justify-between text-sm text-gray-600 mb-2">
                  <span>Progress</span>
                  <span>{Math.round(getProgress(session))}%</span>
                </div>
                <div className="w-full bg-gray-200 rounded-full h-2">
                  <div
                    className="bg-primary-600 h-2 rounded-full transition-all duration-300"
                    style={{ width: `${getProgress(session)}%` }}
                  ></div>
                </div>
                <div className="flex justify-between text-xs text-gray-500 mt-1">
                  <span>Current: {formatTimestamp(session.current_ts_ns)}</span>
                  <span>{session.frames_sent.toLocaleString()} frames sent</span>
                </div>
              </div>

              {/* Session Controls */}
              <div className="flex items-center justify-between">
                <div className="flex items-center space-x-2">
                  {session.running ? (
                    <>
                      {session.paused ? (
                        <button
                          onClick={() => handleSessionAction(session.session_id, 'resume')}
                          className="btn-primary flex items-center text-sm px-3 py-2"
                        >
                          <Play className="h-4 w-4 mr-1" />
                          Resume
                        </button>
                      ) : (
                        <button
                          onClick={() => handleSessionAction(session.session_id, 'pause')}
                          className="btn-secondary flex items-center text-sm px-3 py-2"
                        >
                          <Pause className="h-4 w-4 mr-1" />
                          Pause
                        </button>
                      )}
                      <button
                        onClick={() => handleSessionAction(session.session_id, 'stop')}
                        className="text-red-600 hover:text-red-700 flex items-center text-sm px-3 py-2"
                      >
                        <Square className="h-4 w-4 mr-1" />
                        Stop
                      </button>
                    </>
                  ) : (
                    <span className="text-gray-500 text-sm">Session ended</span>
                  )}
                </div>
                
                <div className="text-sm text-gray-600">
                  Topics: {session.topics.join(', ')}
                </div>
              </div>
            </div>
          ))
        )}
      </div>

      {/* Performance Note */}
      <div className="card bg-blue-50 border-blue-200">
        <div className="flex">
          <div className="flex-shrink-0">
            <Clock className="h-5 w-5 text-blue-400" />
          </div>
          <div className="ml-3">
            <h3 className="text-sm font-medium text-blue-800">
              Performance Guidelines
            </h3>
            <div className="mt-2 text-sm text-blue-700">
              <ul className="space-y-1">
                <li>• Replay sessions consume the same pub-sub channels as live data</li>
                <li>• High replay rates (100x) may cause backpressure with slow subscribers</li>
                <li>• Use topic filters to reduce data volume for specific analysis</li>
                <li>• Each session gets a unique topic prefix: replay.&lt;session_id&gt;.*</li>
              </ul>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}