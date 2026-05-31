# Two-Stage Thread Pool for Raft Message Processing

**Status**: Implemented

## Problem Statement

The current Raft thread pool has only 2 threads handling all Raft messages. When append_entries with data trigger blocking I/O operations (10+ seconds in `applier_create_req()` → `localize_journal_entry_prepare()`), these threads become occupied. This causes head-of-line blocking where fast messages (pre-vote, heartbeat) get queued behind slow operations, leading to election timeouts and cluster instability.

## Solution: Two-Stage Thread Pool Architecture

Separate fast validation/routing operations from blocking I/O operations using two dedicated thread pools.

### Architecture

```
gRPC Thread
    │ post()
    ▼
Raft Thread Pool (configured threads)
    - Validate group_id, intended_addr
    - Lookup raft_server from _raft_servers map
    - Update service-level metrics (wait time, active threads)
    - Update per-group metrics (COUNTER_INCREMENT)
    - Determine message type (fast vs slow)
    - Route or process
    │
    ├─ is_slow_message()? (append_entries/install_snapshot)
    │   │ post()
    │   ▼
    │  I/O Thread Pool (config: io_thread_pool_size, default: 4)
    │   - process_req() blocks for seconds
    │   - send_response()
    │   - Track service-level I/O pool metrics
    │
    └─ Other messages (fast)
        - Process on Raft thread (< 1ms)
        - send_response()
```

### Key Benefits

1. **No head-of-line blocking**: Fast messages never wait behind slow operations
2. **Resource efficient**: Raft pool stays small, I/O pool configurable
3. **Great observability**: Service-level metrics track wait time and active threads
4. **Simple configuration**: Fixed pool sizes via config (no auto-tuning complexity)

## Component Details

### Thread Pools

**Raft Thread Pool** (`_raft_thread_pool`)
- Size: Configured via `NURAFT_MESG_CONFIG(raft_append_entries_thread_cnt)`
- Purpose: Fast validation, routing, processing lightweight messages
- Expected utilization: Low (handles 100+ msgs/sec with 2 threads)

**I/O Thread Pool** (`_io_thread_pool`)
- Size: Configured via `NURAFT_MESG_CONFIG(io_thread_pool_size)`
  - Default: 4 threads (if config is 0 or not set)
  - Recommended: Match or exceed number of Raft groups
- Purpose: Handle blocking append_entries and snapshot operations
- Expected utilization: Varies by load, tracked via metrics

### Message Classification

**Slow Messages** (route to I/O pool):
- `append_entries_request` - Data replication with potential I/O blocking
- `install_snapshot_request` - Snapshot transfer operations

**Fast Messages** (process on Raft thread):
- All other message types: pre_vote, vote, responses, join/leave, heartbeat, etc.

Classification implemented in `is_slow_message()`:
```cpp
bool proto_service::is_slow_message(nuraft::msg_type type) const {
    return type == nuraft::msg_type::append_entries_request ||
           type == nuraft::msg_type::install_snapshot_request;
}
```

### Metrics Architecture

**Service-Level Metrics** (global, in `service_metrics` class):

| Metric | Type | Description |
|--------|------|-------------|
| `raft_pool_wait_time_us` | Histogram | Time from post() to Raft pool lambda execution |
| `io_pool_wait_time_us` | Histogram | Time from post() to I/O pool lambda execution |
| `raft_pool_active_threads` | Gauge | Number of threads currently executing in Raft pool |
| `io_pool_active_threads` | Gauge | Number of threads currently executing in I/O pool |

Metrics group: "RAFTService" (global)

**Per-Group Metrics** (in `group_metrics` class):

| Metric | Type | Description |
|--------|------|-------------|
| `group_steps` | Counter | Total messages received per group |
| `group_sends` | Counter | Total messages sent per group |
| `append_entries_latency_us` | Histogram | End-to-end latency per group |

**Logging** (per-request in I/O pool):
```
LOGT("I/O pool executed [group={}] [type={}] wait={}us exec={}us", ...);
```

### Thread Safety

