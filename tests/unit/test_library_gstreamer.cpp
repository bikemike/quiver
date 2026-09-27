#include <catch2/catch_test_macros.hpp>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include "test_helpers.h"
#include <string>

static std::string GstTimeFormat(gint64 time)
{
    gint64 total_secs = GST_TIME_AS_SECONDS(time);
    gint64 secs  = total_secs % 60;
    gint64 total_mins = total_secs / 60;
    gint64 mins  = total_mins % 60;
    gint64 hours = total_mins / 60;

    gchar* str = nullptr;
    if (0 != hours)
        str = g_strdup_printf("%lld:%02lld:%02lld", (long long)hours, (long long)mins, (long long)secs);
    else
        str = g_strdup_printf("%lld:%02lld", (long long)mins, (long long)secs);

    std::string res(str);
    g_free(str);
    return res;
}

TEST_CASE("GStreamer Library Pipeline and Media Verification", "[lib][gstreamer][video]")
{
    // gst_init_check was already run in main
    REQUIRE(gst_is_initialized() == TRUE);

    SECTION("GStreamer Time Formatting Logic")
    {
        // 0 seconds -> "0:00"
        REQUIRE(GstTimeFormat(0) == "0:00");
        // 65 seconds -> "1:05" (65 * 10^9 ns)
        REQUIRE(GstTimeFormat(65 * GST_SECOND) == "1:05");
        // 3661 seconds -> "1:01:01"
        REQUIRE(GstTimeFormat(3661 * GST_SECOND) == "1:01:01");
    }

    SECTION("GStreamer Pipeline Construction and Caps Creation")
    {
        GstElement* pipeline = gst_pipeline_new("test_pipeline");
        REQUIRE(pipeline != nullptr);

        GstCaps* caps = gst_caps_new_empty_simple("video/x-raw");
        REQUIRE(caps != nullptr);
        REQUIRE(gst_caps_is_fixed(caps) == TRUE);

        gst_caps_set_simple(caps,
            "width", G_TYPE_INT, 640,
            "height", G_TYPE_INT, 480,
            nullptr);

        GstStructure* s = gst_caps_get_structure(caps, 0);
        gint w = 0, h = 0;
        REQUIRE(gst_structure_get_int(s, "width", &w) == TRUE);
        REQUIRE(gst_structure_get_int(s, "height", &h) == TRUE);
        REQUIRE(w == 640);
        REQUIRE(h == 480);

        gst_caps_unref(caps);
        gst_object_unref(pipeline);
    }

    SECTION("GStreamer Discoverer on sample video")
    {
        std::string imagesDir = QuiverTest_GetImagesDir();
        std::string videoPath = imagesDir + "/sample_video.mp4";
        gchar* videoUri = g_filename_to_uri(videoPath.c_str(), NULL, NULL);
        REQUIRE(videoUri != NULL);

        GError* err = nullptr;
        GstDiscoverer* discoverer = gst_discoverer_new(5 * GST_SECOND, &err);
        REQUIRE(discoverer != nullptr);

        GstDiscovererInfo* info = gst_discoverer_discover_uri(discoverer, videoUri, &err);
        REQUIRE(info != nullptr);
        REQUIRE(gst_discoverer_info_get_result(info) == GST_DISCOVERER_OK);

        GstClockTime duration = gst_discoverer_info_get_duration(info);
        REQUIRE(duration > 0);

        GList* videoStreams = gst_discoverer_info_get_video_streams(info);
        REQUIRE(videoStreams != nullptr);

        GstDiscovererVideoInfo* vInfo = (GstDiscovererVideoInfo*)videoStreams->data;
        REQUIRE(gst_discoverer_video_info_get_width(vInfo) > 0);
        REQUIRE(gst_discoverer_video_info_get_height(vInfo) > 0);

        gst_discoverer_stream_info_list_free(videoStreams);
        gst_discoverer_info_unref(info);
        g_object_unref(discoverer);
        g_free(videoUri);
    }

    SECTION("GStreamer Frame Stepping Behavior")
    {
        // gst_event_new_step strictly requires rate > 0.0
        // Forward stepping creates a valid step event with rate = 1.0
        GstEvent *fwd_event = gst_event_new_step(GST_FORMAT_BUFFERS, 1, 1.0, TRUE, FALSE);
        REQUIRE(fwd_event != nullptr);
        gst_event_unref(fwd_event);

        // Frame duration calculation from framerate fraction (e.g. 30fps)
        gint fps_n = 30, fps_d = 1;
        gint64 frame_duration = (GST_SECOND * (gint64)fps_d) / (gint64)fps_n;
        REQUIRE(frame_duration == GST_SECOND / 30);

        // Stepping backward uses accurate seek to pos - frame_duration
        gint64 pos = 5 * GST_SECOND;
        gint64 target = (pos >= frame_duration) ? (pos - frame_duration) : 0;
        REQUIRE(target < pos);
        REQUIRE(target == pos - (GST_SECOND / 30));
    }

    SECTION("GStreamer Dual Sink Branching Pause and Resume")
    {
        GstElement *pipeline = gst_pipeline_new("dual_sink_test");
        REQUIRE(pipeline != nullptr);

        GstElement *src = gst_element_factory_make("videotestsrc", "src");
        GstElement *tee = gst_element_factory_make("tee", "tee");
        GstElement *q1 = gst_element_factory_make("queue", "q1");
        GstElement *q2 = gst_element_factory_make("queue", "q2");
        GstElement *sink1 = gst_element_factory_make("fakesink", "sink1");
        GstElement *sink2 = gst_element_factory_make("fakesink", "sink2");

        REQUIRE(src != nullptr);
        REQUIRE(tee != nullptr);
        REQUIRE(q1 != nullptr);
        REQUIRE(q2 != nullptr);
        REQUIRE(sink1 != nullptr);
        REQUIRE(sink2 != nullptr);

        g_object_set(G_OBJECT(sink1), "sync", TRUE, NULL);
        g_object_set(G_OBJECT(sink2), "sync", TRUE, "async", TRUE, "qos", FALSE, NULL);

        g_object_set(G_OBJECT(q2),
            "max-size-buffers", (guint)1,
            "max-size-bytes", (guint)0,
            "max-size-time", (guint64)0,
            "leaky", 2,
            NULL);

        gst_bin_add_many(GST_BIN(pipeline), src, tee, q1, sink1, q2, sink2, NULL);
        REQUIRE(gst_element_link(src, tee) == TRUE);
        REQUIRE(gst_element_link(q1, sink1) == TRUE);
        REQUIRE(gst_element_link(q2, sink2) == TRUE);

        GstPad *tp1 = gst_element_request_pad_simple(tee, "src_%u");
        GstPad *tp2 = gst_element_request_pad_simple(tee, "src_%u");
        GstPad *q1_sink = gst_element_get_static_pad(q1, "sink");
        GstPad *q2_sink = gst_element_get_static_pad(q2, "sink");

        REQUIRE(tp1 != nullptr);
        REQUIRE(tp2 != nullptr);
        REQUIRE(gst_pad_link(tp1, q1_sink) == GST_PAD_LINK_OK);
        REQUIRE(gst_pad_link(tp2, q2_sink) == GST_PAD_LINK_OK);

        gst_object_unref(tp1);
        gst_object_unref(tp2);
        gst_object_unref(q1_sink);
        gst_object_unref(q2_sink);

        // Transition PLAYING -> PAUSED -> PLAYING
        REQUIRE(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
        REQUIRE(gst_element_get_state(pipeline, NULL, NULL, 2 * GST_SECOND) == GST_STATE_CHANGE_SUCCESS);

        REQUIRE(gst_element_set_state(pipeline, GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE);
        REQUIRE(gst_element_get_state(pipeline, NULL, NULL, 2 * GST_SECOND) == GST_STATE_CHANGE_SUCCESS);

        REQUIRE(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
        REQUIRE(gst_element_get_state(pipeline, NULL, NULL, 2 * GST_SECOND) == GST_STATE_CHANGE_SUCCESS);

        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }

    SECTION("Dynamic Video-Sink Hot-Swapping (Single-Sink vs Dual-Sink)")
    {
        GstElement *pipe = gst_element_factory_make("playbin", "test_playbin");
        REQUIRE(pipe != nullptr);

        std::string imagesDir = QuiverTest_GetImagesDir();
        std::string videoPath = imagesDir + "/sample_video.mp4";
        gchar *videoUri = g_filename_to_uri(videoPath.c_str(), NULL, NULL);
        REQUIRE(videoUri != NULL);
        g_object_set(G_OBJECT(pipe), "uri", videoUri, NULL);
        g_free(videoUri);

        // Helper lambda to construct single-sink or dual-sink bin
        auto build_sink = [](bool dual) -> GstElement* {
            GstElement *bin = gst_bin_new(dual ? "dual_bin" : "single_bin");
            GstElement *upload = gst_element_factory_make("glupload", "up");
            GstElement *cc = gst_element_factory_make("glcolorconvert", "cc");
            GstElement *scaler = gst_element_factory_make("gltransformation", "trans");
            GstElement *sink = gst_element_factory_make("fakesink", "main_sink");
            g_object_set(G_OBJECT(sink), "sync", TRUE, NULL);

            if (dual) {
                GstElement *tee = gst_element_factory_make("tee", "tee");
                GstElement *qmain = gst_element_factory_make("queue", "qm");
                GstElement *qprev = gst_element_factory_make("queue", "qp");
                GstElement *prev_sink = gst_element_factory_make("fakesink", "prev_sink");
                g_object_set(G_OBJECT(prev_sink), "sync", TRUE, "async", TRUE, "qos", FALSE, NULL);

                gst_bin_add_many(GST_BIN(bin), upload, cc, tee, qmain, scaler, sink, qprev, prev_sink, NULL);
                gst_element_link(upload, cc);
                gst_element_link(cc, tee);
                gst_element_link(qmain, scaler);
                gst_element_link(scaler, sink);
                gst_element_link(qprev, prev_sink);

                GstPad *tp1 = gst_element_request_pad_simple(tee, "src_%u");
                GstPad *tp2 = gst_element_request_pad_simple(tee, "src_%u");
                GstPad *qp1 = gst_element_get_static_pad(qmain, "sink");
                GstPad *qp2 = gst_element_get_static_pad(qprev, "sink");
                gst_pad_link(tp1, qp1);
                gst_pad_link(tp2, qp2);
                gst_object_unref(tp1);
                gst_object_unref(tp2);
                gst_object_unref(qp1);
                gst_object_unref(qp2);
            } else {
                gst_bin_add_many(GST_BIN(bin), upload, cc, scaler, sink, NULL);
                gst_element_link(upload, cc);
                gst_element_link(cc, scaler);
                gst_element_link(scaler, sink);
            }

            GstPad *pad = gst_element_get_static_pad(upload, "sink");
            gst_element_add_pad(bin, gst_ghost_pad_new("sink", pad));
            gst_object_unref(pad);
            return bin;
        };

        // 1. Initial configuration: single sink
        GstElement *sink1 = build_sink(false);
        REQUIRE(sink1 != nullptr);
        g_object_set(G_OBJECT(pipe), "video-sink", sink1, NULL);

        REQUIRE(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
        REQUIRE(gst_element_get_state(pipe, NULL, NULL, 2 * GST_SECOND) == GST_STATE_CHANGE_SUCCESS);

        // 2. Hot-swap to dual sink (simulating enabling video nav control)
        REQUIRE(gst_element_set_state(pipe, GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
        GstElement *sink2 = build_sink(true);
        REQUIRE(sink2 != nullptr);
        g_object_set(G_OBJECT(pipe), "video-sink", sink2, NULL);

        REQUIRE(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
        REQUIRE(gst_element_get_state(pipe, NULL, NULL, 2 * GST_SECOND) == GST_STATE_CHANGE_SUCCESS);

        // 3. Hot-swap back to simple single sink (simulating disabling video nav control)
        REQUIRE(gst_element_set_state(pipe, GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
        GstElement *sink3 = build_sink(false);
        REQUIRE(sink3 != nullptr);
        g_object_set(G_OBJECT(pipe), "video-sink", sink3, NULL);

        REQUIRE(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
        REQUIRE(gst_element_get_state(pipe, NULL, NULL, 2 * GST_SECOND) == GST_STATE_CHANGE_SUCCESS);

        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
    }

    SECTION("Allocation Query Pool Margin Expansion (Anti-Starvation)")
    {
        GstElement *pipe = gst_element_factory_make("playbin", "test_alloc_pipe");
        REQUIRE(pipe != nullptr);

        std::string imagesDir = QuiverTest_GetImagesDir();
        std::string videoPath = imagesDir + "/sample_video.mp4";
        gchar *videoUri = g_filename_to_uri(videoPath.c_str(), NULL, NULL);
        REQUIRE(videoUri != NULL);
        g_object_set(G_OBJECT(pipe), "uri", videoUri, NULL);
        g_free(videoUri);

        // Build dual-sink bin with allocation probe
        GstElement *bin = gst_bin_new("alloc_test_bin");
        GstElement *upload = gst_element_factory_make("glupload", "up");
        GstElement *cc = gst_element_factory_make("glcolorconvert", "cc");
        GstElement *tee = gst_element_factory_make("tee", "tee");
        GstElement *qm = gst_element_factory_make("queue", "qm");
        GstElement *qp = gst_element_factory_make("queue", "qp");
        GstElement *scaler = gst_element_factory_make("gltransformation", "trans");
        GstElement *msink = gst_element_factory_make("fakesink", "msink");
        GstElement *psink = gst_element_factory_make("fakesink", "psink");

        g_object_set(G_OBJECT(qm), "max-size-buffers", 1, "max-size-bytes", 0, "max-size-time", (guint64)0, NULL);
        g_object_set(G_OBJECT(qp), "max-size-buffers", 1, "max-size-bytes", 0, "max-size-time", (guint64)0, "leaky", 2, NULL);
        g_object_set(G_OBJECT(msink), "sync", TRUE, NULL);
        g_object_set(G_OBJECT(psink), "sync", TRUE, "async", TRUE, "qos", FALSE, NULL);

        gst_bin_add_many(GST_BIN(bin), upload, cc, tee, qm, scaler, msink, qp, psink, NULL);
        gst_element_link(upload, cc);
        gst_element_link(cc, tee);
        gst_element_link(qm, scaler);
        gst_element_link(scaler, msink);
        gst_element_link(qp, psink);

        GstPad *tp1 = gst_element_request_pad_simple(tee, "src_%u");
        GstPad *tp2 = gst_element_request_pad_simple(tee, "src_%u");
        GstPad *qp1 = gst_element_get_static_pad(qm, "sink");
        GstPad *qp2 = gst_element_get_static_pad(qp, "sink");
        gst_pad_link(tp1, qp1);
        gst_pad_link(tp2, qp2);
        gst_object_unref(tp1);
        gst_object_unref(tp2);
        gst_object_unref(qp1);
        gst_object_unref(qp2);

        // Track minimum buffers observed returning upstream
        guint observed_min_buffers = 0;
        GstPad *up_sink = gst_element_get_static_pad(upload, "sink");

        gst_pad_add_probe(up_sink,
            GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
            [](GstPad *pad, GstPadProbeInfo *info, gpointer user_data) -> GstPadProbeReturn {
                if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM) {
                    GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);
                    if (query != NULL && GST_QUERY_TYPE(query) == GST_QUERY_ALLOCATION) {
                        static thread_local bool s_bInProbe = false;
                        if (s_bInProbe) return GST_PAD_PROBE_OK;
                        struct Guard { Guard() { s_bInProbe = true; } ~Guard() { s_bInProbe = false; } } g;

                        GstPadQueryFunction qfunc = GST_PAD_QUERYFUNC(pad);
                        if (qfunc != NULL) {
                            qfunc(pad, GST_OBJECT_PARENT(pad), query);
                        } else {
                            gst_pad_query_default(pad, GST_OBJECT_PARENT(pad), query);
                        }

                        const guint target_min = 32;
                        guint n_pools = gst_query_get_n_allocation_pools(query);
                        if (n_pools > 0) {
                            for (guint i = 0; i < n_pools; ++i) {
                                GstBufferPool *pool = NULL;
                                guint size = 0, min_buf = 0, max_buf = 0;
                                gst_query_parse_nth_allocation_pool(query, i, &pool, &size, &min_buf, &max_buf);
                                min_buf = std::max(min_buf, target_min);
                                if (max_buf != 0 && max_buf < min_buf)
                                    max_buf = min_buf;
                                gst_query_set_nth_allocation_pool(query, i, pool, size, min_buf, max_buf);
                                if (pool) gst_object_unref(pool);
                            }
                        } else {
                            gst_query_add_allocation_pool(query, NULL, 0, target_min, 0);
                        }

                        guint *pObserved = (guint *)user_data;
                        if (pObserved && gst_query_get_n_allocation_pools(query) > 0) {
                            guint min_buf = 0;
                            gst_query_parse_nth_allocation_pool(query, 0, NULL, NULL, &min_buf, NULL);
                            *pObserved = min_buf;
                        }
                        return GST_PAD_PROBE_HANDLED;
                    }
                }
                return GST_PAD_PROBE_OK;
            },
            &observed_min_buffers, NULL);

        gst_element_add_pad(bin, gst_ghost_pad_new("sink", up_sink));
        gst_object_unref(up_sink);

        g_object_set(G_OBJECT(pipe), "video-sink", bin, NULL);

        REQUIRE(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
        REQUIRE(gst_element_get_state(pipe, NULL, NULL, 2 * GST_SECOND) == GST_STATE_CHANGE_SUCCESS);

        // Verify the allocation query returning to the decoder was boosted to >= 32
        CHECK(observed_min_buffers >= 32);

        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
    }
}

