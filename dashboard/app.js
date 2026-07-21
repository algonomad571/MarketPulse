let throughputChartInstance = null;
let latencyChartInstance = null;

async function fetchTelemetry() {
    try {
        // Cache buster to force reload
        const response = await fetch(`runtime_benchmark.json?t=${new Date().getTime()}`);
        if (!response.ok) throw new Error('Failed to fetch telemetry');
        const data = await response.json();
        updateDashboard(data);
    } catch (error) {
        console.error('Error fetching telemetry:', error);
        // Do not update the dashboard if we fail to fetch (so we don't flash dummy data)
    }
}

function formatNumber(num) {
    return new Intl.NumberFormat('en-US').format(Math.round(num));
}

function updateDashboard(data) {
    // Global Metrics
    document.getElementById('global-throughput').textContent = `${formatNumber(data.performance.global_throughput)} ops/s`;
    document.getElementById('elapsed-time').textContent = `${formatNumber(data.performance.elapsed_ms)} ms`;
    document.getElementById('load-imbalance').textContent = `${data.performance.load_imbalance_pct.toFixed(2)}%`;

    // Aggregate Latency
    let sumP50 = 0, sumP95 = 0, sumP99 = 0, maxLat = 0;
    data.workers.forEach(w => {
        sumP50 += w.latency_ns.p50;
        sumP95 += w.latency_ns.p95;
        sumP99 += w.latency_ns.p99;
        if (w.latency_ns.max > maxLat) maxLat = w.latency_ns.max;
    });
    let numW = data.workers.length;
    
    // Health Indicators
    let healthStatus = '🟢 HEALTHY';
    let healthColor = '#2ea043';
    let maxQueuePct = 0;
    data.workers.forEach(w => {
        let pct = (w.queue_peak / 1048576) * 100;
        if (pct > maxQueuePct) maxQueuePct = pct;
    });

    if (maxQueuePct > 80 || sumP99 / numW > 50000) {
        healthStatus = '🔴 CRITICAL';
        healthColor = '#f85149';
    } else if (maxQueuePct > 50 || sumP99 / numW > 15000) {
        healthStatus = '🟡 WARNING';
        healthColor = '#d29922';
    }

    document.querySelector('.status-indicator').style.backgroundColor = healthColor;
    document.querySelector('.status-indicator').style.boxShadow = `0 0 8px ${healthColor}`;
    document.querySelector('.status-indicator').nextSibling.textContent = ` ${healthStatus}`;

    document.getElementById('p50-latency').textContent = `${(sumP50 / numW / 1000).toFixed(1)} µs`;
    document.getElementById('p95-latency').textContent = `${(sumP95 / numW / 1000).toFixed(1)} µs`;
    document.getElementById('p99-latency').textContent = `${(sumP99 / numW / 1000).toFixed(1)} µs`;
    
    let maxLatMs = (maxLat / 1000000).toFixed(2);
    // Add context to max latency based on if it's abnormally huge
    let maxContext = maxLatMs > 5 ? " (OS Scheduler/Warmup)" : "";
    document.getElementById('max-latency').textContent = `${maxLatMs} ms${maxContext}`;

    // Render Workers
    const workersGrid = document.getElementById('workers-grid');
    workersGrid.innerHTML = '';
    data.workers.forEach(w => {
        const queuePct = ((w.queue_peak / 1048576) * 100).toFixed(2);
        const card = document.createElement('div');
        card.className = 'worker-card';
        card.innerHTML = `
            <div class="worker-header">
                <span class="worker-title">worker ${w.worker_id}</span>
                <span class="worker-core">core ${w.core}</span>
            </div>
            <div class="worker-stats">
                <span>processed ${formatNumber(w.processed)}</span>
                <span>util ${w.utilization_pct.toFixed(1)}%</span>
            </div>
            <div class="progress-bar-bg">
                <div class="progress-bar-fill" style="width: ${w.utilization_pct}%"></div>
            </div>
            <div class="worker-stats" style="margin-top: 0.8rem; margin-bottom: 0.2rem;">
                <span>queue peak: ${formatNumber(w.queue_peak)} / 1.04M</span>
                <span>${queuePct}%</span>
            </div>
            <div class="progress-bar-bg" style="height: 4px;">
                <div class="progress-bar-fill" style="width: ${queuePct}%; background-color: #8b949e;"></div>
            </div>
            <div class="worker-stats" style="margin-top: 0.6rem; margin-bottom: 0; color: #8b949e;">
                <span>p99 latency:</span>
                <span>${(w.latency_ns.p99 / 1000).toFixed(1)} µs</span>
            </div>
        `;
        workersGrid.appendChild(card);
    });

    updateCharts(data);
}

function updateCharts(data) {
    const labels = data.workers.map(w => `w${w.worker_id}`);
    const throughputs = data.workers.map(w => w.processed);
    const latencies = data.workers.map(w => w.latency_ns.p99 / 1000); // µs

    Chart.defaults.color = '#8b949e';
    Chart.defaults.font.family = "'Segoe UI', Tahoma, Geneva, Verdana, sans-serif";

    // Throughput Chart
    const ctxT = document.getElementById('throughputChart').getContext('2d');
    if (throughputChartInstance) throughputChartInstance.destroy();
    throughputChartInstance = new Chart(ctxT, {
        type: 'bar',
        data: {
            labels: labels,
            datasets: [{
                label: 'Events Processed',
                data: throughputs,
                backgroundColor: 'rgba(46, 160, 67, 0.2)',
                borderColor: '#2ea043',
                borderWidth: 1,
                borderRadius: 4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                y: { beginAtZero: true, grid: { color: 'rgba(255,255,255,0.02)' } },
                x: { grid: { display: false } }
            },
            plugins: { legend: { display: false } }
        }
    });

    // Latency Chart
    const ctxL = document.getElementById('latencyChart').getContext('2d');
    if (latencyChartInstance) latencyChartInstance.destroy();
    latencyChartInstance = new Chart(ctxL, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [{
                label: 'P99 Latency (µs)',
                data: latencies,
                backgroundColor: 'rgba(210, 153, 34, 0.1)',
                borderColor: '#d29922',
                borderWidth: 2,
                fill: true,
                tension: 0.3,
                pointBackgroundColor: '#16181d',
                pointBorderColor: '#d29922'
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                y: { beginAtZero: true, grid: { color: 'rgba(255,255,255,0.02)' } },
                x: { grid: { display: false } }
            },
            plugins: { legend: { display: false } }
        }
    });
}

document.getElementById('refresh-btn').addEventListener('click', fetchTelemetry);

// Initial load
fetchTelemetry();
