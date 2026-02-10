import { useEffect, useRef, useCallback, useMemo, useState } from 'react';
import type {
  BookData,
  TradeData,
  LatencyData,
  StrategySignalData,
  WsMessage,
  TradesPayload,
  StrategiesPayload,
} from '../types/protocol';

export interface QuantumFlowState {
  connected: boolean;
  symbols: string[];
  booksBySymbol: Record<string, BookData>;
  tradesBySymbol: Record<string, TradeData[]>;
  latency: LatencyData | null;
  latencyHistory: LatencyData[];
  strategies: StrategySignalData[];
}

const RECONNECT_DELAY_MS = 2000;
const MAX_LATENCY_HISTORY = 120;

export function useQuantumFlowWs(url: string): QuantumFlowState {
  const [connected, setConnected] = useState(false);
  const [booksBySymbol, setBooksBySymbol] = useState<Record<string, BookData>>({});
  const [tradesBySymbol, setTradesBySymbol] = useState<Record<string, TradeData[]>>({});
  const [latency, setLatency] = useState<LatencyData | null>(null);
  const [latencyHistory, setLatencyHistory] = useState<LatencyData[]>([]);
  const [strategies, setStrategies] = useState<StrategySignalData[]>([]);

  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout>>();

  const connect = useCallback(() => {
    if (wsRef.current?.readyState === WebSocket.OPEN) return;

    const ws = new WebSocket(url);
    wsRef.current = ws;

    ws.onopen = () => setConnected(true);

    ws.onclose = () => {
      setConnected(false);
      reconnectTimer.current = setTimeout(connect, RECONNECT_DELAY_MS);
    };

    ws.onerror = () => ws.close();

    ws.onmessage = (event: MessageEvent) => {
      try {
        const msg: WsMessage = JSON.parse(event.data as string);

        switch (msg.type) {
          case 'book': {
            const book = msg.data as BookData;
            setBooksBySymbol(prev => ({ ...prev, [book.symbol]: book }));
            break;
          }
          case 'trades': {
            const payload = msg.data as TradesPayload;
            const symbol = payload.symbol;
            const trades = payload.trades.map(t => ({ ...t, symbol: t.symbol ?? symbol }));
            setTradesBySymbol(prev => ({ ...prev, [symbol]: trades }));
            break;
          }
          case 'latency': {
            const lat = msg.data as LatencyData;
            setLatency(lat);
            setLatencyHistory(prev => {
              const next = [...prev, lat];
              return next.length > MAX_LATENCY_HISTORY
                ? next.slice(next.length - MAX_LATENCY_HISTORY)
                : next;
            });
            break;
          }
          case 'strategies':
            setStrategies((msg.data as StrategiesPayload).signals);
            break;
        }
      } catch {}
    };
  }, [url]);

  const symbols = useMemo(() => {
    const set = new Set<string>();
    Object.keys(booksBySymbol).forEach(s => set.add(s));
    Object.keys(tradesBySymbol).forEach(s => set.add(s));
    strategies.forEach(s => {
      if (s.symbol) set.add(s.symbol);
    });
    return Array.from(set).sort();
  }, [booksBySymbol, tradesBySymbol, strategies]);

  useEffect(() => {
    connect();
    return () => {
      clearTimeout(reconnectTimer.current);
      wsRef.current?.close();
    };
  }, [connect]);

  return { connected, symbols, booksBySymbol, tradesBySymbol, latency, latencyHistory, strategies };
}