**Atomic Counter Management**:
- `_raft_pool_active_threads` - Tracks active Raft pool threads
- `_io_pool_active_threads` - Tracks active I/O pool threads
- RAII guard pattern via `atomic_counter_guard`:
  ```cpp
  struct atomic_counter_guard {
      std::atomic<int>& counter;
      explicit atomic_counter_guard(std::atomic<int>& c) : counter(c) { ++counter; }
      ~atomic_counter_guard() { --counter; }
  };
  ```

**Metrics Update Points**:
- Raft pool: Update on entry to Raft thread lambda (after queue wait)
- I/O pool: Update on entry to I/O thread lambda (after queue wait)
- Gauges reflect instantaneous active thread count
- Histograms capture queue wait time distribution

## Implementation Details

### Key Components

**proto_service Class Members**:
```cpp
class proto_service : public msg_service {
private:
    boost::asio::thread_pool _raft_thread_pool;
    boost::asio::thread_pool _io_thread_pool;
    std::atomic<int> _raft_pool_active_threads{0};
    std::atomic<int> _io_pool_active_threads{0};
    service_metrics _service_metrics;  // Global metrics
};
```

**service_metrics Class**:
```cpp
class service_metrics : public sisl::MetricsGroupWrapper {
public:
    service_metrics() : sisl::MetricsGroupWrapper("RAFTService", "global") {
        REGISTER_HISTOGRAM(raft_pool_wait_time_us, ...);
        REGISTER_HISTOGRAM(io_pool_wait_time_us, ...);
        REGISTER_GAUGE(raft_pool_active_threads, ...);
        REGISTER_GAUGE(io_pool_active_threads, ...);
    }
};
```

### Message Flow

1. **gRPC receives message** → posts to `_raft_thread_pool`
2. **Raft pool lambda**:
   - Records wait time and active threads in `_service_metrics`
   - Validates group_id and intended_addr
   - Looks up raft_server
   - Updates per-group `group_steps` counter
   - Classifies message via `is_slow_message()`
3. **Slow path** (append_entries/install_snapshot):
   - Posts to `_io_thread_pool` with captured raft_server
   - I/O lambda records wait time and active threads
   - Executes `execute_step()` (blocking I/O)
   - Sends response
4. **Fast path** (all other messages):
   - Executes `execute_step()` directly on Raft thread
   - Sends response

### Configuration

**I/O Thread Pool Size**:
```cpp
size_t proto_service::calculate_io_pool_size() {
    auto config_size = NURAFT_MESG_CONFIG(io_thread_pool_size);
    return (config_size > 0) ? config_size : 4;  // Default to 4 threads
}
```

Called in constructor: `_io_thread_pool{calculate_io_pool_size()}`

**Configuration Parameter**:
- `io_thread_pool_size` (default: 0, treated as 4)
- Set > 0 for explicit pool size
- Pool size is fixed at construction (no dynamic resizing)

## Error Handling

- Exception handling unchanged (isolated in I/O threads via try-catch in `execute_step()`)
- Graceful shutdown: thread pools auto-join on destruction
- Saturation: tasks queue normally via boost::asio::thread_pool
- Missing groups: validated early on Raft thread, returns NOT_FOUND immediately

## Testing

### Integration Tests (`thread_pool_tests.cpp`)

1. **SlowMessagesRouteToIOPool**: Verifies append_entries complete through I/O pool
2. **FastMessagesProcessedOnRaftThread**: Verifies leadership changes (pre_vote/vote) complete quickly
3. **NoHeadOfLineBlocking**: Verifies fast messages bypass slow queue under load
4. **MetricsCountersConsistency**: Verifies atomic counters don't leak
5. **AppendEntriesSucceedThroughIOPool**: Verifies no data loss through routing

### Unit Tests

**MessageTypeTest.IsSlowMessageClassification**: Validates message type classification logic

## Success Criteria

- ✅ No election timeouts under heavy append_entries load
- ✅ Fast messages (pre-vote/vote) complete within 5-8 seconds even during blocking I/O
- ✅ All append_entries complete successfully (no data loss)
- ✅ Service-level metrics properly track pool utilization
- ✅ Atomic counters correctly increment/decrement (no leaks)

## Future Enhancements

- Dynamic I/O pool resizing based on runtime workload
- Per-group I/O pools for stronger isolation
- Priority queues within I/O pool for different message priorities
- Adaptive timeout based on queue depth metrics
- Expose pool size metrics via REST API for runtime monitoring
