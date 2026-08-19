#include <config.h>

#include "QuiverVideoOps.h"

#include <gst/gst.h>
#include <gst/video/video.h>

#include "Timer.h"

namespace QuiverVideoOps
{
	GdkPixbuf* LoadPixbuf(const gchar *uri, gint* numerator /*= NULL*/, gint* denominator /*= NULL*/, gint64 position_ns /*= -1*/)
	{
		Timer t("Load Video Pixbuf");

		GdkPixbuf* pixbuf = NULL;

		GstElement* pipeline = gst_element_factory_make("playbin", "player");

		GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

		GstElement* bin = gst_bin_new("pixbuf_bin");
		GstElement* convert = gst_element_factory_make("videoconvert", "convert");
		GstElement* pixbuf_sink = gst_element_factory_make("gdkpixbufsink", "pixbuf_sink");
		gst_bin_add_many(GST_BIN(bin), convert, pixbuf_sink, NULL);
		gst_element_link(convert, pixbuf_sink);
		GstPad* pad = gst_element_get_static_pad(convert, "sink");
		gst_element_add_pad(bin, gst_ghost_pad_new("sink", pad));
		gst_object_unref(pad);

		GstElement* fakesink    = gst_element_factory_make("fakesink", NULL);

		GstPlayFlags flags = (GstPlayFlags)0;
		g_object_get(G_OBJECT(pipeline), "flags", &flags, NULL);
		flags = (GstPlayFlags) (GST_PLAY_FLAG_DEINTERLACE|0x1000|flags); // 0x1000 is GST_PLAY_FLAG_FORCE_SW_DECODERS

		g_object_set(G_OBJECT(pipeline), 
			"uri", uri, 
			"video-sink", bin, 
			"audio-sink", fakesink, 
			"flags", flags, 
			NULL);

		bool pause_finished = false;
		GstStateChangeReturn rval = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
		if (GST_STATE_CHANGE_FAILURE == rval)
		{
			gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
			gst_object_unref(bus);
			gst_object_unref(pipeline);
			return pixbuf;
		}
		else if (GST_STATE_CHANGE_SUCCESS == rval)
		{
			pause_finished = true;
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
		else if (GST_STATE_CHANGE_SUCCESS == rval)
		{
			pause_finished = true;
		}

		gint64 clip_duration = 0;
		gst_element_query_duration(GST_ELEMENT(pipeline), GST_FORMAT_TIME, &clip_duration);

		gint64 seek_target = (position_ns >= 0) ? position_ns : (gint64)(clip_duration / 2);
		
		// Clear bus of old preroll-pixbuf messages before seeking
		while (GstMessage* msg = gst_bus_pop(bus)) {
			gst_message_unref(msg);
		}

		gboolean seek_started = gst_element_seek_simple(GST_ELEMENT(pipeline), GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH|GST_SEEK_FLAG_ACCURATE), seek_target);

		// wait for message from bus
		bool gotAspectRatio = false;
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
						gint n = 1, d = 1;
						GstPad* pad = gst_element_get_static_pad(pixbuf_sink, "sink");
						if (pad != NULL)
						{
							GstCaps* caps = gst_pad_get_current_caps(pad);
							if (caps != NULL)
							{
								GstStructure* caps_struct = gst_caps_get_structure(caps, 0);
								if (caps_struct != NULL && gst_structure_has_field(caps_struct, "pixel-aspect-ratio"))
								{
									gst_structure_get_fraction(caps_struct, "pixel-aspect-ratio", &n, &d);
								}
								gst_caps_unref(caps);
							}
							gst_object_unref(pad);
						}

						if (NULL != numerator)
							*numerator = n;
						if (NULL != denominator)
							*denominator = d;
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

		rval = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

		gst_object_unref(bus);
		gst_object_unref(pipeline);

		return pixbuf;

	}

}
