#include <config.h>

#include "QuiverVideoOps.h"

#include <gst/gst.h>
#include <gst/video/video.h>

#include "Timer.h"

namespace QuiverVideoOps
{
	GdkPixbuf* LoadPixbuf(const gchar *uri, gint* numerator /*= NULL*/, gint* denominator /*= NULL*/)
	{
		Timer t("Load Video Pixbuf");
		GdkPixbuf* pixbuf = NULL;

		GstElement* pipeline = gst_element_factory_make("playbin2", "player");

		GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

		GstElement* pixbuf_sink = gst_element_factory_make("gdkpixbufsink", NULL);
		GstElement* fakesink    = gst_element_factory_make("fakesink", NULL);

		GstPlayFlags flags = (GstPlayFlags)0;
		g_object_get(G_OBJECT(pipeline), "flags", &flags, NULL);
		flags = (GstPlayFlags) (GST_PLAY_FLAG_DEINTERLACE|flags);

		g_object_set(G_OBJECT(pipeline), 
			"uri", uri, 
			"video-sink", pixbuf_sink, 
			"audio-sink", fakesink, 
			"flags", flags, 
			NULL);

		GstStateChangeReturn rval = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
		if (GST_STATE_CHANGE_FAILURE == rval)
		{
			gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
			gst_object_unref(bus);
			gst_object_unref(pipeline);
			return pixbuf;
		}

		GstState current = GST_STATE_VOID_PENDING;
		GstState pending = GST_STATE_VOID_PENDING;

		rval = gst_element_get_state(GST_ELEMENT(pipeline), &current, &pending, GST_SECOND*5 );

		if (GST_STATE_CHANGE_FAILURE == rval)
		{
			gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
			gst_object_unref(bus);
			gst_object_unref(pipeline);
			return pixbuf;
		}

		gint64 clip_duration = 0;
		GstFormat format = GST_FORMAT_TIME;
		gst_element_query_duration(GST_ELEMENT(pipeline), &format, &clip_duration);

		gboolean seek_started = gst_element_seek_simple(GST_ELEMENT(pipeline), format, GstSeekFlags(GST_SEEK_FLAG_FLUSH|GST_SEEK_FLAG_KEY_UNIT), (gint64)(clip_duration / 2));

		// wait for message from bus
		bool gotAspectRatio = false;
		bool pause_finished = false;
		bool seek_finished = (FALSE == seek_started);
		while (!seek_finished || !pause_finished || !gotAspectRatio) 
		{
			GstMessage* message;

			message = gst_bus_timed_pop(bus, (gotAspectRatio ? 500 : 1000) * GST_MSECOND);
			if (NULL == message)
			{
				break;
			}

			switch (GST_MESSAGE_TYPE(message))
			{
			case GST_MESSAGE_ASYNC_DONE:
				if (!pause_finished)
					pause_finished = true;
				else if (!seek_finished)
					seek_finished = true;
				break;
			case GST_MESSAGE_ELEMENT:
				if (!gotAspectRatio)
				{
					const GstStructure* structure = gst_message_get_structure(message);
					const gchar* name = gst_structure_get_name(structure);
					if ( 0 == g_strcmp0(name, "preroll-pixbuf") ||
					 0 == g_strcmp0(name, "pixbuf") )
					{
						const GValue* fraction =  gst_structure_get_value(structure, "pixel-aspect-ratio");
						gint n, d;
						n = gst_value_get_fraction_numerator(fraction);
						d = gst_value_get_fraction_denominator(fraction);
						// FIXME: bug in gstreamer has n / d reversed
						// https://bugzilla.gnome.org/show_bug.cgi?id=665882
						if (NULL != denominator)
							*denominator = n;
						if (NULL != numerator)
							*numerator = d;
						gotAspectRatio = true;
					}
				}
				break;
			default:
				break;
			}
			gst_message_unref(message);
		}


		g_object_get(pixbuf_sink, "last-pixbuf", &pixbuf, NULL);

		if (NULL != pixbuf)
		{
			g_object_ref(pixbuf);
		}
		gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
 
		gst_object_unref(bus);
		gst_object_unref(pipeline);

		return pixbuf;
	}

}
