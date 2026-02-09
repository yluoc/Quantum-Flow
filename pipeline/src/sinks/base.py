"""Base sink protocol/ABC."""

from __future__ import annotations

from abc import ABC, abstractmethod

from src.normalizer import NormalizedEvent


class Sink(ABC):
    """Base interface for event sinks."""
    
    @abstractmethod
    async def write(self, event: NormalizedEvent) -> None:
        """Write a normalized event."""
        pass
    
    @abstractmethod
    async def close(self) -> None:
        """Close the sink and flush any pending writes."""
        pass
