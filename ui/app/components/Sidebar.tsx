'use client';

import React from 'react';
import Link from 'next/link';
import { usePathname } from 'next/navigation';
import { 
  BarChart3,
  Database,
  Radio,
  Play,
  Activity,
  Settings,
  Home
} from 'lucide-react';

const navigation = [
  { name: 'Overview', href: '/', icon: Home },
  { name: 'Symbols', href: '/symbols', icon: Database },
  { name: 'Feeds', href: '/feeds', icon: Radio },
  { name: 'Replay', href: '/replay', icon: Play },
  { name: 'Metrics', href: '/metrics', icon: BarChart3 },
];

export default function Sidebar() {
  const pathname = usePathname();

  return (
    <div className="flex flex-col w-64 bg-white shadow-sm border-r border-gray-200">
      <div className="flex items-center h-16 px-6 border-b border-gray-200">
        <Activity className="h-8 w-8 text-primary-600" />
        <span className="ml-3 text-xl font-bold text-gray-900">
          MarketData
        </span>
      </div>
      
      <nav className="flex-1 px-4 py-6 space-y-1">
        {navigation.map((item) => {
          const isActive = pathname === item.href;
          return (
            <Link
              key={item.name}
              href={item.href}
              className={`
                group flex items-center px-2 py-2 text-sm font-medium rounded-md transition-colors
                ${
                  isActive
                    ? 'bg-primary-50 border-r-2 border-primary-600 text-primary-700'
                    : 'text-gray-600 hover:bg-gray-50 hover:text-gray-900'
                }
              `}
            >
              <item.icon
                className={`
                  mr-3 h-5 w-5 flex-shrink-0
                  ${
                    isActive
                      ? 'text-primary-600'
                      : 'text-gray-400 group-hover:text-gray-500'
                  }
                `}
              />
              {item.name}
            </Link>
          );
        })}
      </nav>
      
      <div className="border-t border-gray-200 p-4">
        <div className="flex items-center">
          <div className="h-2 w-2 bg-green-400 rounded-full animate-pulse-slow"></div>
          <span className="ml-2 text-xs text-gray-500">
            System Online
          </span>
        </div>
      </div>
    </div>
  );
}