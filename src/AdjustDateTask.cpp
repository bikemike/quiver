#include "AdjustDateTask.h"

#include "ImageSaveManager.h"

/*
 * this routine parses a date in exif date format and checks that it is valid
 * format: YYYY:MM:DD HH:MM:SS
 */
#include <libexif/exif-ifd.h>
#include <libexif/exif-entry.h>
#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>
#include <libexif/exif-tag.h>

static void
exif_convert_arg_to_entry (const char *set_value, ExifEntry *e, ExifByteOrder o)
{
	unsigned int i;
	char *value_p;

	/*
	 * ASCII strings are handled separately,
	 * since they don't require any conversion.
	 */
	if (e->format == EXIF_FORMAT_ASCII) {
		if (e->data) free (e->data);

		e->components = strlen (set_value) + 1;
		e->size = sizeof (char) * e->components;
		e->data = (unsigned char*)malloc (e->size);
		if (!e->data) 
		{
			//fprintf (stderr, ("Not enough memory."));
			//fputc ('\n', stderr);
			//exit (1);
		}
		else
		{
			strcpy ((char*)e->data, set_value);
		}
		return;
	}

	value_p = (char*) set_value;
	for (i = 0; i < e->components; i++) 
	{
		const char *begin, *end;
		unsigned char *buf, s;
		const char comp_separ = ' ';

		begin = value_p;
		value_p = index (begin, comp_separ);
		if (!value_p) 
		{
			if (i != e->components - 1)
			{
				break;
			}
			else
			{
				end = begin + strlen (begin);
			}
		} 
		else
		{
			end = value_p++;
		}

		buf = (unsigned char*)malloc ((end - begin + 1) * sizeof (char));
		strncpy ((char*)buf, begin, end - begin);
		buf[end - begin] = '\0';

		s = exif_format_get_size (e->format);
		switch (e->format)
		{
			case EXIF_FORMAT_ASCII:
				//internal_error (); /* Previously handled */
				break;
			case EXIF_FORMAT_SHORT:
				exif_set_short (e->data + (s * i), o, atoi ((char*)buf));
				break;
			case EXIF_FORMAT_LONG:
				exif_set_long (e->data + (s * i), o, atol ((char*)buf));
				break;
			case EXIF_FORMAT_SLONG:
				exif_set_slong (e->data + (s * i), o, atol ((char*)buf));
				break;
			case EXIF_FORMAT_RATIONAL:
			case EXIF_FORMAT_SRATIONAL:
			case EXIF_FORMAT_BYTE:
			default:
				break;
		}

		free (buf);
	}
}


static gboolean exif_date_format_is_valid(const char *date)
{
	gboolean retval = FALSE;
	
	if (19 == strlen(date))
	{
		int year, month, day, hour, min, sec;
		sscanf(date,"%d:%d:%d %d:%d:%d",&year, &month, &day, &hour, &min, &sec);
		struct tm tm_date = {0};
		tm_date.tm_sec = sec;
		tm_date.tm_min = min;
		tm_date.tm_hour = hour;
		tm_date.tm_mday = day;
		tm_date.tm_mon = month -1;
		tm_date.tm_year = year - 1900;
		tm_date.tm_isdst = -1;


		if ( tm_date.tm_sec == sec &&
			tm_date.tm_min == min &&
			tm_date.tm_hour == hour &&
			tm_date.tm_mday == day &&
			tm_date.tm_mon == month -1 &&
			tm_date.tm_year == year - 1900 )
		{
			retval = TRUE;
		}
		else
		{
		}
		
	}
	else
	{
	}

	return retval;
}
static void exif_update_entry(ExifData *pExifData, ExifIfd ifd,ExifTag tag,const char *value)
{
	ExifEntry *e;

	/* If the entry doesn't exist, create it. */
	e = exif_content_get_entry (pExifData->ifd[ifd], tag);
	if (!e) 
	{
		e = exif_entry_new ();
		exif_content_add_entry (pExifData->ifd[ifd], e);
		exif_entry_initialize (e, tag);
	}

	/* Now set the value and save the data. */
	exif_convert_arg_to_entry (value, e, exif_data_get_byte_order (pExifData));
	
	//save_exif_data_to_file (exifData, *args, fname);
}






AdjustDateTask::AdjustDateTask(int adj_years, int adj_days, int adj_hours, int adj_mins, int adj_secs )
	: m_bAdjustDate(true), m_iAdjYears(adj_years), m_iAdjDays(adj_days),
	m_iAdjHours(adj_hours), m_iAdjMins(adj_mins), m_iAdjSecs(adj_secs),
	m_iCurrentFile(0), m_dFileSavePercent(0), m_flagsDateFields(DATE_FIELD_EXIF_DATE_TIME)
{
}

