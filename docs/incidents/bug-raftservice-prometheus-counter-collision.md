# RAFTService Prometheus Counter Collision

## Symptom

Storage Manager processes aborted when the metrics endpoint was scraped:

```text
GET /metrics HTTP/1.1
User-Agent: otelcontribcol/...
```

The core dump stopped at the SISL Prometheus counter monotonicity assertion:

```text
#5 sisl::PrometheusReportCounter::set_value(this=..., value=2)
   at sisl/metrics/prometheus_reporter.hpp:49
#6 sisl::CounterDynamicInfo::publish(this=..., value=...)
#7 MetricsGroupImpl::publish_result()::<lambda>(idx=0, result={m_value=2})
#12 sisl::WisrBufferMetricsGroup::gather_result(...)
#13 sisl::MetricsGroupImpl::publish_result(...)
#14 sisl::MetricsFarm::report(sisl::kTextFormat)
#15 storage_mgr::http_svc::get_prometheus_metrics(...)
```

GDB showed the Prometheus counter already held a larger value:

```text
idx = 0
result = 2
Prometheus counter value = 24
m_grp_name = "RAFTService"
m_inst_name = "global"
```

## Root Cause Chain

1. RAFTService registers two independent SISL counters with different internal metric names.

   Evidence: `src/proto/proto_service.cpp` registers:

   ```cpp
   REGISTER_COUNTER(raft_pool_msg_count, "Messages processed on Raft thread", "raft_service_counter");
   REGISTER_COUNTER(io_pool_msg_count, "Messages routed to I/O pool", "raft_service_counter");
   ```

   Status: CONFIRMED.

2. Both counters use the same Prometheus report name and no differentiating label.

   Evidence: the third `REGISTER_COUNTER` argument for both counters is `"raft_service_counter"`, and no label pair is provided.

   `REGISTER_COUNTER` passes the internal counter name and all additional arguments to `MetricsGroupImpl::register_counter`:

   ```cpp
   #define REGISTER_COUNTER(name, ...) \
       { \
           auto& nc{sisl::NamedCounter< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance()}; \
           nc.set_index(this->m_impl_ptr->register_counter(nc.get_name(), __VA_ARGS__)); \
       }
   ```

   `CounterStaticInfo` then stores `report_name` as the exported metric name when it is provided:

   ```cpp
   CounterStaticInfo::CounterStaticInfo(const std::string& name, const std::string& desc,
                                        const std::string& report_name,
                                        const metric_label& label_pair) :
           m_name(report_name.empty() ? name : report_name), m_desc(desc) {
       if (!label_pair.first.empty() && !label_pair.second.empty()) { m_label_pair = label_pair; }
   }
   ```

   Therefore both internal counters map to the same exported metric name:

   ```text
   raft_pool_msg_count -> raft_service_counter
   io_pool_msg_count   -> raft_service_counter
   ```

   Status: CONFIRMED.

3. SISL Prometheus reporter uses `entity=<instance_name>` plus the optional label pair as the Prometheus label set.

   Evidence: `sisl::PrometheusReporter::add_counter` builds labels as `{{"entity", instance_name}}` when `label_pair` is empty.

   ```cpp
   if (!label_pair.first.empty() && !label_pair.second.empty()) {
       label_pairs = {{"entity", instance_name}, {label_pair.first, label_pair.second}};
   } else {
       label_pairs = {{"entity", instance_name}};
   }
   ```

   Status: CONFIRMED.

4. RAFTService uses the instance name `global`.

   Evidence: `service_metrics() : sisl::MetricsGroupWrapper("RAFTService", "global")`.

   Status: CONFIRMED.

5. Therefore both SISL counters publish to the same Prometheus time series.

   Evidence: GDB showed both counter static entries have `m_name = "raft_service_counter"`, empty labels, and `m_inst_name = "global"`.

   Status: CONFIRMED.

6. The two SISL counters can diverge because they are incremented in different paths.

   Evidence: `io_pool_msg_count` is incremented when routing work to the I/O pool; `raft_pool_msg_count` is incremented when work is processed on the Raft thread.

   Status: CONFIRMED.

7. During `/metrics`, SISL publishes counters by internal index. The first counter tried to publish value `2` into a Prometheus counter already at `24`.

   Evidence: GDB showed `idx=0`, `result=2`, and `this->m_counter.gauge_.value_ = 24`.

   Status: CONFIRMED.

8. `PrometheusReportCounter::set_value` computes `diff = value - m_counter.Value()` and asserts `diff >= 0`, so publishing `2` after `24` aborts the process.

   Evidence: core dump stopped at `assert(diff >= 0)`.

   Status: CONFIRMED.

## Trigger Conditions

1. `/metrics` is scraped by Prometheus or OTel collector.
2. `io_pool_msg_count` and `raft_pool_msg_count` have different values.
3. The larger counter publishes first or has already advanced the shared Prometheus counter in a previous scrape.
4. A smaller counter later publishes to the same Prometheus time series.
5. SISL treats the smaller value as a counter rollback and aborts.

## SKIPped Conditions

None. The causal chain was confirmed by GDB output and code inspection.

## Fix

Give each RAFTService metric variant a distinct Prometheus label, matching the existing histogram pattern. The counters should publish as:

```text
raft_service_counter{entity="global", op="raft_pool"}
raft_service_counter{entity="global", op="io_pool"}
```

The gauges should also get labels because they currently share `raft_service_gauge{entity="global"}` and overwrite each other, even though gauges do not assert on value decreases.
