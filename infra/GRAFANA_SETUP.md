# MarketPulse Grafana Dashboard Setup

## Overview

The **MarketPulse Observability** dashboard provides comprehensive real-time visibility into the MarketPulse data pipeline performance and health. It monitors ingestion, normalization, publishing, and replay stages with detailed latency and backpressure metrics.

## Dashboard Location

- **File**: `infra/grafana/dashboards/marketpulse-observability.json`
- **UID**: `marketpulse-observability`
- **Tags**: `marketpulse`, `observability`, `performance`

## Panel Descriptions

### 1. Throughput (rate of frame processing)
- **Type**: Time Series
- **Metrics**: 
  - `rate(feed_events_ingested_total[1m])` - Feed ingestion rate
  - `rate(normalizer_events_total[1m])` - Normalization rate
  - `rate(publisher_frames_published_total[1m])` - Publishing rate
  - `rate(recorder_frames_total[1m])` - Recording rate
- **Unit**: Operations/second
- **Purpose**: Shows the throughput of each pipeline stage

### 2. Ingestion Rate (current gauge)
- **Type**: Gauge
- **Metric**: `ingestion_rate`
- **Unit**: Operations/second
- **Thresholds**: 
  - Green: 0-5000
  - Yellow: 5000-10000
  - Red: 10000+
- **Purpose**: Real-time ingestion rate gauge

### 3. Publish Rate (current gauge)
- **Type**: Gauge
- **Metric**: `publish_rate`
- **Unit**: Operations/second
- **Thresholds**: 
  - Green: 0-5000
  - Yellow: 5000-10000
  - Red: 10000+
- **Purpose**: Real-time publishing rate gauge

### 4. Replay Rate (current gauge)
- **Type**: Gauge
- **Metric**: `replay_rate`
- **Unit**: Operations/second
- **Thresholds**: 
  - Green: 0-1000
  - Yellow: 1000-5000
  - Red: 5000+
- **Purpose**: Real-time replay rate gauge

### 5. Queue Depth (items pending)
- **Type**: Time Series
- **Metric**: `queue_depth`
- **Unit**: Items
- **Purpose**: Tracks pending items in queues; spikes indicate backpressure

### 6. Backpressure Active (0=Inactive, 1=Active)
- **Type**: Gauge
- **Metric**: `backpressure_active`
- **Mapping**: 0 = Green (Inactive), 1 = Red (Active)
- **Purpose**: Binary indicator of active backpressure conditions

### 7. Producer Pauses (pause rate)
- **Type**: Time Series (Bar Chart)
- **Metric**: `rate(producer_pauses[1m])`
- **Unit**: Pauses/second
- **Purpose**: Shows how often producers are paused due to backpressure

### 8. Dropped Frames (5-minute increase)
- **Type**: Time Series (Bar Chart)
- **Metric**: `increase(dropped_frames[5m])`
- **Unit**: Frames
- **Purpose**: Tracks frame loss events; should be near zero in healthy state

### 9. P50 Latency (microseconds)
- **Type**: Time Series
- **Metric**: `p50_latency / 1000` (converted from nanoseconds)
- **Unit**: Microseconds
- **Stats**: Mean, Min, Max
- **Purpose**: 50th percentile latency for normalization

### 10. P99 Latency (microseconds)
- **Type**: Time Series
- **Metric**: `p99_latency / 1000` (converted from nanoseconds)
- **Unit**: Microseconds
- **Stats**: Mean, Min, Max
- **Purpose**: 99th percentile latency for normalization; highlights tail latencies

## Import Methods

### Method 1: Docker Compose Provisioning (Recommended)

If using Docker Compose with Grafana provisioning, the dashboard is automatically loaded from the file system:

```bash
# Dashboard is served from the provisioning directory
# Grafana reads /etc/grafana/provisioning/dashboards/marketpulse-observability.json
# No manual import needed
```

**Environment Setup** (in docker-compose.yml):
```yaml
volumes:
  - ./infra/grafana/dashboards:/etc/grafana/provisioning/dashboards
  - ./infra/grafana/datasources:/etc/grafana/provisioning/datasources
```

### Method 2: Manual Import via Grafana UI

1. **Open Grafana** → Navigate to `http://localhost:3000`
2. **Log in** with default credentials (admin/admin)
3. **Go to**: Dashboards → New → Import
4. **Upload JSON**:
   - Choose the file: `infra/grafana/dashboards/marketpulse-observability.json`
   - Or paste the JSON content directly
5. **Configure**:
   - Select Prometheus data source
   - Click "Import"

### Method 3: Using Grafana API

