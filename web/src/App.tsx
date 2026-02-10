import { useQuantumFlowWs } from './hooks/useQuantumFlowWs';
import ConnectionStatus from './components/ConnectionStatus';
import OrderBookPanel from './components/OrderBookPanel';
import TradeFlowPanel from './components/TradeFlowPanel';
import LatencyPanel from './components/LatencyPanel';
import StrategyPanel from './components/StrategyPanel';

const WS_URL = `ws://${window.location.hostname || 'localhost'}:9001`;

export default function App() {
  const state = useQuantumFlowWs(WS_URL);

  return (
    <div className="h-screen flex flex-col bg-gray-950 text-gray-100 p-3 gap-3">
      <div className="flex items-center justify-between px-2">
        <h1 className="text-xl font-bold tracking-tight text-gray-200">
          QuantumFlow
        </h1>
        <ConnectionStatus connected={state.connected} />
      </div>

      <div className="flex-1 grid grid-cols-2 grid-rows-2 gap-3 min-h-0">
        <OrderBookPanel book={state.book} />
        <TradeFlowPanel trades={state.trades} />
        <LatencyPanel latency={state.latency} history={state.latencyHistory} />
        <StrategyPanel strategies={state.strategies} />
      </div>
    </div>
  );
}
