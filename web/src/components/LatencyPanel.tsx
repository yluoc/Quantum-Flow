import { useMemo } from 'react';
import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer } from 'recharts';
import type { LatencyData } from '../types/protocol';
import { formatUs } from '../lib/format';

interface Props {
  latency: LatencyData | null;
  history: LatencyData[];
}

export default function LatencyPanel({ latency, history }: Props) {
  const { avg, max, chartData } = useMemo(() => {
    if (history.length === 0)
      return { avg: 0, max: 0, chartData: [] };

    const totals = history.map(h => h.total_us);
    const sum = totals.reduce((a, b) => a + b, 0);
    const a = sum / totals.length;
    const m = Math.max(...totals);

    const cd = history.map((h, i) => ({
      idx: i,
      total: Number(h.total_us.toFixed(1)),
      match: Number(h.order_match_us.toFixed(1)),
      strategy: Number(h.strategy_eval_us.toFixed(1)),
      broadcast: Number(h.ws_broadcast_us.toFixed(1)),
    }));

    return { avg: a, max: m, chartData: cd };
  }, [history]);

  return (
    <div className="bg-gray-900 rounded-lg p-4 border border-gray-800 flex flex-col h-full overflow-hidden">
      <h2 className="text-lg font-semibold mb-2 text-gray-300">Latency Metrics</h2>

      {!latency ? (
        <p className="text-gray-500 text-sm">Waiting for data...</p>
      ) : (
        <>
          {/* Breakdown */}
          <div className="text-xs font-mono space-y-0.5 mb-3">
            <div className="flex justify-between text-gray-400">
              <span>Python -&gt; C++</span>
              <span>{formatUs(latency.python_to_cpp_us)}</span>
            </div>
            <div className="flex justify-between text-gray-400">
              <span>Order Matching</span>
              <span>{formatUs(latency.order_match_us)}</span>
            </div>
            <div className="flex justify-between text-gray-400">
              <span>Strategy Eval</span>
              <span>{formatUs(latency.strategy_eval_us)}</span>
            </div>
            <div className="flex justify-between text-gray-400">
              <span>WS Broadcast</span>
              <span>{formatUs(latency.ws_broadcast_us)}</span>
            </div>
            <div className="border-t border-gray-700 pt-1 flex justify-between text-white font-semibold">
              <span>Total</span>
              <span>{formatUs(latency.total_us)}</span>
            </div>
          </div>

          <div className="text-xs text-gray-500 mb-1">
            avg: {formatUs(avg)} | max: {formatUs(max)}
          </div>

          {/* Chart */}
          <div className="flex-1 min-h-[120px]">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={chartData}>
                <XAxis dataKey="idx" hide />
                <YAxis
                  width={40}
                  tick={{ fill: '#6b7280', fontSize: 10 }}
                  tickFormatter={(v: number) => v.toFixed(0)}
                />
                <Tooltip
                  contentStyle={{
                    backgroundColor: '#1f2937',
                    border: '1px solid #374151',
                    borderRadius: '6px',
                    fontSize: 11,
                  }}
                  labelFormatter={() => ''}
                  formatter={(value: number, name: string) => [formatUs(value), name]}
                />
                <Line type="monotone" dataKey="total" stroke="#f59e0b" dot={false} strokeWidth={1.5} />
                <Line type="monotone" dataKey="match" stroke="#3b82f6" dot={false} strokeWidth={1} />
                <Line type="monotone" dataKey="strategy" stroke="#8b5cf6" dot={false} strokeWidth={1} />
                <Line type="monotone" dataKey="broadcast" stroke="#10b981" dot={false} strokeWidth={1} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </>
      )}
    </div>
  );
}
