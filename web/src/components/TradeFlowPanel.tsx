import { useMemo } from 'react';
import type { TradeData } from '../types/protocol';
import { formatPrice, formatQty } from '../lib/format';

interface Props {
  symbol: string;
  trades: TradeData[];
}

const BUCKET_COUNT = 30;

export default function TradeFlowPanel({ symbol, trades }: Props) {
  const recent = useMemo(() => {
    return [...trades].reverse().slice(0, 50);
  }, [trades]);

  const { buyVol, sellVol, maxVol } = useMemo(() => {
    const bv = new Array<number>(BUCKET_COUNT).fill(0);
    const sv = new Array<number>(BUCKET_COUNT).fill(0);

    if (trades.length === 0) return { buyVol: bv, sellVol: sv, maxVol: 1 };

    const perBucket = Math.max(1, Math.floor(trades.length / BUCKET_COUNT));
    for (let i = 0; i < trades.length; i++) {
      const bucket = Math.min(Math.floor(i / perBucket), BUCKET_COUNT - 1);
      if (trades[i].side === 0) bv[bucket] += trades[i].quantity;
      else sv[bucket] += trades[i].quantity;
    }

    const mx = Math.max(...bv, ...sv, 1);
    return { buyVol: bv, sellVol: sv, maxVol: mx };
  }, [trades]);

  return (
    <div className="bg-gray-900 rounded-lg p-4 border border-gray-800 flex flex-col h-full overflow-hidden">
      <div className="flex justify-between items-center mb-2">
        <h2 className="text-lg font-semibold text-gray-300">Trade Flow</h2>
        <span className="text-xs text-gray-500">{symbol || 'N/A'}</span>
      </div>

      {/* Volume Histogram */}
      <div className="mb-3">
        <div className="text-xs text-gray-500 mb-1">Buy / Sell Volume</div>
        <div className="flex gap-px items-end h-12">
          {buyVol.map((v, i) => (
            <div key={`b${i}`} className="flex-1 flex flex-col justify-end">
              <div
                className="bg-green-500/70 rounded-t-sm"
                style={{ height: `${(v / maxVol) * 100}%`, minHeight: v > 0 ? '1px' : '0px' }}
              />
            </div>
          ))}
        </div>
        <div className="flex gap-px items-start h-12">
          {sellVol.map((v, i) => (
            <div key={`s${i}`} className="flex-1">
              <div
                className="bg-red-500/70 rounded-b-sm"
                style={{ height: `${(v / maxVol) * 100}%`, minHeight: v > 0 ? '1px' : '0px' }}
              />
            </div>
          ))}
        </div>
      </div>

      {/* Trades Table */}
      <div className="text-xs text-gray-500 mb-1">Recent Trades ({trades.length})</div>
      <div className="flex-1 overflow-auto text-xs">
        <table className="w-full">
          <thead className="sticky top-0 bg-gray-900">
            <tr className="text-gray-500 border-b border-gray-800">
              <th className="text-left py-1 px-1">Price</th>
              <th className="text-right py-1 px-1">Qty</th>
              <th className="text-center py-1 px-1">Side</th>
            </tr>
          </thead>
          <tbody>
            {recent.map((t, i) => (
              <tr key={i} className="border-b border-gray-800/50 hover:bg-gray-800/30">
                <td className="py-0.5 px-1">{formatPrice(t.price)}</td>
                <td className="text-right py-0.5 px-1">{formatQty(t.quantity)}</td>
                <td className={`text-center py-0.5 px-1 ${
                  t.side === 0 ? 'text-green-400' : 'text-red-400'
                }`}>
                  {t.side === 0 ? 'BUY' : 'SELL'}
                </td>
              </tr>
            ))}
            {recent.length === 0 && (
              <tr><td colSpan={3} className="text-center py-4 text-gray-600">No trades yet</td></tr>
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
}