```bash
# Get authentication token
GRAFANA_TOKEN=$(curl -s -X POST \
  http://localhost:3000/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"user":"admin","password":"admin"}' | jq -r '.token')

# Import dashboard
curl -X POST \
  http://localhost:3000/api/dashboards/db \
  -H "Authorization: Bearer $GRAFANA_TOKEN" \
  -H "Content-Type: application/json" \
  -d @infra/grafana/dashboards/marketpulse-observability.json
```

## Metrics Data Source

The dashboard requires a **Prometheus** data source named `prometheus` configured at:
- Local: `http://localhost:9090`
- Docker: `http://prometheus:9090`

**Prometheus Configuration** (`infra/prometheus/prometheus.yml`):
```yaml
global:
  scrape_interval: 15s

scrape_configs:
  - job_name: 'marketpulse'
    static_configs:
      - targets: ['localhost:8080']  # ControlServer metrics port
```

## Metrics Exported

All metrics are exported via `GET /metrics` on the ControlServer HTTP port (default 8080):

### Counter Metrics
- `feed_events_ingested_total`
- `normalizer_events_total`
- `publisher_frames_published_total`
- `recorder_frames_total`
- `frame_distribution_total`
- `dropped_frames`
- `producer_pauses`
- `producer_resume_count`
- `normalizer_enqueue_failures_total`
- `recorder_degraded_drops_total`

### Gauge Metrics
- `ingestion_rate` (events/sec)
- `normalization_rate` (events/sec)
- `publish_rate` (frames/sec)
- `replay_rate` (frames/sec)
- `queue_depth` (items)
- `backpressure_active` (0 or 1)
- `p50_latency` (nanoseconds)
- `p99_latency` (nanoseconds)
- `queue_*_queue_depth` (per-queue depths)
- `queue_*_backpressure_active` (per-queue states)
- `queue_*_producer_pauses` (per-queue pause counts)
- `queue_*_producer_resume_count` (per-queue resume counts)

## Dashboard Features

- **Auto-Refresh**: 10-second refresh interval
- **Time Range**: Last 1 hour (configurable)
- **Dark Theme**: Professional dark mode
- **Live Mode**: Enabled for real-time updates
- **Legends**: Table format with statistics (mean, max, min)
- **Multi-Series Tooltips**: Hover to see all series values

## Performance Interpretation

### Healthy State
- Throughput: Stable and consistent
- Queue Depth: Low and stable (< 1000 items)
- Backpressure Active: 0 (inactive)
- Producer Pauses: Minimal or zero
- Dropped Frames: 0
- Latency (P99): < 100 microseconds

### Warning Signs
- **Rising Queue Depth**: Suggests downstream bottleneck
- **Backpressure Active**: 1 indicates queueing delays
- **Producer Pauses > 0**: Producers waiting for capacity
- **Dropped Frames > 0**: Data loss occurring (check recorder/publisher)
- **P99 Latency > 1000 microseconds**: High tail latencies

### Error State
- **Sustained Queue Depth**: > 50000 items
- **Continuous Backpressure**: Active for > 1 minute
- **Increasing Dropped Frames**: Suggests degraded mode
- **Producer Pauses**: Rising trend indicates congestion

## Advanced Configuration

### Customize Dashboard

Edit `infra/grafana/dashboards/marketpulse-observability.json` to:
- Adjust time ranges for specific panels
- Change alert thresholds
- Add additional queries or panels
- Modify color schemes

### Create Alerts

1. Go to dashboard → Panel → Edit
2. Click "Alert" tab
3. Configure alert conditions (e.g., queue_depth > 50000)
4. Set notification channels

### Export Dashboard

1. Dashboard menu → Share → Export for sharing
2. Select "External" to include all options
3. Share the exported JSON file

## Troubleshooting

### Metrics not appearing
- Verify Prometheus scrape configuration
- Check MarketPulse `/metrics` endpoint is responding
- Confirm Prometheus data source is selected in dashboard

### Latency metrics showing zero
- Latency histograms only record data when normalizer events are processed
- Run with sample data to generate latency metrics
- Check normalizer_event_latency_ns histogram in Prometheus

### Dashboard doesn't auto-refresh
- Verify Grafana refresh interval setting
- Check browser network tab for failed requests
- Restart Grafana service

## Files Modified/Created

- ✅ Created: `infra/grafana/dashboards/marketpulse-observability.json`
- ✅ Updated: `infra/grafana/dashboards/dashboard.yml` (provisioning config)

## Next Steps

1. Deploy Prometheus and Grafana (see `docker-compose.yml`)
2. Import or auto-provision the dashboard
3. Start MarketPulse application
4. Access Grafana dashboard at `http://localhost:3000`
5. Monitor pipeline performance in real-time
