import { useEffect, useRef, useCallback, useState } from 'react';
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
  book: BookData | null;
  trades: TradeData[];
  latency: LatencyData | null;
  latencyHistory: LatencyData[];
  strategies: StrategySignalData[];
}

const RECONNECT_DELAY_MS = 2000;
const MAX_LATENCY_HISTORY = 120;

export function useQuantumFlowWs(url: string): QuantumFlowState {
  const [connected, setConnected] = useState(false);
  const [book, setBook] = useState<BookData | null>(null);
  const [trades, setTrades] = useState<TradeData[]>([]);
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
          case 'book':
            setBook(msg.data as BookData);
            break;
          case 'trades':
            setTrades((msg.data as TradesPayload).trades);
            break;
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

  useEffect(() => {
    connect();
    return () => {
      clearTimeout(reconnectTimer.current);
      wsRef.current?.close();
    };
  }, [connect]);

  return { connected, book, trades, latency, latencyHistory, strategies };
}
