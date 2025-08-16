'use client';

import React from 'react';
import { Database, Plus, Search } from 'lucide-react';
import { useApi } from '../hooks/useApi';
import LoadingSpinner from '../components/LoadingSpinner';

interface Symbol {
  id: number;
  symbol: string;
}

interface SymbolsResponse {
  symbols: Symbol[];
  count: number;
}

export default function SymbolsPage() {
  const { data, loading, error, refetch } = useApi<SymbolsResponse>('/symbols');

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
          <h3 className="text-lg font-medium text-gray-900 mb-2">Error Loading Symbols</h3>
          <p className="text-gray-600 mb-4">{error}</p>
          <button onClick={refetch} className="btn-primary">
            Retry
          </button>
        </div>
      </div>
    );
  }

  const symbols = data?.symbols || [];

  return (
    <div className="p-8">
      <div className="mb-8">
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-3xl font-bold text-gray-900">Symbol Registry</h1>
            <p className="text-gray-600 mt-1">
              Manage trading symbols and their internal identifiers
            </p>
          </div>
          <div className="text-sm text-gray-500">
            Total: {data?.count || 0} symbols
          </div>
        </div>
      </div>

      {/* Search and Actions */}
      <div className="card mb-6">
        <div className="flex items-center justify-between">
          <div className="flex items-center space-x-4">
            <div className="relative">
              <Search className="absolute left-3 top-1/2 transform -translate-y-1/2 h-4 w-4 text-gray-400" />
              <input
                type="text"
                placeholder="Search symbols..."
                className="pl-10 pr-4 py-2 w-80 border border-gray-300 rounded-lg focus:ring-2 focus:ring-primary-500 focus:border-transparent"
              />
            </div>
          </div>
          <button className="btn-primary flex items-center">
            <Plus className="h-4 w-4 mr-2" />
            Add Symbol
          </button>
        </div>
      </div>

      {/* Symbols Table */}
      <div className="card">
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-lg font-semibold text-gray-900 flex items-center">
            <Database className="h-5 w-5 mr-2 text-primary-600" />
            Registered Symbols
          </h2>
        </div>
        
        {symbols.length === 0 ? (
          <div className="text-center py-12">
            <Database className="h-12 w-12 text-gray-300 mx-auto mb-4" />
            <h3 className="text-lg font-medium text-gray-900 mb-2">No symbols registered</h3>
            <p className="text-gray-500 mb-4">
              Symbols will appear here once the feed starts processing data
            </p>
            <button className="btn-primary">
              Start Feed
            </button>
          </div>
        ) : (
          <div className="overflow-hidden shadow ring-1 ring-black ring-opacity-5 md:rounded-lg">
            <table className="min-w-full divide-y divide-gray-300">
              <thead className="bg-gray-50">
                <tr>
                  <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wide">
                    Symbol ID
                  </th>
                  <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wide">
                    Symbol
                  </th>
                  <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wide">
                    Status
                  </th>
                  <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wide">
                    Actions
                  </th>
                </tr>
              </thead>
              <tbody className="bg-white divide-y divide-gray-200">
                {symbols.map((symbol) => (
                  <tr key={symbol.id} className="hover:bg-gray-50">
                    <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900">
                      #{symbol.id}
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap">
                      <div className="text-sm font-medium text-gray-900">
                        {symbol.symbol}
                      </div>
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap">
                      <span className="status-badge active">
                        Active
                      </span>
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-500">
                      <button className="text-primary-600 hover:text-primary-900 mr-4">
                        View Details
                      </button>
                      <button className="text-red-600 hover:text-red-900">
                        Remove
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      {/* Symbol Statistics */}
      {symbols.length > 0 && (
        <div className="grid grid-cols-1 md:grid-cols-3 gap-6 mt-8">
          <div className="card">
            <h3 className="text-sm font-medium text-gray-500 uppercase tracking-wide">
              Most Active
            </h3>
            <div className="mt-2">
              <div className="text-2xl font-bold text-gray-900">BTCUSDT</div>
              <div className="text-sm text-gray-500">1.2M messages today</div>
            </div>
          </div>
          
          <div className="card">
            <h3 className="text-sm font-medium text-gray-500 uppercase tracking-wide">
              Recently Added
            </h3>
            <div className="mt-2">
              <div className="text-2xl font-bold text-gray-900">{symbols[symbols.length - 1]?.symbol || 'None'}</div>
              <div className="text-sm text-gray-500">Latest symbol</div>
            </div>
          </div>
          
          <div className="card">
            <h3 className="text-sm font-medium text-gray-500 uppercase tracking-wide">
              Total Symbols
            </h3>
            <div className="mt-2">
              <div className="text-2xl font-bold text-gray-900">{symbols.length}</div>
              <div className="text-sm text-gray-500">Registered symbols</div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}