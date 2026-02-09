import { useMemo } from 'react';
import type { BookData } from '../types/protocol';
import { formatPrice, formatQty } from '../lib/format';

interface Props {
  book: BookData | null;
}

export default function OrderBookPanel({ book }: Props) {
  const { bidCum, askCum, maxCum } = useMemo(() => {
    if (!book) return { bidCum: [], askCum: [], maxCum: 1 };

    let cum = 0;
    const bc = book.bids.map(l => { cum += l.quantity; return cum; });

    cum = 0;
    const ac = book.asks.map(l => { cum += l.quantity; return cum; });

    const mx = Math.max(bc[bc.length - 1] ?? 1, ac[ac.length - 1] ?? 1);
    return { bidCum: bc, askCum: ac, maxCum: mx };
  }, [book]);

  if (!book) {
    return (
      <div className="bg-gray-900 rounded-lg p-4 border border-gray-800">
        <h2 className="text-lg font-semibold mb-2 text-gray-300">Order Book</h2>
        <p className="text-gray-500 text-sm">Waiting for data...</p>
      </div>
    );
  }

  const spread = book.best_ask - book.best_bid;

  return (
    <div className="bg-gray-900 rounded-lg p-4 border border-gray-800 flex flex-col h-full overflow-hidden">
      <div className="flex justify-between items-center mb-2">
        <h2 className="text-lg font-semibold text-gray-300">Order Book</h2>
        <span className="text-xs text-gray-500">{book.symbol}</span>
      </div>

      <div className="text-xs mb-3 flex gap-4 text-gray-400">
        <span>Bid: <span className="text-green-400">{formatPrice(book.best_bid)}</span></span>
        <span>Ask: <span className="text-red-400">{formatPrice(book.best_ask)}</span></span>
        <span>Spread: <span className="text-yellow-400">{formatPrice(spread)}</span></span>
        <span>Mid: {formatPrice(book.mid_price)}</span>
      </div>

      {/* Depth Chart */}
      <div className="flex gap-1 mb-3 h-16">
        <div className="flex-1 flex items-end justify-end gap-px">
          {bidCum.map((c, i) => (
            <div
              key={i}
              className="bg-green-600/60 min-w-[2px] flex-1 rounded-t-sm"
              style={{ height: `${(c / maxCum) * 100}%` }}
            />
          ))}
        </div>
        <div className="flex-1 flex items-end gap-px">
          {askCum.map((c, i) => (
            <div
              key={i}
              className="bg-red-600/60 min-w-[2px] flex-1 rounded-t-sm"
              style={{ height: `${(c / maxCum) * 100}%` }}
            />
          ))}
        </div>
      </div>

      {/* Price Ladder */}
      <div className="flex-1 overflow-auto text-xs">
        <table className="w-full">
          <thead className="sticky top-0 bg-gray-900">
            <tr className="text-gray-500 border-b border-gray-800">
              <th className="text-right py-1 px-1">Bid Qty</th>
              <th className="text-right py-1 px-1">#</th>
              <th className="text-center py-1 px-1">Price</th>
              <th className="text-left py-1 px-1">#</th>
              <th className="text-left py-1 px-1">Ask Qty</th>
            </tr>
          </thead>
          <tbody>
            {[...book.asks].reverse().map((lvl, i) => (
              <tr key={`a${i}`} className="border-b border-gray-800/50 hover:bg-gray-800/30">
                <td className="text-right py-0.5 px-1"></td>
                <td className="text-right py-0.5 px-1"></td>
                <td className="text-center py-0.5 px-1 text-red-400">{formatPrice(lvl.price)}</td>
                <td className="text-left py-0.5 px-1 text-gray-500">{lvl.order_count}</td>
                <td className="text-left py-0.5 px-1 text-red-400">{formatQty(lvl.quantity)}</td>
              </tr>
            ))}
            {book.bids.map((lvl, i) => (
              <tr key={`b${i}`} className="border-b border-gray-800/50 hover:bg-gray-800/30">
                <td className="text-right py-0.5 px-1 text-green-400">{formatQty(lvl.quantity)}</td>
                <td className="text-right py-0.5 px-1 text-gray-500">{lvl.order_count}</td>
                <td className="text-center py-0.5 px-1 text-green-400">{formatPrice(lvl.price)}</td>
                <td className="text-left py-0.5 px-1"></td>
                <td className="text-left py-0.5 px-1"></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
