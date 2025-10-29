"""
Output Formatter

Formats statistical results for different output types (console, JSON).
"""

import json
from typing import Dict


class OutputFormatter:
    """Formats analysis results for display or export."""

    def format_console(self, stats: Dict) -> str:
        """
        Format statistics for console output.

        Args:
            stats: Dictionary of statistics from DataAnalyzer

        Returns:
            Formatted string for console display
        """
        if not stats:
            return "No statistics to display"

        lines = []
        lines.append("=" * 60)
        lines.append("CSV Data Statistics")
        lines.append("=" * 60)

        for column, column_stats in stats.items():
            lines.append(f"\nColumn: {column}")
            lines.append("-" * 40)
            lines.append(f"  Count:      {column_stats['count']}")
            lines.append(f"  Mean:       {column_stats['mean']:.2f}")
            lines.append(f"  Median:     {column_stats['median']:.2f}")
            lines.append(f"  Min:        {column_stats['min']:.2f}")
            lines.append(f"  Max:        {column_stats['max']:.2f}")
            lines.append(f"  Std Dev:    {column_stats['stdev']:.2f}")

        lines.append("=" * 60)
        return "\n".join(lines)

    def format_json(self, stats: Dict) -> str:
        """
        Format statistics as JSON.

        Args:
            stats: Dictionary of statistics from DataAnalyzer

        Returns:
            JSON string representation of statistics
        """
        return json.dumps(stats, indent=2)
