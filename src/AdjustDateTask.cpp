#include "AdjustDateTask.h"

#include "ImageSaveManager.h"

#include <exiv2/exiv2.hpp>

/*
 * this routine parses a date in exif date format and checks that it is valid
 * format: YYYY:MM:DD HH:MM:SS
 */
static gboolean exif_date_format_is_valid(const char *date)
{
	gboolean retval = FALSE;

	if (19 == strlen(date))
	{
		int year, month, day, hour, min, sec;
		sscanf(date,"%d:%d:%d %d:%d:%d",&year, &month, &day, &hour, &min, &sec);
		struct tm tm_date = {};
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
	}

	return retval;
}






AdjustDateTask::AdjustDateTask(int adj_years, int adj_days, int adj_hours, int adj_mins, int adj_secs )
	: m_bAdjustDate(true), m_iAdjYears(adj_years), m_iAdjDays(adj_days),
	m_iAdjHours(adj_hours), m_iAdjMins(adj_mins), m_iAdjSecs(adj_secs),
	m_iCurrentFile(0), m_dFileSavePercent(0), m_flagsDateFields(DATE_FIELD_NONE)
{
}

AdjustDateTask::AdjustDateTask(tm tm_new_date)
	: m_bAdjustDate(false),
	m_iCurrentFile(0), m_dFileSavePercent(0), m_flagsDateFields(DATE_FIELD_NONE)
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
		while ((size_t)m_iCurrentFile < m_vectQuiverFiles.size() )
		{

			QuiverFile f = m_vectQuiverFiles[m_iCurrentFile];

			GFileInfo* pInfo = f.GetFileInfo();
			GDateTime* pModTime = NULL;
			if (NULL != pInfo)
			{
				// adjust the modification time of the file
				pModTime = g_file_info_get_modification_date_time(pInfo);
				
				// the adjustment is done after the exif data is modified
				// see below
			}

			
			if ((DATE_FIELD_EXIF_DATE_TIME & m_flagsDateFields) ||
				(DATE_FIELD_EXIF_DATE_TIME_ORIG & m_flagsDateFields) ||
				(DATE_FIELD_EXIF_DATE_TIME_DIGITIZED & m_flagsDateFields))
			{
				std::shared_ptr<Exiv2::ExifData> pExifData = f.GetExifData();

				if (NULL != pExifData.get())
				{
					// use date_time_original
					auto it = pExifData->findKey(
						Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
					if (pExifData->end() == it)
					{
						// try date_time
						it = pExifData->findKey(
							Exiv2::ExifKey("Exif.Image.DateTime"));
					}

					if (pExifData->end() != it)
					{
						char szDate[20];
						g_strlcpy(szDate, it->toString().c_str(), sizeof(szDate));

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
							time_t date = mktime(&tm_exif_time);
 (void)date;

							g_snprintf(szDate, 20, "%04d:%02d:%02d %02d:%02d:%02d",
								tm_exif_time.tm_year+1900,tm_exif_time.tm_mon+1,tm_exif_time.tm_mday,
								tm_exif_time.tm_hour, tm_exif_time.tm_min, tm_exif_time.tm_sec);

							if ( exif_date_format_is_valid(szDate) )
							{
								// operator[] creates the entry if missing
								if (DATE_FIELD_EXIF_DATE_TIME & m_flagsDateFields)
								{
									(*pExifData)["Exif.Image.DateTime"] = szDate;
								}
								if (DATE_FIELD_EXIF_DATE_TIME_ORIG & m_flagsDateFields)
								{
									(*pExifData)["Exif.Photo.DateTimeOriginal"] = szDate;
								}
								if (DATE_FIELD_EXIF_DATE_TIME_DIGITIZED & m_flagsDateFields)
								{
									(*pExifData)["Exif.Photo.DateTimeDigitized"] = szDate;
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
			}
			
			if (NULL != pInfo)
			{
				// adjust the modification time of the file
				if ((DATE_FIELD_MODIFICATION_TIME & m_flagsDateFields) && (NULL != pModTime))
				{
					GDateTime* pDateTimeNew = g_date_time_add_full(pModTime,
					                                               m_iAdjYears,
																   0,
					                                               m_iAdjDays,
					                                               m_iAdjHours,
					                                               m_iAdjMins,
					                                               m_iAdjSecs);

					g_file_info_set_modification_date_time(pInfo, pDateTimeNew);

					GFile* file = g_file_new_for_uri(f.GetURI());
					g_file_set_attributes_from_info(file, pInfo, G_FILE_QUERY_INFO_NONE, NULL, NULL);
					g_object_unref(file);
					
					g_date_time_unref(pDateTimeNew);
					g_date_time_unref(pModTime);
				}

				g_object_unref(pInfo);
			}

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


