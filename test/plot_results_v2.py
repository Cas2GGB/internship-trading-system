import re
import matplotlib.pyplot as plt
import numpy as np
import os

def parse_log_phase(filepath):
    """
    解析日志文件，丢弃 "Metrics reset" 之前的内容，只关注第二阶段（交易阶段）。
    返回列表，每条记录为 dict：{'orders', 'tps', 'avg_lat', 'p50', 'p999', 'max_lat'}
    兼容旧格式（无 P50/P999 字段）。
    """
    if not os.path.exists(filepath):
        print(f"警告：文件 {filepath} 不存在，跳过。")
        return []

    data = []
    phase2_started = False

    # 新格式：含 P50、P999、WinMax、超阈值比例
    pattern_new = re.compile(
        r'Orders: (\d+)'
        r' \| TPS: (\d+)'
        r' \| Avg Latency: ([\d\.]+) us'
        r' \| P50: (\d+) us'
        r' \| P999: (\d+) us'
        r' \| WinMax: (\d+) us'
        r' \| >100us: ([\d\.]+)%'
        r' \| >1ms: ([\d\.]+)%'
        r' \| Max Latency: (\d+) us'
    )
    # 兼容无 WinMax 的旧新格式
    pattern_mid = re.compile(
        r'Orders: (\d+)'
        r' \| TPS: (\d+)'
        r' \| Avg Latency: ([\d\.]+) us'
        r' \| P50: (\d+) us'
        r' \| P999: (\d+) us'
        r' \| >100us: ([\d\.]+)%'
        r' \| >1ms: ([\d\.]+)%'
        r' \| Max Latency: (\d+) us'
    )
    # 旧格式：仅含 Avg 和 Max
    pattern_old = re.compile(
        r'Orders: (\d+)'
        r' \| TPS: (\d+)'
        r' \| Avg Latency: ([\d\.]+) us'
        r' \| Max Latency: (\d+) us'
    )

    with open(filepath, 'r') as f:
        for line in f:
            if "Metrics reset" in line:
                phase2_started = True
                continue
            if not phase2_started:
                continue

            m = pattern_new.search(line)
            if m:
                data.append({
                    'orders':    int(m.group(1)),
                    'tps':       int(m.group(2)),
                    'avg_lat':   float(m.group(3)),
                    'p50':       int(m.group(4)),
                    'p999':      int(m.group(5)),
                    'winmax':    int(m.group(6)),
                    'over100us': float(m.group(7)),
                    'over1ms':   float(m.group(8)),
                    'max_lat':   int(m.group(9)),
                })
                continue

            m = pattern_mid.search(line)
            if m:
                data.append({
                    'orders':    int(m.group(1)),
                    'tps':       int(m.group(2)),
                    'avg_lat':   float(m.group(3)),
                    'p50':       int(m.group(4)),
                    'p999':      int(m.group(5)),
                    'winmax':    None,
                    'over100us': float(m.group(6)),
                    'over1ms':   float(m.group(7)),
                    'max_lat':   int(m.group(8)),
                })
                continue

            m = pattern_old.search(line)
            if m:
                data.append({
                    'orders':      int(m.group(1)),
                    'tps':         int(m.group(2)),
                    'avg_lat':     float(m.group(3)),
                    'p50':         None,
                    'p999':        None,
                    'winmax':      None,
                    'over100us':   None,
                    'over1ms':     None,
                    'max_lat':     int(m.group(4)),
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

def plot_percentile_comparison(baseline, experiment, control, filename='test/latency_percentile.png'):
    """
    Plot P50 and P999 latency over time as dual-panel line charts.
    Top panel: P50; Bottom panel: P999.
    """
    datasets = [
        ('Baseline (No Snapshot)', baseline,   '#2ecc71', '--'),
        ('Internal Fork Snapshot', experiment, '#3498db', '-'),
        ('External Gcore Snapshot', control,   '#e74c3c', '-'),
    ]

    valid = []
    for label, data, color, ls in datasets:
        if not data:
            print(f"  [{label}] Empty data, skipping.")
            continue
        if data[-1]['p50'] is None or data[-1]['p999'] is None:
            print(f"  [{label}] No P50/P999 data (old log format), skipping.")
            continue
        xs      = [d['orders'] for d in data]
        p50s    = [d['p50']    for d in data]
        p999s   = [d['p999']   for d in data]
        winmaxs = [d['winmax'] if d['winmax'] is not None else d['p999'] for d in data]
        print(f"  [{label}] points={len(xs)}, last_orders={xs[-1]}, "
              f"max_P50={max(p50s)}us, max_P999={max(p999s)}us, "
              f"max_WinMax={max(winmaxs)}us")
        valid.append((label, xs, p50s, p999s, winmaxs, color, ls))

    if not valid:
        print("No P50/P999 data found in any log. Skipping percentile chart.")
        return

    fig, (ax_top, ax_bot) = plt.subplots(2, 1, figsize=(13, 9),
                                          sharex=True,
                                          gridspec_kw={'hspace': 0.35})

    for label, xs, p50s, p999s, winmaxs, color, ls in valid:
        ax_top.plot(xs, p50s,    label=label,              color=color, linestyle=ls,   linewidth=1.8, alpha=0.85)
        ax_bot.plot(xs, p999s,   label=f'{label} P999',    color=color, linestyle=ls,   linewidth=1.8, alpha=0.85)
        ax_bot.plot(xs, winmaxs, label=f'{label} WinMax',  color=color, linestyle=':',  linewidth=1.2, alpha=0.55)

    # ---- top panel: P50 ----
    ax_top.set_title('P50 (Median) Latency', fontsize=12, fontweight='bold')
    ax_top.set_ylabel('Latency (us)')
    ax_top.legend(fontsize=10, loc='upper left')
    ax_top.grid(True, linestyle='--', alpha=0.5)

    # ---- bottom panel: P999 + WinMax ----
    ax_bot.set_title('P999 (99.9th pct) + WinMax  [window = 10k orders]\n'
                     'WinMax = per-window max, captures gcore pause spike',
                     fontsize=11, fontweight='bold')
    ax_bot.set_ylabel('Latency (us)')
    ax_bot.set_xlabel('Processed Orders (Phase 2)')
    ax_bot.legend(fontsize=8, loc='upper left', ncol=2)
    ax_bot.grid(True, linestyle='--', alpha=0.5)
    all_vals = [v for _, _, _, p999s, winmaxs, _, _ in valid for v in p999s + winmaxs]
    if all_vals and max(all_vals) > 10 * max(1, min(all_vals)):
        ax_bot.set_yscale('log')
        ax_bot.set_ylabel('Latency (us) - Log Scale')

    fig.suptitle('Latency Percentile Comparison (Trading Phase)\n'
                 'P50 = median  |  P999 = 99.9th percentile  |  WinMax = window peak',
                 fontsize=13, fontweight='bold', y=0.995)
    fig.subplots_adjust(top=0.92)
    fig.savefig(filename, bbox_inches='tight')
    print(f"Saved {filename}")


if __name__ == "__main__":
    os.makedirs("test", exist_ok=True)

    base_data = parse_log_phase("test/baseline.log")
    exp_data  = parse_log_phase("test/experiment.log")
    ctrl_data = parse_log_phase("test/control.log")

    # 1. TPS 折线图
    plot_tps(base_data, exp_data, ctrl_data, "test/tps_comparison.png")

    # 2. 峰值延迟柱状图（Max Latency）
    plot_latency_comparison(base_data, exp_data, ctrl_data,
                            'max_lat', 'Peak Latency (Worst Case)',
                            "test/latency_max.png")

    # 3. 平均延迟柱状图（Avg Latency，取最后一条累计均值）
    def get_avg(data):
        return data[-1]['avg_lat'] if data else 0

    labels = ['Baseline', 'Internal Fork', 'External Gcore']
    avgs   = [get_avg(base_data), get_avg(exp_data), get_avg(ctrl_data)]

    fig, ax = plt.subplots(figsize=(10, 6))
    bars = ax.bar(labels, avgs, color=['gray', 'blue', 'red'])
    for bar in bars:
        h = bar.get_height()
        ax.text(bar.get_x() + bar.get_width() / 2., h,
                f'{h:.2f} us', ha='center', va='bottom')
    ax.set_title('Average Latency Comparison (Phase 2)')
    ax.set_ylabel('Latency (microseconds)')
    ax.grid(True, axis='y')
    fig.tight_layout()
    fig.savefig('test/latency_avg.png')
    print("Saved test/latency_avg.png")
    plt.close(fig)

    # 4. P50 / P999 分位数时序图（新增）
    plot_percentile_comparison(base_data, exp_data, ctrl_data,
                               "test/latency_percentile.png")

