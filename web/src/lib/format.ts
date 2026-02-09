export function formatPrice(price: number): string {
  if (price >= 1000) return price.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  if (price >= 1) return price.toFixed(2);
  return price.toFixed(6);
}

export function formatQty(qty: number): string {
  if (qty >= 1_000_000) return (qty / 1_000_000).toFixed(1) + 'M';
  if (qty >= 1_000) return (qty / 1_000).toFixed(1) + 'K';
  return qty.toString();
}

export function formatUs(us: number): string {
  if (us >= 1000) return (us / 1000).toFixed(1) + ' ms';
  return us.toFixed(1) + ' us';
}

export function formatNsTimestamp(ns: number): string {
  const ms = ns / 1_000_000;
  const date = new Date(ms);
  if (isNaN(date.getTime()) || ms < 1e12) {
    // Likely a steady_clock timestamp, not wall clock - show raw
    return formatQty(ns);
  }
  return date.toLocaleTimeString('en-US', { hour12: false, fractionalSecondDigits: 3 } as Intl.DateTimeFormatOptions);
}
