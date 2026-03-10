import re
import matplotlib.pyplot as plt
import os

def parse_log_phase(filepath):
    """
    Parses the log file, discarding everything before "Metrics reset" 
    to focus only on Phase 2 (Trading Phase).
    Returns a list of dicts: {'orders', 'tps', 'avg_lat', 'max_lat'}
    """
    if not os.path.exists(filepath):
        print(f"Warning: File {filepath} not found.")
        return []

    data = []
    phase2_started = False
    
    with open(filepath, 'r') as f:
        for line in f:
            # Detect Phase 2 start
            if "Metrics reset" in line:
                phase2_started = True
                continue
            
            # If we haven't seen the reset marker, ignore (unless it's baseline which might not have it?)
            # Actually, baseline ALSO has it now. So we strictly enforce it.
            if not phase2_started:
                continue

            # Parse performance lines
            # [Perf] Orders: 100000 | TPS: 587758 | Avg Latency: 0.10778 us | Max Latency: 207 us
            match = re.search(r'Orders: (\d+) \| TPS: (\d+) \| Avg Latency: ([\d\.]+) us \| Max Latency: (\d+) us', line)
            if match:
                data.append({
                    'orders': int(match.group(1)),
                    'tps': int(match.group(2)),
                    'avg_lat': float(match.group(3)),
                    'max_lat': int(match.group(4))
                })
    return data

def plot_tps(baseline, experiment, control, filename='tps_comparison.png'):
    plt.figure(figsize=(12, 6))
    
    if baseline:
        x = [d['orders'] for d in baseline]
        y = [d['tps'] for d in baseline]
        plt.plot(x, y, label='Baseline (No Snapshot)', marker='', linestyle='--', alpha=0.7)

    if experiment:
        x = [d['orders'] for d in experiment]
        y = [d['tps'] for d in experiment]
        plt.plot(x, y, label='Internal Fork (Snapshot)', marker='', linestyle='-', linewidth=2)

    if control:
        x = [d['orders'] for d in control]
        y = [d['tps'] for d in control]
        plt.plot(x, y, label='External Gcore (Snapshot)', marker='o', linestyle='-', linewidth=2, color='red')

    plt.title('Throughput Comparison (Trading Phase)')
    plt.xlabel('Processed Orders (Phase 2)')
    plt.ylabel('TPS (Orders/sec)')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    plt.savefig(filename)
    print(f"Saved {filename}")

def plot_latency_comparison(baseline, experiment, control, metric='max_lat', title='Max Latency', filename='latency_max.png'):
    """
    Plots a bar chart comparing the MAXIMUM latency observed in the entire Phase 2.
    """
    plt.figure(figsize=(10, 6))
    
    labels = []
    values = []
    colors = []

    if baseline:
        val = max(d[metric] for d in baseline)
        labels.append('Baseline')
        values.append(val)
        colors.append('gray')

    if experiment:
        val = max(d[metric] for d in experiment)
        labels.append('Internal Fork')
        values.append(val)
        colors.append('blue')

    if control:
        val = max(d[metric] for d in control)
        # For Gcore, the max latency might be huge, so we might need log scale
        labels.append('External Gcore')
        values.append(val)
        colors.append('red')

    bars = plt.bar(labels, values, color=colors)
    
    # Add value labels on top of bars
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height,
                f'{int(height)} us',
                ha='center', va='bottom')

    plt.title(f'{title} Comparison (Phase 2)')
    plt.ylabel('Latency (microseconds)')
    plt.grid(True, axis='y', linestyle='--', alpha=0.5)
    
    # Use log scale if values differ by orders of magnitude (likely for Gcore)
    if values and max(values) > 10 * min(values):
        plt.yscale('log')
        plt.ylabel('Latency (us) - Log Scale')

    plt.tight_layout()
    plt.savefig(filename)
    print(f"Saved {filename}")

if __name__ == "__main__":
    # Ensure test directory exists for output
    if not os.path.exists("test"):
        os.makedirs("test", exist_ok=True)
        
    base_data = parse_log_phase("test/baseline.log")
    exp_data = parse_log_phase("test/experiment.log")
    ctrl_data = parse_log_phase("test/control.log")

    # 1. TPS Line Chart
    plot_tps(base_data, exp_data, ctrl_data, "test/tps_comparison.png")

    # 2. Max Latency Bar Chart
    plot_latency_comparison(base_data, exp_data, ctrl_data, 'max_lat', 'Peak Latency (Worst Case)', "test/latency_max.png")

    # 3. Avg Latency Bar Chart (Average of averages)
    # Re-calculate overall average from the logs:
    # Since the C++ engine logs the *cumulative* average from Phase 2 start, 
    # the truly accurate Phase 2 average is simply the value from the *last* record.
    def get_avg(data):
        if not data: return 0
        return data[-1]['avg_lat']

    # Hack to reuse the plotting function by creating fake data objects with 'avg' key
    # effectively passing the computed average as a single value to be plotted
    # Actually, let's just create a custom simple plotter for average
    import numpy as np
    
    labels = ['Baseline', 'Internal Fork', 'External Gcore']
    avgs = [get_avg(base_data), get_avg(exp_data), get_avg(ctrl_data)]
    
    plt.figure(figsize=(10, 6))
    bars = plt.bar(labels, avgs, color=['gray', 'blue', 'red'])
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height,
                f'{height:.2f} us',
                ha='center', va='bottom')
    plt.title('Average Latency Comparison (Phase 2)')
    plt.ylabel('Latency (microseconds)')
    plt.grid(True, axis='y')
    plt.savefig('test/latency_avg.png')
    print("Saved test/latency_avg.png")
