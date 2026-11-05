# MLFQ Scheduler Trace & Visualization

This directory contains empirical execution traces and visualization tools for the Multi-Level Feedback Queue (MLFQ) scheduler implemented in Enhanced-XV6.

## Files

| File | Description |
|---|---|
| `data.txt` | Primary trace log recording process queue occupancy over time. Format: `<pid> <ticks> <queuepriority>`. |
| `data2.txt` | Secondary trace log captured under an alternate workload or run. |
| `plot_mlfq.py` | Python visualization script that parses the trace log and plots process priority transitions over ticks. |
| `mlfq_timeline_1.png` | Process queue timeline graph generated from `data.txt`. |
| `mlfq_timeline_2.png` | Process queue timeline graph generated from `data2.txt`. |

## What the Data and Graphs Show

In an MLFQ scheduler:
- New processes enter the highest priority queue (Queue 0).
- CPU-bound processes consume their allotted time quantum and are progressively demoted to lower-priority queues (1, 2, 3).
- I/O-bound processes yield voluntarily before exhausting their quantum and retain higher priority.
- Periodic priority boost resets all processes back to the top queue to prevent starvation.

The timeline plots visualize these dynamics:
- **X-axis (`Number of Ticks`)**: Elapsed kernel clock ticks.
- **Y-axis (`Queue ID`)**: Active queue level for each process (0 = highest priority, 3 = lowest priority).
- **Line colors**: Individual process IDs ($P4, P5, \dots$). Demotions appear as steps downward (to higher queue indices), while priority boosts appear as sharp upward jumps.

## How to Run the Plotting Script

### Prerequisites

Ensure Python 3 and the required dependencies are installed:

```bash
pip install pandas matplotlib numpy
```

### Running the Script

Run the script from anywhere (it automatically resolves file paths relative to its own directory):

```bash
python docs/mlfq/plot_mlfq.py
```

The script will render the graph in an interactive window and save high-resolution output as `mlfq_timeline_1.png` in this directory.

