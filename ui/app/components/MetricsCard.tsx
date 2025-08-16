import React from 'react';
import { TrendingUp, TrendingDown } from 'lucide-react';

interface MetricsCardProps {
  title: string;
  value: string | number;
  unit?: string;
  change?: {
    value: number;
    type: 'increase' | 'decrease' | 'neutral';
  };
  icon?: React.ReactNode;
  color?: 'blue' | 'green' | 'yellow' | 'red' | 'purple' | 'indigo';
}

const colorClasses = {
  blue: 'text-blue-600',
  green: 'text-green-600', 
  yellow: 'text-yellow-600',
  red: 'text-red-600',
  purple: 'text-purple-600',
  indigo: 'text-indigo-600',
};

export default function MetricsCard({ 
  title, 
  value, 
  unit, 
  change, 
  icon, 
  color = 'blue' 
}: MetricsCardProps) {
  return (
    <div className="metric-card hover:shadow-md transition-shadow duration-200">
      <div className="flex items-center justify-between">
        <div className="flex-1">
          <h3 className="text-sm font-medium text-gray-500 uppercase tracking-wide">
            {title}
          </h3>
          <div className="flex items-baseline mt-2">
            <span className="text-2xl font-bold text-gray-900">
              {typeof value === 'number' ? value.toLocaleString() : value}
            </span>
            {unit && (
              <span className="ml-1 text-sm text-gray-500">
                {unit}
              </span>
            )}
          </div>
          {change && (
            <div className="flex items-center mt-2">
              {change.type === 'increase' ? (
                <TrendingUp className="h-4 w-4 text-green-500" />
              ) : change.type === 'decrease' ? (
                <TrendingDown className="h-4 w-4 text-red-500" />
              ) : null}
              <span className={`text-sm font-medium ml-1 ${
                change.type === 'increase' ? 'text-green-600' : 
                change.type === 'decrease' ? 'text-red-600' : 
                'text-gray-600'
              }`}>
                {change.value > 0 ? '+' : ''}{change.value}%
              </span>
            </div>
          )}
        </div>
        {icon && (
          <div className={`flex-shrink-0 ${colorClasses[color]}`}>
            {icon}
          </div>
        )}
      </div>
    </div>
  );
}