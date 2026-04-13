#include "ImageSaveManager.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include <gio/gio.h>

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

static int save_jpeg_file(std::string filename, GdkPixbuf* pixbuf, ExifData *exifData, 
		IImageSaver::ImageSaveProgressCallback callback, void* user_data);

bool ImageSaverJPEG::SaveImage(QuiverFile quiverFile,
			GdkPixbuf *pixbuf /* = NULL */,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{
	printf("save JPEG image! %s\n", quiverFile.GetURI());
	ExifData* exifData = quiverFile.GetExifData();
	int rval = save_jpeg_file(quiverFile.GetURI(), pixbuf, exifData, cb, user_data);
	exif_data_unref(exifData);
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
//#define JPEG_INTERNALS
#include <jpeglib.h>
#include "libjpeg/jpegint.h"
#include "libjpeg/transupp.h"
}


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

	//printf("items: comp %d, pass count %ld, pass lim %ld, total %d\n",
		//prog->progress_mgr.completed_passes,
		//prog->progress_mgr.pass_counter,prog->progress_mgr.pass_limit,
		//prog->progress_mgr.total_passes);

	double dProgress = 
		(prog->progress_mgr.completed_passes
		+ ((double)prog->progress_mgr.pass_counter/(prog->progress_mgr.pass_limit-1)))
		 / (double)(prog->progress_mgr.total_passes)
		+
		(prog->progress_mgr_other->completed_passes
		+ ((double)prog->progress_mgr_other->pass_counter/(prog->progress_mgr_other->pass_limit -1)))
		 / (double)(prog->progress_mgr_other->total_passes);

	dProgress /= 2;

	if (NULL != prog->callback)
	{
		prog->callback(dProgress, prog->user_data);
	}
	printf("progress : %f\n", dProgress);
}

extern "C"
{
EXTERN(void) jpeg_gio_dest JPP((j_compress_ptr cinfo, GOutputStream * outfile));
EXTERN(void) jpeg_gio_src JPP((j_decompress_ptr cinfo, GInputStream * infile));
}

