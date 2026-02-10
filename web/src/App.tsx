import { useEffect, useMemo, useState } from 'react';
import { useQuantumFlowWs } from './hooks/useQuantumFlowWs';
import ConnectionStatus from './components/ConnectionStatus';
import OrderBookPanel from './components/OrderBookPanel';
import TradeFlowPanel from './components/TradeFlowPanel';
import LatencyPanel from './components/LatencyPanel';
import StrategyPanel from './components/StrategyPanel';

const WS_URL = `ws://${window.location.hostname || 'localhost'}:9001`;

export default function App() {
  const state = useQuantumFlowWs(WS_URL);
  const { setSymbols } = state;
  const [selectedSymbol, setSelectedSymbol] = useState<string>('');

  useEffect(() => {
    if (state.symbols.length === 0) {
      if (selectedSymbol) setSelectedSymbol('');
      return;
    }
    if (!selectedSymbol || !state.symbols.includes(selectedSymbol)) {
      setSelectedSymbol(state.symbols[0]);
    }
  }, [state.symbols, selectedSymbol]);

  useEffect(() => {
    if (!state.connected || !selectedSymbol) return;
    setSymbols([selectedSymbol]);
  }, [state.connected, selectedSymbol, setSymbols]);

  const selectedBook = selectedSymbol ? state.booksBySymbol[selectedSymbol] ?? null : null;
  const selectedTrades = selectedSymbol ? state.tradesBySymbol[selectedSymbol] ?? [] : [];
  const selectedStrategies = useMemo(
    () => (selectedSymbol ? state.strategies.filter(s => s.symbol === selectedSymbol) : state.strategies),
    [state.strategies, selectedSymbol]
  );

  return (
    <div className="h-screen flex flex-col bg-gray-950 text-gray-100 p-3 gap-3">
      <div className="flex items-center justify-between px-2">
        <h1 className="text-xl font-bold tracking-tight text-gray-200">
          QuantumFlow
        </h1>
        <div className="flex items-center gap-3">
          <label className="text-xs text-gray-400 flex items-center gap-2">
            <span>Symbol</span>
            <select
              className="bg-gray-900 border border-gray-700 text-gray-200 text-xs rounded px-2 py-1"
              value={selectedSymbol}
              onChange={(e) => setSelectedSymbol(e.target.value)}
              disabled={state.symbols.length === 0}
            >
              {state.symbols.length === 0 ? (
                <option value="">No symbols</option>
              ) : (
                state.symbols.map((symbol) => (
                  <option key={symbol} value={symbol}>
                    {symbol}
                  </option>
                ))
              )}
            </select>
          </label>
          <ConnectionStatus connected={state.connected} />
        </div>
      </div>

      <div className="flex-1 grid grid-cols-2 grid-rows-2 gap-3 min-h-0">
        <OrderBookPanel book={selectedBook} />
        <TradeFlowPanel symbol={selectedSymbol} trades={selectedTrades} />
        <LatencyPanel latency={state.latency} history={state.latencyHistory} />
        <StrategyPanel strategies={selectedStrategies} />
      </div>
    </div>
  );
}
