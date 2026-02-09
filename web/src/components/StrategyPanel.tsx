import type { StrategySignalData } from '../types/protocol';

interface Props {
  strategies: StrategySignalData[];
}

function signalColor(signal: string): string {
  switch (signal) {
    case 'BUY':
    case 'LONG_SPOT_SHORT_PERP':
    case 'LONG_PAIR':
      return 'text-green-400';
    case 'SELL':
    case 'SHORT_SPOT_LONG_PERP':
    case 'SHORT_PAIR':
      return 'text-red-400';
    case 'NEUTRAL':
    default:
      return 'text-gray-500';
  }
}

function signalBg(signal: string): string {
  switch (signal) {
    case 'BUY':
    case 'LONG_SPOT_SHORT_PERP':
    case 'LONG_PAIR':
      return 'bg-green-500/10';
    case 'SELL':
    case 'SHORT_SPOT_LONG_PERP':
    case 'SHORT_PAIR':
      return 'bg-red-500/10';
    default:
      return '';
  }
}

export default function StrategyPanel({ strategies }: Props) {
  return (
    <div className="bg-gray-900 rounded-lg p-4 border border-gray-800 flex flex-col h-full overflow-hidden">
      <div className="flex justify-between items-center mb-2">
        <h2 className="text-lg font-semibold text-gray-300">Strategy Signals</h2>
        <span className="text-xs text-gray-500">{strategies.length} active</span>
      </div>

      <div className="flex-1 overflow-auto text-xs">
        <table className="w-full">
          <thead className="sticky top-0 bg-gray-900">
            <tr className="text-gray-500 border-b border-gray-800">
              <th className="text-left py-1 px-1">Strategy</th>
              <th className="text-left py-1 px-1">Symbol</th>
              <th className="text-center py-1 px-1">Signal</th>
              <th className="text-right py-1 px-1">Confidence</th>
            </tr>
          </thead>
          <tbody>
            {strategies.map((s, i) => (
              <tr key={i} className={`border-b border-gray-800/50 hover:bg-gray-800/30 ${signalBg(s.signal)}`}>
                <td className="py-1 px-1 text-gray-300">{s.strategy_name}</td>
                <td className="py-1 px-1 text-gray-400">{s.symbol}</td>
                <td className={`text-center py-1 px-1 font-semibold ${signalColor(s.signal)}`}>
                  {s.signal}
                </td>
                <td className="text-right py-1 px-1 text-gray-400">
                  {(s.confidence * 100).toFixed(0)}%
                </td>
              </tr>
            ))}
            {strategies.length === 0 && (
              <tr><td colSpan={4} className="text-center py-4 text-gray-600">No signals yet</td></tr>
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
}
