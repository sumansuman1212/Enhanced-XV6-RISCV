from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

# Resolve paths relative to this script's directory
SCRIPT_DIR = Path(__file__).resolve().parent
DATA_FILE = SCRIPT_DIR / 'data.txt'
OUTPUT_FILE = SCRIPT_DIR / 'mlfq_timeline_1.png'

# Read the data from the file
data = pd.read_csv(DATA_FILE, sep=r'\s+', header=None, names=['pid', 'ticks', 'queuepriority'])

# Ensure the data is sorted by ticks
data.sort_values(by='ticks', inplace=True)

# Create a line plot for the timeline graph
plt.figure(figsize=(12, 6))

# Plot each process with a different color and connect the points with lines
for pid in sorted(data['pid'].unique()):  # Sort process IDs
    process_data = data[data['pid'] == pid]
    # Plot line connecting all points of the same process, without markers
    plt.plot(process_data['ticks'], process_data['queuepriority'], label=f'P{pid}', linewidth=2)  # Increased line width for better visibility

# Set the labels and title
plt.title('MLFQ Scheduler: Process Queue Over Time', fontsize=16)
plt.xlabel('Number of Ticks', fontsize=14)
plt.ylabel('Queue ID', fontsize=14)

# Set the y-axis ticks to have increments of 0.5 from 0 to 3 (or more if necessary)
plt.yticks(np.arange(0, 3.5, 0.5))

# Beautify the graph
plt.xticks(fontsize=12)
plt.yticks(fontsize=12)
plt.grid(True, linestyle='--', alpha=0.7)
plt.legend(title='Process ID', fontsize=12, title_fontsize='13', loc='best')

# Adjust layout and save the plot
plt.tight_layout()
plt.savefig(OUTPUT_FILE, dpi=300)
plt.show()
