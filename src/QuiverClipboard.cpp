#include "QuiverClipboard.h"

#include <glib.h>

namespace QuiverClipboard
{


	//---------------------------------------------------------------------
	// content deserializers (wire mime types -> GType string)
	//---------------------------------------------------------------------

	static void deserialize_mime_to_string(GdkContentDeserializer *deserializer)
	{
		GInputStream *stream = gdk_content_deserializer_get_input_stream(deserializer);
		std::string data;
		char buf[8192];
		GError *error = NULL;
		while (true)
		{
			gssize n = g_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
			if (n > 0)
				data.append(buf, (size_t)n);
			else
				break;
		}

		GValue *value = gdk_content_deserializer_get_value(deserializer);
		g_value_init(value, G_TYPE_STRING);
		if (error != NULL)
			g_value_set_static_string(value, "");
		else if (!data.empty() && g_utf8_validate(data.c_str(), data.size(), NULL))
			g_value_set_string(value, data.c_str());
		else
			g_value_set_static_string(value, "");
		if (error != NULL)
			g_error_free(error);

		gdk_content_deserializer_return_success(deserializer);
	}

	//---------------------------------------------------------------------

	void Init()
	{
		static bool s_registered = false;
		if (s_registered)
			return;
		s_registered = true;

		/* Accept file-manager payloads and hand them to a G_TYPE_STRING caller
		 * (a GtkDropTarget(G_TYPE_STRING) or a clipboard read of that type). */
		gdk_content_register_deserializer("text/uri-list", G_TYPE_STRING,
			deserialize_mime_to_string, NULL, NULL);
		gdk_content_register_deserializer("application/x-gnome-copied-files", G_TYPE_STRING,
			deserialize_mime_to_string, NULL, NULL);
		gdk_content_register_deserializer("text/plain;charset=utf-8", G_TYPE_STRING,
			deserialize_mime_to_string, NULL, NULL);
		gdk_content_register_deserializer("text/plain", G_TYPE_STRING,
			deserialize_mime_to_string, NULL, NULL);
	}

	static std::string JoinUriList(const std::list<std::string>& uris)
	{
		std::string text;
		for (std::list<std::string>::const_iterator it = uris.begin(); it != uris.end(); ++it)
		{
			if (!text.empty())
				text += "\n";
			text += *it;
		}
		return text;
	}

	static std::string JoinPathList(const std::list<std::string>& uris)
	{
		std::string text;
		for (std::list<std::string>::const_iterator it = uris.begin(); it != uris.end(); ++it)
		{
			gchar *path = g_filename_from_uri(it->c_str(), NULL, NULL);
			if (!text.empty())
				text += "\n";
			if (path != NULL)
			{
				text += path;
				g_free(path);
			}
			else
				text += *it;
		}
		return text;
	}

	GdkContentProvider * MakeContentProvider(const std::list<std::string>& uris,
	                                         bool bCut,
	                                         bool bTextOnly,
	                                         bool bIncludeUriList)
	{
		Init();

		/* Plain-text payload: local file paths, one per line.
		 * Offered as text/plain and text/plain;charset=utf-8 so drops into
		 * text editors insert the file path into the current document instead
		 * of the editor attempting to open the binary image as a file. */
		const std::string plainText = JoinPathList(uris);
		GBytes *plainBytes = g_bytes_new(plainText.c_str(), plainText.size());
		GdkContentProvider *plainUtf8Provider = gdk_content_provider_new_for_bytes(
			"text/plain;charset=utf-8", plainBytes);
		GdkContentProvider *plainProvider = gdk_content_provider_new_for_bytes(
			"text/plain", plainBytes);
		g_bytes_unref(plainBytes);

		if (bTextOnly)
		{
			GdkContentProvider *providers[2] = { plainUtf8Provider, plainProvider };
			return gdk_content_provider_new_union(providers, 2);
		}

		/* GNOME copied files payload for file managers (Nautilus, etc.) and
		 * in-app moves/copies. */
		const std::string listText = JoinUriList(uris);
		const std::string gnomeText = (bCut ? "cut\n" : "copy\n") + listText;
		GBytes *gnomeBytes = g_bytes_new(gnomeText.c_str(), gnomeText.size());
		GdkContentProvider *gnomeProvider = gdk_content_provider_new_for_bytes(
			"application/x-gnome-copied-files", gnomeBytes);
		g_bytes_unref(gnomeBytes);

		/* In-app G_TYPE_STRING provider holding the cut/copy header + URIs */
		GValue strVal = G_VALUE_INIT;
		g_value_init(&strVal, G_TYPE_STRING);
		g_value_set_string(&strVal, gnomeText.c_str());
		GdkContentProvider *strProvider = gdk_content_provider_new_for_value(&strVal);
		g_value_unset(&strVal);

		if (bIncludeUriList)
		{
			/* text/uri-list is offered on the clipboard (for foreign file managers),
			 * but omitted during drag-and-drop so text editors don't hijack the drop
			 * to open binary image files. */
			GBytes *listBytes = g_bytes_new(listText.c_str(), listText.size());
			GdkContentProvider *listProvider = gdk_content_provider_new_for_bytes(
				"text/uri-list", listBytes);
			g_bytes_unref(listBytes);

			GdkContentProvider *providers[5] = {
				plainUtf8Provider, plainProvider, gnomeProvider, strProvider, listProvider
			};
			return gdk_content_provider_new_union(providers, 5);
		}
		else
		{
			GdkContentProvider *providers[4] = {
				plainUtf8Provider, plainProvider, gnomeProvider, strProvider
			};
			return gdk_content_provider_new_union(providers, 4);
		}
	}