static int save_jpeg_file(std::string filename, GdkPixbuf* pixbuf, ExifData *exifData,
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
	GFile* ginfile;
	GFile* goutfile;
	GInputStream* in;
	GOutputStream* out;
	
	gchar* name_used;
	GError* error = NULL;


	gint fhandle = g_file_open_tmp("quiver_jpeg.XXXXXX", &name_used,&error);
	
	if (NULL != error)
	{
		g_error_free(error);
		return -1;
	}

	if (-1 == fhandle)
	{
		return -1;
	}
	
	close(fhandle);

	std::string strTmpFile = name_used;
	g_free(name_used);
	
	const gchar *infile  = filename.c_str();
	const gchar *outfile = strTmpFile.c_str();

	/* open infile */
	ginfile  = g_file_new_for_uri(infile);
	goutfile = g_file_new_for_path(outfile);

	in = G_INPUT_STREAM(g_file_read(ginfile, NULL, NULL));
	printf("opening infile: %s\n",infile);
	if (NULL == in)
	{
		//fprintf(stderr,"open %s: %s\n",infile,strerror(errno));
		g_object_unref(ginfile);
		g_object_unref(goutfile);
		return -1;
	}

	/* open outfile */
	out = G_OUTPUT_STREAM(g_file_replace(goutfile, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL));
	if (NULL == out)
	{
		//fprintf(stderr,"open %s: %s\n",outfile,strerror(errno));
		g_object_unref(ginfile);
		g_object_unref(goutfile);
		g_object_unref(in);
		return -1;
	}

	/* go! */

	//rc = jpeg_transform_fp(in,out,transform,comment,thumbnail,tsize,flags);
	//

	struct jpeg_decompress_struct src;
	struct jpeg_compress_struct   dst;

	// sett up the progress monitors
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

	jpeg_gio_src(&src, in);

	/* setup dst */
	dst.err = jpeg_std_error(&jdsterr);
	jpeg_create_compress(&dst);


	// hook up the progress monitors
    src.progress = &dcomp_progress.progress_mgr;
    dst.progress = &comp_progress.progress_mgr;

	jpeg_gio_dest(&dst, out);

	/* transform image */

	jvirt_barray_ptr* src_coef_arrays;
	//jvirt_barray_ptr * dst_coef_arrays;
	//jpeg_transform_info transformoption;


	jcopy_markers_setup(&src, JCOPYOPT_ALL_BUT_EXIF);
	if (JPEG_HEADER_OK != jpeg_read_header(&src, TRUE))
		return -1;

	// perfect transform possible?
	int MCU_width = src.max_h_samp_factor * DCTSIZE;
	int MCU_height = src.max_v_samp_factor * DCTSIZE;

	boolean perfect =  jtransform_perfect_transform( 
		src.image_width,
		src.image_height,
		MCU_width,
		MCU_height,JXFORM_ROT_90);
	 
	printf("Can do perfect transform: %dx%d with mcu %dx%d   %d\n", src.image_width, src.image_height, MCU_width, MCU_height,perfect);	

	
	printf("got here\n");

	/* do exif updating */
	//ExifData *ed = NULL;
	unsigned char *exif_raw_data;
	unsigned int  exif_raw_size;

	/* build new exif data block */
	// exifData is saved to a data block allocated by the function
	// - it must be freed
	exif_data_save_data(exifData,&exif_raw_data,&exif_raw_size);
	//exif_data_unref(ed);

	/* Any space needed by a transform option must be requested before
	 * jpeg_read_coefficients so that memory allocation will be done right.
	 */
	//jtransform_request_workspace(&src, &transformoption);
	src_coef_arrays = jpeg_read_coefficients(&src);

	if (NULL == pixbuf)	
	{
		jpeg_copy_critical_parameters(&src, &dst);

		/*dst_coef_arrays = jtransform_adjust_parameters
		(&src, &dst, src_coef_arrays, &transformoption);
		*/
		/* Start compressor (note no image data is actually written here) */
		jpeg_write_coefficients(&dst, src_coef_arrays);

		/* Copy to the output file any extra markers that we want to preserve */
		jpeg_write_marker(&dst, JPEG_APP0+1, exif_raw_data, exif_raw_size);
		jcopy_markers_execute(&src, &dst, JCOPYOPT_ALL_BUT_EXIF);
	}
	else
	{
		// copy in pixbuf
		dst.image_width = gdk_pixbuf_get_width(pixbuf); 	/* image width and height, in pixels */
		dst.image_height = gdk_pixbuf_get_height(pixbuf);
		dst.input_components = 3;	/* # of color components per pixel */
		dst.in_color_space = JCS_RGB; /* colorspace of input image */

		jpeg_set_defaults(&dst);
		/* Make optional parameter settings here */
		// FIXME: allow setting quality
		// jpeg_set_quality (j_compress_ptr cinfo, int quality, boolean force_baseline)	

		JSAMPROW row_pointer[1];	/* pointer to a single row */

		int row_stride = gdk_pixbuf_get_rowstride (pixbuf);
		guchar *pixels;

		pixels = gdk_pixbuf_get_pixels (pixbuf);

		int n_channels = gdk_pixbuf_get_n_channels (pixbuf);

		g_assert(n_channels == 3);
		g_assert (gdk_pixbuf_get_bits_per_sample (pixbuf) == 8);


		jpeg_start_compress(&dst, TRUE);

		/* Copy to the output file any extra markers that we want to preserve */
		jpeg_write_marker(&dst, JPEG_APP0+1, exif_raw_data, exif_raw_size);
		jcopy_markers_execute(&src, &dst, JCOPYOPT_ALL_BUT_EXIF);

		if (gdk_pixbuf_get_has_alpha(pixbuf))
		{
			//FIXME implement alpha code
			printf("has alpha chan!!\n");
		}
		else
		{
			while (dst.next_scanline < dst.image_height)
			{
				//p = pixels + y * row_stride + x * n_channels;
				row_pointer[0] = & pixels[dst.next_scanline * row_stride];
				jpeg_write_scanlines(&dst, row_pointer, 1);
			}
		}


	}
	free(exif_raw_data);


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

	g_object_unref(in);
	g_object_unref(out);

	// move tmp file to original file name
	//rc = g_rename(outfile,infile);
	//printf("got here %d\n", rc);
	//FIXME need to preserve file attributes of the original
	g_file_move(
			goutfile,
			ginfile,
			(GFileCopyFlags)(G_FILE_COPY_OVERWRITE | G_FILE_COPY_ALL_METADATA),
			NULL,
			NULL,
			NULL,
			NULL);


	g_object_unref(ginfile);
	g_object_unref(goutfile);

	return rc;
}

