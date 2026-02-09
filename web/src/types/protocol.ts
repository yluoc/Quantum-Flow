export interface PriceLevel {
  price: number;
  quantity: number;
  order_count: number;
}

export interface BookData {
  symbol: string;
  best_bid: number;
  best_ask: number;
  mid_price: number;
  bids: PriceLevel[];
  asks: PriceLevel[];
}

export interface TradeData {
  price: number;
  quantity: number;
  side: number; // 0=buy, 1=sell
  timestamp_ns: number;
}

export interface TradesPayload {
  trades: TradeData[];
}

export interface LatencyData {
  python_to_cpp_us: number;
  order_match_us: number;
  strategy_eval_us: number;
  ws_broadcast_us: number;
  total_us: number;
}

export interface StrategySignalData {
  strategy_name: string;
  symbol: string;
  signal: string;
  confidence: number;
  timestamp_ns: number;
}

export interface StrategiesPayload {
  signals: StrategySignalData[];
}

export type MessageType = 'book' | 'trades' | 'latency' | 'strategies';

export interface WsMessage<T = unknown> {
  type: MessageType;
  timestamp_ns: number;
  data: T;
}
