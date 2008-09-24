#include "ImageSaveManager.h"

#include <glib.h>
#include <glib/gstdio.h>

ImageSaveManagerPtr ImageSaveManager::c_pImageSaveManagerPtr;

class ImageSaverJPEG;

typedef boost::shared_ptr<ImageSaverJPEG> ImageSaverJPEGPtr;


class ImageSaverJPEG : public IImageSaver
{
public:
	virtual ~ImageSaverJPEG(){};

	virtual std::string GetMimeType();

	virtual bool SaveImage(QuiverFile quiverFile,
			GdkPixbuf *pixbuf = NULL,
			ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL);

	virtual bool SaveImageAs(QuiverFile quiverFile, std::string strFileName,
			GdkPixbuf *pixbuf = NULL,
			ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL);
};


std::string ImageSaverJPEG::GetMimeType()
{
	return "image/jpeg";
}

static int save_jpeg_file(std::string filename,ExifData *exifData, 
		IImageSaver::ImageSaveProgressCallback callback, void* user_data);

bool ImageSaverJPEG::SaveImage(QuiverFile quiverFile,
			GdkPixbuf *pixbuf /* = NULL */,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{
	printf("save JPEG image! %s\n", quiverFile.GetURI());
	int rval = save_jpeg_file(quiverFile.GetFilePath(),quiverFile.GetExifData(), cb, user_data);
	quiverFile.Reload();
	return (0 == rval);
}

bool ImageSaverJPEG::SaveImageAs(QuiverFile quiverFile, std::string strFileName, 
			GdkPixbuf *pixbuf /* = NULL */,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{
	printf("save JPEG image as! %s\n", quiverFile.GetURI());
	return true;
}


ImageSaveManager::ImageSaveManager()
{
	// add types
	ImageSaverJPEGPtr jpegSaver(new ImageSaverJPEG());
	m_mapImageSavers.insert(ImageSaverPair(jpegSaver->GetMimeType(),jpegSaver));
}

ImageSaveManager::~ImageSaveManager()
{
}

ImageSaveManagerPtr ImageSaveManager::GetInstance()
{
	if (NULL == c_pImageSaveManagerPtr.get())
	{
		ImageSaveManagerPtr imgSaveMgrPtr(new ImageSaveManager());
		c_pImageSaveManagerPtr = imgSaveMgrPtr;
	}
	return c_pImageSaveManagerPtr;
}

bool ImageSaveManager::IsFormatSupported(std::string strMimeType)
{
	ImageSaverMap::iterator itr;
	itr = m_mapImageSavers.find(strMimeType);
	return (m_mapImageSavers.end() != itr);
}

bool ImageSaveManager::SaveImage(QuiverFile quiverFile, 
			GdkPixbuf *pixbuf /* = NULL */,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{
	bool bRetVal = false;
	ImageSaverMap::iterator itr;
	itr = m_mapImageSavers.find(quiverFile.GetMimeType());
	if (m_mapImageSavers.end() != itr)
	{
		// found the mime type
		bRetVal = itr->second->SaveImage(quiverFile,pixbuf);
	}
	return bRetVal;
}

bool ImageSaveManager::SaveImageAs(QuiverFile quiverFile, std::string strFileName,
			GdkPixbuf *pixbuf /* = NULL */,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{
	bool bRetVal = false;
	ImageSaverMap::iterator itr;
	itr = m_mapImageSavers.find(quiverFile.GetMimeType());
	if (m_mapImageSavers.end() != itr)
	{
		// found the mime type
		bRetVal = itr->second->SaveImageAs(quiverFile, strFileName, pixbuf);
	}
	return bRetVal;
}




//==============================================================================
//==============================================================================
// the following is the jpeg saving code
//==============================================================================
//==============================================================================

extern "C" {
#define JPEG_INTERNALS
#include <jpeglib.h>
}

typedef enum {
	JCOPYOPT_NONE,		/* copy no optional markers */
	JCOPYOPT_COMMENTS,	/* copy only comment (COM) markers */
	JCOPYOPT_ALL		/* copy all optional markers */
} JCOPY_OPTION;

/* Copy markers saved in the given source object to the destination object.
 * This should be called just after jpeg_start_compress() or
 * jpeg_write_coefficients().
 * Note that those routines will have written the SOI, and also the
 * JFIF APP0 or Adobe APP14 markers if selected.
 */

GLOBAL(void)
jcopy_markers_execute (j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
		       JCOPY_OPTION option)
{
  jpeg_saved_marker_ptr marker;

  /* In the current implementation, we don't actually need to examine the
   * option flag here; we just copy everything that got saved.
   * But to avoid confusion, we do not output JFIF and Adobe APP14 markers
   * if the encoder library already wrote one.
   */
  for (marker = srcinfo->marker_list; marker != NULL; marker = marker->next) {
    if (dstinfo->write_JFIF_header &&
	marker->marker == JPEG_APP0 &&
	marker->data_length >= 5 &&
	GETJOCTET(marker->data[0]) == 0x4A &&
	GETJOCTET(marker->data[1]) == 0x46 &&
	GETJOCTET(marker->data[2]) == 0x49 &&
	GETJOCTET(marker->data[3]) == 0x46 &&
	GETJOCTET(marker->data[4]) == 0)
      continue;			/* reject duplicate JFIF */
    if (dstinfo->write_Adobe_marker &&
	marker->marker == JPEG_APP0+14 &&
	marker->data_length >= 5 &&
	GETJOCTET(marker->data[0]) == 0x41 &&
	GETJOCTET(marker->data[1]) == 0x64 &&
	GETJOCTET(marker->data[2]) == 0x6F &&
	GETJOCTET(marker->data[3]) == 0x62 &&
	GETJOCTET(marker->data[4]) == 0x65)
      continue;			/* reject duplicate Adobe */
#ifdef NEED_FAR_POINTERS
    /* We could use jpeg_write_marker if the data weren't FAR... */
    {
      unsigned int i;
      jpeg_write_m_header(dstinfo, marker->marker, marker->data_length);
      for (i = 0; i < marker->data_length; i++)
	jpeg_write_m_byte(dstinfo, marker->data[i]);
    }
#else
    jpeg_write_marker(dstinfo, marker->marker,
		      marker->data, marker->data_length);
#endif
  }
}

#ifndef SAVE_MARKERS_SUPPORTED
#error please add a #define JPEG_INTERNALS above jpeglib.h include
#endif

GLOBAL(void)
jcopy_markers_setup (j_decompress_ptr srcinfo, JCOPY_OPTION option)
{
#ifdef SAVE_MARKERS_SUPPORTED
  int m;

  /* Save comments except under NONE option */
  if (option != JCOPYOPT_NONE) {
    jpeg_save_markers(srcinfo, JPEG_COM, 0xFFFF);
  }
  /* Save all types of APPn markers iff ALL option */
  if (option == JCOPYOPT_ALL) {
    for (m = 0; m < 16; m++)
      jpeg_save_markers(srcinfo, JPEG_APP0 + m, 0xFFFF);
  }
#endif /* SAVE_MARKERS_SUPPORTED */
}
/*

struct jpeg_progress
{
	struct jpeg_progress_mgr progress_mgr;
	struct jpeg_progress_mgr* progress_mgr_other;
	IImageSaver::ImageSaveProgressCallback callback;
	void * user_data;
};

static void jpeg_progress_cb(j_common_ptr cinfo)
{
	jpeg_progress* prog = (jpeg_progress*) cinfo->progress;

	printf("items: comp %ld, pass count %ld, pass lim %ld, total %ld\n",
		prog->progress_mgr.completed_passes,
		prog->progress_mgr.pass_counter,prog->progress_mgr.pass_limit,
		prog->progress_mgr.total_passes);

	double dProgress = 
		(prog->progress_mgr.completed_passes
		+ (prog->progress_mgr.pass_counter/prog->progress_mgr.pass_limit))
		 / (prog->progress_mgr.total_passes)
		+
		(prog->progress_mgr_other->completed_passes
		+ (prog->progress_mgr_other->pass_counter/prog->progress_mgr_other->pass_limit))
		 / (prog->progress_mgr_other->total_passes);

	dProgress /= 2;

	if (NULL != prog->callback)
	{
		prog->callback(dProgress, prog->user_data);
	}
	printf("progress : %f\n", dProgress);
}
*/

static int save_jpeg_file(std::string filename,ExifData *exifData,
		IImageSaver::ImageSaveProgressCallback callback, void* user_data)
{
/*
int jpeg_transform_files(char *infile, char *outfile,
			 JXFORM_CODE transform,
			 unsigned char *comment,
			 char *thumbnail, int tsize,
			 unsigned int flags)
{
*/
	int rc = 0;
	FILE *in;
	FILE *out;
	
	gchar* name_used;
	GError* error = NULL;

	/*
	struct jpeg_progress comp_progress;
	struct jpeg_progress dcomp_progress;

	comp_progress.progress_mgr_other = &dcomp_progress.progress_mgr;
	dcomp_progress.progress_mgr_other = &comp_progress.progress_mgr;

	comp_progress.callback = callback;
	dcomp_progress.callback = callback;

	comp_progress.user_data = user_data;
	dcomp_progress.user_data = user_data;

    comp_progress.progress_mgr.progress_monitor = jpeg_progress_cb;
    dcomp_progress.progress_mgr.progress_monitor = jpeg_progress_cb;
	*/

	gint fhandle = g_file_open_tmp("quiver_jpeg.XXXXXX", &name_used,&error);
	
	if (-1 == fhandle || NULL != error)
	{
		return -1;
	}
	
	close(fhandle);

	std::string strTmpFile = name_used;
	g_free(name_used);
	
	const gchar *infile  = filename.c_str();
	const gchar *outfile = strTmpFile.c_str();

	// FIXME: need to convert this to use gnome_vfs 
	/* open infile */
	in = g_fopen(infile,"r");
	if (NULL == in) 
	{
		//fprintf(stderr,"open %s: %s\n",infile,strerror(errno));
		return -1;
	}

	/* open outfile */
	out = g_fopen(outfile,"w");
	if (NULL == out)
	{
		//fprintf(stderr,"open %s: %s\n",outfile,strerror(errno));
		fclose(in);
		return -1;
	}

	/* go! */

	//rc = jpeg_transform_fp(in,out,transform,comment,thumbnail,tsize,flags);
	//

	struct jpeg_decompress_struct src;
	struct jpeg_compress_struct   dst;

	struct jpeg_error_mgr jdsterr;
	//struct longjmp_error_mgr jsrcerr;

	src.err = jpeg_std_error(&jdsterr);
	/* setup src */
	/*
	jsrcerr.jpeg.error_exit = longjmp_error_exit;
	if (setjmp(jsrcerr.setjmp_buffer))
		printf("ouch\n");
	*/


	jpeg_create_decompress(&src);
    //src.progress = &dcomp_progress.progress_mgr;

	jpeg_stdio_src(&src, in);

	/* setup dst */
	dst.err = jpeg_std_error(&jdsterr);
	jpeg_create_compress(&dst);

    //dst.progress = &comp_progress.progress_mgr;

	jpeg_stdio_dest(&dst, out);

	/* transform image */

	jvirt_barray_ptr * src_coef_arrays;
	//jvirt_barray_ptr * dst_coef_arrays;
	//jpeg_transform_info transformoption;


	jcopy_markers_setup(&src, JCOPYOPT_ALL);
	if (JPEG_HEADER_OK != jpeg_read_header(&src, TRUE))
	return -1;

	/* do exif updating */
	jpeg_saved_marker_ptr mark;
	//ExifData *ed = NULL;
	unsigned char *data;
	unsigned int  size;

	for (mark = src.marker_list; NULL != mark; mark = mark->next)
	{
		//printf("searching...\n");
		if (mark->marker == JPEG_APP0 +1)
		{
			//printf("found exif marker!\n");
			break;
		}
		continue;
	}


	if (NULL == mark) 
	{
		mark = (jpeg_marker_struct*)src.mem->alloc_large((j_common_ptr)&src,JPOOL_IMAGE,sizeof(*mark));
		memset(mark,0,sizeof(*mark));
		mark->marker = JPEG_APP0 +1;
		mark->next   = src.marker_list->next;
		src.marker_list->next = mark;
	}

	/* build new exif data block */
	exif_data_save_data(exifData,&data,&size);
	//exif_data_unref(ed);

	/* update jpeg APP2 (EXIF) marker */
	mark->data = (JOCTET*)src.mem->alloc_large((j_common_ptr)&src,JPOOL_IMAGE,size);
	mark->original_length = size;
	mark->data_length = size;
	memcpy(mark->data,data,size);
	free(data);

	/* Any space needed by a transform option must be requested before
	 * jpeg_read_coefficients so that memory allocation will be done right.
	 */
	//jtransform_request_workspace(&src, &transformoption);
	src_coef_arrays = jpeg_read_coefficients(&src);
	jpeg_copy_critical_parameters(&src, &dst);

	/*dst_coef_arrays = jtransform_adjust_parameters
	(&src, &dst, src_coef_arrays, &transformoption);
	*/
	/* Start compressor (note no image data is actually written here) */
	jpeg_write_coefficients(&dst, src_coef_arrays);

	/* Copy to the output file any extra markers that we want to preserve */
	jcopy_markers_execute(&src, &dst, JCOPYOPT_ALL);

	/* Execute image transformation, if any */
	/*
	jtransform_execute_transformation(src, dst,
					  src_coef_arrays,
					  &transformoption);
   */
	/* Finish compression and release memory */
	jpeg_finish_compress(&dst);
	jpeg_finish_decompress(&src);


	/* cleanup */
	jpeg_destroy_decompress(&src);
	jpeg_destroy_compress(&dst);

	fclose(in);
	fclose(out);

	rc = g_rename(outfile,infile);
	
	return rc;
}

