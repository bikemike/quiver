#include "PropertyView.h"

#include <cstdio>
#include <ctime>

#include <exiv2/exiv2.hpp>

#include "QuiverFile.h"

PropertyView::PropertyView()
	: m_pScrolledWindow(nullptr)
	, m_pLabel(nullptr)
{
	m_pLabel = gtk_label_new("");
	gtk_label_set_wrap(GTK_LABEL(m_pLabel), TRUE);
	gtk_label_set_selectable(GTK_LABEL(m_pLabel), TRUE);
	gtk_widget_set_halign(m_pLabel, GTK_ALIGN_START);
	gtk_widget_set_valign(m_pLabel, GTK_ALIGN_START);

	m_pScrolledWindow = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_pScrolledWindow), m_pLabel);
	gtk_widget_set_vexpand(m_pScrolledWindow, TRUE);
}

PropertyView::~PropertyView()
{
}

GtkWidget *PropertyView::GetWidget()
{
	return m_pScrolledWindow;
}

static void add_field(std::string &text, const char *name, const std::string &value)
{
	if (!value.empty())
	{
		text += name;
		text += value;
		text += "\n";
	}
}

void PropertyView::SetQuiverFile(QuiverFile file)
{
	if (!file.GetURI())
	{
		Clear();
		return;
	}

	std::string text;

	add_field(text, "Path: ", file.GetFilePath());
	add_field(text, "Name: ", file.GetFileName());

	const char *mime = file.GetMimeType();
	if (mime && *mime)
	{
		text += "Type: ";
		text += mime;
		text += "\n";
	}

	unsigned long long size = file.GetFileSize();
	if (file.IsFolder())
	{
		text += "Folder\n";
	}
	else if (size > 0)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%.2f MB (%llu bytes)", size / (1024.0 * 1024.0), size);
		text += "Size: ";
		text += buf;
		text += "\n";
	}

	if (file.IsWidthHeightSet())
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%d x %d", file.GetWidth(), file.GetHeight());
		text += "Dimensions: ";
		text += buf;
		text += "\n";
	}

	if (!file.IsFolder())
	{
		time_t t = file.GetTimeT();
		if (t > 0)
		{
			char buf[64];
			struct tm tm_time;
			localtime_r(&t, &tm_time);
			strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_time);
			text += "Date: ";
			text += buf;
			text += "\n";
		}
	}

	if (file.IsVideo())
	{
		text += "Type: Video\n";
	}
	else
	{
		std::shared_ptr<Exiv2::ExifData> exif = file.GetExifData();
		if (exif && !exif->empty())
		{
			text += "\nEXIF:\n";
			for (Exiv2::ExifData::const_iterator it = exif->begin(); it != exif->end(); ++it)
			{
				std::string key = it->key();
				if (key.find("Exif.Thumbnail.") == 0)
				{
					continue;
				}
				std::string comment = it->tagName();
				std::string value = it->print(&*exif);
				text += comment;
				text += ": ";
				text += value;
				text += "\n";
			}
		}
	}

	gtk_label_set_text(GTK_LABEL(m_pLabel), text.c_str());
}

void PropertyView::Clear()
{
	gtk_label_set_text(GTK_LABEL(m_pLabel), "");
}