	void SetClipboard(const std::list<std::string>& uris, bool bCut, GdkClipboard *clipboard)
	{
		if (clipboard == NULL)
		{
			GdkDisplay *display = gdk_display_get_default();
			if (display != NULL)
				clipboard = gdk_display_get_clipboard(display);
		}
		if (clipboard == NULL)
			return;
		gdk_clipboard_set_content(clipboard, MakeContentProvider(uris, bCut, false, true));
	}

	bool ParseClipboardText(const std::string& text,
	                        std::list<std::string>& urisOut, bool& cutOut)
	{
		urisOut.clear();
		cutOut = false;

		std::string line;
		bool bHeaderHandled = false;
		for (size_t i = 0; i <= text.size(); ++i)
		{
			const char c = (i < text.size()) ? text[i] : '\n';
			if (c == '\n' || c == '\r')
			{
				/* strip a trailing \r kept by CRLF payloads */
				if (!line.empty() && *line.rbegin() == '\r')
					line.erase(line.size() - 1);

				if (!bHeaderHandled)
				{
					if (line == "copy")
						cutOut = false;
					else if (line == "cut")
						cutOut = true;
					else
					{
						/* no header: the whole payload is a URI list or path list */
						if (!line.empty())
							urisOut.push_back(line);
					}
					bHeaderHandled = true;
				}
				else if (!line.empty())
				{
					urisOut.push_back(line);
				}
				line.clear();
			}
			else
				line += c;
		}

		/* Convert local absolute paths to file:// URIs and keep valid file:// URIs */
		std::list<std::string> filtered;
		for (std::list<std::string>::const_iterator it = urisOut.begin();
			it != urisOut.end(); ++it)
		{
			if (g_str_has_prefix(it->c_str(), "file://"))
				filtered.push_back(*it);
			else if (g_path_is_absolute(it->c_str()))
			{
				gchar *uri = g_filename_to_uri(it->c_str(), NULL, NULL);
				if (uri)
				{
					filtered.push_back(uri);
					g_free(uri);
				}
			}
		}
		urisOut.swap(filtered);
		return !urisOut.empty();
	}

	//---------------------------------------------------------------------
	// reading the system clipboard back (best effort)
	//---------------------------------------------------------------------

	struct ClipboardReadState
	{
		bool done;
		bool ok;
		std::string text;
		GMainLoop *loop;
	};

	static void clipboard_read_value_cb(GObject *source, GAsyncResult *result, gpointer user_data)
	{
		ClipboardReadState *st = (ClipboardReadState*)user_data;
		GError *error = NULL;
		const GValue *value = gdk_clipboard_read_value_finish(
			GDK_CLIPBOARD(source), result, &error);
		st->done = true;
		if (error != NULL)
		{
			g_error_free(error);
		}
		else if (value != NULL && G_VALUE_HOLDS_STRING(value))
		{
			const char *str = g_value_get_string(value);
			if (str != NULL)
				st->text = str;
		}
		g_main_loop_quit(st->loop);
	}

	bool GetClipboardUris(std::list<std::string>& urisOut, bool& cutOut, GdkClipboard *clipboard)
	{
		urisOut.clear();
		cutOut = false;
		Init();
		if (clipboard == NULL)
		{
			GdkDisplay *display = gdk_display_get_default();
			if (display != NULL)
				clipboard = gdk_display_get_clipboard(display);
		}
		if (clipboard == NULL)
			return false;

		for (int attempt = 0; attempt < 2; ++attempt)
		{
			ClipboardReadState st;
			st.done = false;
			st.ok = false;
			st.loop = g_main_loop_new(NULL, FALSE);
			gdk_clipboard_read_value_async(clipboard, G_TYPE_STRING,
				G_PRIORITY_DEFAULT, NULL, clipboard_read_value_cb, &st);
			g_main_loop_run(st.loop);
			st.done = true;
			g_main_loop_unref(st.loop);

			if (ParseClipboardText(st.text, urisOut, cutOut))
				return true;

			/* First attempt may have landed on the gnome header payload; a
			 * bare-list read then yields nothing new.  Nothing more to try. */
			break;
		}
		return false;
	}

}