AdjustDateTask::AdjustDateTask(tm tm_new_date)
	: m_bAdjustDate(false),
	m_iCurrentFile(0), m_dFileSavePercent(0), m_flagsDateFields(DATE_FIELD_EXIF_DATE_TIME)
{
	m_tmNewDate = tm_new_date;
}

void AdjustDateTask::SetAdjustDateFields(DateFields f)
{
	m_flagsDateFields = f;
}

void AdjustDateTask::AddAdjustDateFields(DateFields f)
{
	m_flagsDateFields = (DateFields)(f|m_flagsDateFields);
}

std::string AdjustDateTask::GetDescription() const
{
	return "Adjust Exif Date";
}

// quantity type may be kb, items, images, files, 
// or anything else the task iterates over
std::string AdjustDateTask::GetIterationTypeName(bool shortname, bool plural ) const
{
	if (shortname)
	{
		if (plural)
			return "imgs";
		return "img";
	}

	if (plural)
		return "images";
	return "image";
}

// get total and current iteration
int AdjustDateTask::GetTotalIterations() const
{
	return m_vectQuiverFiles.size();
}

int AdjustDateTask::GetCurrentIteration() const
{
	return m_iCurrentFile;
}


double AdjustDateTask::GetProgress() const
{
	if (IsFinished())
	{
		return 1.;
	}
	return m_iCurrentFile / (double)m_vectQuiverFiles.size() + m_dFileSavePercent;
}

void AdjustDateTask::AddFile(QuiverFile quiverFile)
{
	m_vectQuiverFiles.push_back(quiverFile);
}

void AdjustDateTask::AddFiles(std::vector<QuiverFile> vectQuiverFiles)
{
	m_vectQuiverFiles.insert(m_vectQuiverFiles.end(), vectQuiverFiles.begin(), vectQuiverFiles.end());
}

void AdjustDateTask::Run()
{
	if (m_bAdjustDate)
	{
		// adjust exif date
		while (m_iCurrentFile < m_vectQuiverFiles.size() )
		{

			QuiverFile f = m_vectQuiverFiles[m_iCurrentFile];

			ExifData *pExifData = f.GetExifData();
		
			time_t date = 0;
			if (NULL != pExifData)
			{
				// use date_time_original
				ExifEntry* pEntry;
				pEntry = exif_data_get_entry(pExifData,EXIF_TAG_DATE_TIME);
				if (NULL != pEntry)
				{
					char szDate[20];
					exif_entry_get_value(pEntry,szDate,20);
		
					tm tm_exif_time;
					int num_substs = sscanf(szDate,"%04d:%02d:%02d %02d:%02d:%02d",
						&tm_exif_time.tm_year,
						&tm_exif_time.tm_mon,
						&tm_exif_time.tm_mday,
						&tm_exif_time.tm_hour,
						&tm_exif_time.tm_min,
						&tm_exif_time.tm_sec);
					tm_exif_time.tm_year -= 1900;
					tm_exif_time.tm_mon -= 1;
					tm_exif_time.tm_isdst = -1;
					if (6 == num_substs)
					{
						tm_exif_time.tm_year += m_iAdjYears;
						tm_exif_time.tm_mday += m_iAdjDays;
						tm_exif_time.tm_hour += m_iAdjHours;
						tm_exif_time.tm_min +=  m_iAdjMins;
						tm_exif_time.tm_sec +=  m_iAdjSecs;
						// successfully parsed date
						date = mktime(&tm_exif_time);

						g_snprintf(szDate, 20, "%04d:%02d:%02d %02d:%02d:%02d",
							tm_exif_time.tm_year+1900,tm_exif_time.tm_mon+1,tm_exif_time.tm_mday,
							tm_exif_time.tm_hour, tm_exif_time.tm_min, tm_exif_time.tm_sec);

						//printf("ymd: %s\n", szDate);


						if ( exif_date_format_is_valid(szDate) )
						{
							if (DATE_FIELD_EXIF_DATE_TIME & m_flagsDateFields)
							{
								exif_update_entry(pExifData, EXIF_IFD_0, EXIF_TAG_DATE_TIME, szDate);
							}
							if (DATE_FIELD_EXIF_DATE_TIME_ORIG & m_flagsDateFields)
							{
								exif_update_entry(pExifData, EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_ORIGINAL, szDate);
							}
							if (DATE_FIELD_EXIF_DATE_TIME_DIGITIZED & m_flagsDateFields)
							{
								exif_update_entry(pExifData, EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_DIGITIZED, szDate);
							}

							f.SetExifData(pExifData);

							if (f.Modified())
							{
								//now save the file:
								ImageSaveManager::GetInstance()->SaveImage(f);
							}
						} 


						
					}
					
				}
			}
			exif_data_unref(pExifData);

			++m_iCurrentFile;

			EmitTaskProgressUpdatedEvent();

			if (ShouldPause())
			{
				break;
			}

			if (ShouldCancel())
			{
				break;
			}
		}				
	}
	else // set date
	{

	}

}


