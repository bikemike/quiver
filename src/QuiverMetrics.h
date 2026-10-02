#ifndef QVMETRICS_H
#define QVMETRICS_H

#include <glib.h>

/*  Lightweight, always-compiled-in performance tracing.
 *
 *  Off by default: every call site collapses to a single integer compare, so
 *  an untraced build pays nothing beyond the branch.  Turn it on with
 *
 *      QUIVER_METRICS=1 ./quiver
 *
 *  and lines are appended to $QUIVER_METRICS_FILE (default
 *  /tmp/quiver-metrics.log) as tab-separated records:
 *
 *      QUIVER-METRIC <seq> <scope> <name> <value> <unit>
 *
 *  one per line, so the log can be filtered with grep/awk and pasted around.
 *  Set QUIVER_METRICS_STDERR=1 to also echo to stderr.  Traces come from
 *  several threads, so writes are serialised.
 *
 *  C linkage on purpose: libquiver/ is plain C and instruments too.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* True when tracing is switched on; cache it if you are on a hot path. */
gboolean quiver_metrics_enabled(void);

/* Monotonic microseconds, same clock the tracer reports in. */
gint64 quiver_metrics_now_us(void);

/* Records one measurement.  value/unit are free-form; unit is usually "ms",
 * "count" or "bytes". */
void quiver_metric_emit(const char* scope, const char* name,
	double value, const char* unit);

/* Adds delta to a counter and reports the new total. */
void quiver_metric_count(const char* scope, const char* name, gint64 delta);

/* A point-in-time marker with no value, e.g. the start of a selection. */
void quiver_metric_mark(const char* scope, const char* name);

/* Bumps a per-scope generation counter and marks it, so the records that
 * follow can be grouped per selection/scroll pass in the log. */
void quiver_metric_epoch(const char* scope);

/* Current generation for a scope; 0 before the first epoch. */
guint64 quiver_metric_current_epoch(const char* scope);

/* Logs the counters accumulated so far and resets them.  Called on exit. */
void quiver_metrics_flush(void);

#ifdef __cplusplus
} /* extern "C" */

/* RAII timer.  Reports the elapsed time to the destructor, so an early return
 * or a thrown exception cannot silently drop the measurement:
 *
 *      QuiverMetricTimer timer("exif", "load");
 */
class QuiverMetricTimer
{
public:
	QuiverMetricTimer(const char* scope, const char* name)
		: m_scope(scope), m_name(name), m_start(0)
	{
		m_active = quiver_metrics_enabled();
		if (m_active)
			m_start = quiver_metrics_now_us();
	}

	~QuiverMetricTimer()
	{
		stop();
	}

	/* Ends the measurement early, for when the scope has to be closed before
	 * the next stage starts.  Idempotent, so the destructor stays safe. */
	void stop()
	{
		if (m_active)
		{
			m_active = false;
			quiver_metric_emit(m_scope, m_name,
				(gdouble)(quiver_metrics_now_us() - m_start) / 1000.0,
				"ms");
		}
	}

	QuiverMetricTimer(const QuiverMetricTimer&) = delete;
	QuiverMetricTimer& operator=(const QuiverMetricTimer&) = delete;

	/* Elapsed so far, for stages that want to log a mid-point. */
	double elapsed_ms() const
	{
		return m_active ? (gdouble)(quiver_metrics_now_us() - m_start) / 1000.0 : 0.0;
	}

private:
	const char* m_scope;
	const char* m_name;
	gint64 m_start;
	bool m_active;
};
#endif /* __cplusplus */

#endif /* QVMETRICS_H */
