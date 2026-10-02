#include "QuiverMetrics.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

/* Implementation notes:
 *  - The enable flag is resolved once; every emit path checks it first so a
 *    disabled run does no formatting and takes no lock.
 *  - The file is opened lazily on the first record, so merely running the app
 *    with the variable exported never creates a log.
 *  - Formatting happens into a stack buffer and is written under one mutex,
 *    because thumbnails and the loader run metrics off the main thread.
 */

namespace {

/* GMutex is zero-initialisable, so a namespace-scope instance is fine. */
GMutex g_lock;
FILE* g_file = nullptr;
bool g_resolved = false;
bool g_enabled = false;
bool g_echo_stderr = false;
guint64 g_seq = 0;

/* Monotonic clock reading, in ms, taken once so every record can carry the
 * time since metrics came up.  Without this, time spent in code that emits no
 * records is simply absent from the log: the records before and after a stall
 * look adjacent even when seconds elapsed between them. */
gint64 g_origin_us = 0;

std::map<std::string, gint64> g_counters;

const char* env_or_null(const char* name)
{
	const char* v = g_getenv(name);
	if (v == nullptr || *v == '\0') return nullptr;
	return v;
}

void resolve()
{
	if (g_resolved) return;

	const char* on = env_or_null("QUIVER_METRICS");
	g_enabled = (on != nullptr) && (0 != g_strcmp0(on, "0"));
	g_echo_stderr = (nullptr != env_or_null("QUIVER_METRICS_STDERR"));
	g_resolved = true;
}

void ensure_open_locked()
{
	if (g_file != nullptr) return;

	const char* path = env_or_null("QUIVER_METRICS_FILE");
	if (path == nullptr)
		path = "/tmp/quiver-metrics.log";

	g_file = fopen(path, "a");
	if (g_file != nullptr)
		setvbuf(g_file, nullptr, _IOLBF, 0);
	g_origin_us = quiver_metrics_now_us();
}

void write_locked(const char* scope, const char* name, double value,
	const char* unit)
{
	ensure_open_locked();
	const double elapsed_ms =
		(double)(quiver_metrics_now_us() - g_origin_us) / 1000.0;
	if (g_file != nullptr)
	{
		fprintf(g_file, "QUIVER-METRIC\t%llu\t%s\t%s\t%.3f\t%s\t%.3f\n",
			(unsigned long long)++g_seq,
			(scope != nullptr) ? scope : "-",
			(name != nullptr) ? name : "-",
			value,
			(unit != nullptr) ? unit : "-",
			elapsed_ms);
	}
	if (g_echo_stderr)
	{
		fprintf(stderr, "QUIVER-METRIC\t%llu\t%s\t%s\t%.3f\t%s\t%.3f\n",
			(unsigned long long)g_seq,
			(scope != nullptr) ? scope : "-",
			(name != nullptr) ? name : "-",
			value,
			(unit != nullptr) ? unit : "-",
			elapsed_ms);
	}
}

} /* namespace */

extern "C" {

gboolean quiver_metrics_enabled(void)
{
	g_mutex_lock(&g_lock);
	resolve();
	gboolean enabled = g_enabled;
	g_mutex_unlock(&g_lock);
	return enabled;
}

gint64 quiver_metrics_now_us(void)
{
	return g_get_monotonic_time();
}

void quiver_metric_emit(const char* scope, const char* name, double value,
	const char* unit)
{
	g_mutex_lock(&g_lock);
	resolve();
	if (g_enabled)
		write_locked(scope, name, value, unit);
	g_mutex_unlock(&g_lock);
}

void quiver_metric_count(const char* scope, const char* name, gint64 delta)
{
	g_mutex_lock(&g_lock);
	resolve();
	if (g_enabled)
	{
		gint64& total = g_counters[std::string(scope) + "/" + name];
		total += delta;
		write_locked(scope, name, (double)total, "count");
	}
	g_mutex_unlock(&g_lock);
}

void quiver_metric_mark(const char* scope, const char* name)
{
	g_mutex_lock(&g_lock);
	resolve();
	if (g_enabled)
		write_locked(scope, name, 0.0, "mark");
	g_mutex_unlock(&g_lock);
}

void quiver_metric_epoch(const char* scope)
{
	g_mutex_lock(&g_lock);
	resolve();
	if (g_enabled)
	{
		/* One map holds the generation for "epoch" scopes; a record is
		 * emitted so the log marks where each pass starts. */
		gint64& n = g_counters[std::string(scope) + "/#epoch"];
		n += 1;
		write_locked(scope, "epoch", (double)n, "count");
	}
	g_mutex_unlock(&g_lock);
}

guint64 quiver_metric_current_epoch(const char* scope)
{
	g_mutex_lock(&g_lock);
	resolve();
	guint64 n = 0;
	if (g_enabled)
	{
		auto it = g_counters.find(std::string(scope) + "/#epoch");
		if (it != g_counters.end())
			n = (guint64)it->second;
	}
	g_mutex_unlock(&g_lock);
	return n;
}

void quiver_metrics_flush(void)
{
	g_mutex_lock(&g_lock);
	resolve();
	if (g_enabled && !g_counters.empty())
	{
		for (const auto& kv : g_counters)
		{
			const std::string& key = kv.first;
			std::string::size_type slash = key.rfind('/');
			std::string scope = (slash != std::string::npos) ? key.substr(0, slash) : key;
			std::string name  = (slash != std::string::npos) ? key.substr(slash + 1) : key;
			write_locked(scope.c_str(), ("total " + name).c_str(),
				(double)kv.second, "count");
		}
	}
	if (g_file != nullptr)
	{
		fflush(g_file);
		fclose(g_file);
		g_file = nullptr;
	}
	g_mutex_unlock(&g_lock);
}

} /* extern "C" */
