#include <config.h>
#include "ImageSaveManager.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include <gio/gio.h>

#include <exiv2/exiv2.hpp>

ImageSaveManagerPtr ImageSaveManager::c_pImageSaveManagerPtr;

class ImageSaverJPEG;

typedef boost::shared_ptr<ImageSaverJPEG> ImageSaverJPEGPtr;


class ImageSaverJPEG : public IImageSaver
{
public:
	virtual ~ImageSaverJPEG(){};

	virtual std::string GetMimeType();

	virtual bool SaveImage(QuiverFile quiverFile,
			GdkTexture *texture = NULL,
			ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL);

	virtual bool SaveImageAs(QuiverFile quiverFile, std::string strFileName,
			GdkTexture *texture = NULL,
			ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL);

#if HAVE_GDK_PIXBUF
	virtual bool SaveImage(QuiverFile quiverFile,
			GdkPixbuf *pixbuf,
			ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL);

	virtual bool SaveImageAs(QuiverFile quiverFile, std::string strFileName,
			GdkPixbuf *pixbuf,
			ImageSaveProgressCallback cb = NULL,
			void* user_data = NULL);
#endif
};


std::string ImageSaverJPEG::GetMimeType()
{
	return "image/jpeg";
}

static int jpeg_apply_exif_metadata(const char* pPath,
		const std::shared_ptr<Exiv2::ExifData>& pExifData,
		const std::shared_ptr<const Exiv2::ExifData>& pExifDataOriginal);

static int save_jpeg_file(std::string filename, GdkTexture* texture,
		std::shared_ptr<Exiv2::ExifData> exifData,
		std::shared_ptr<const Exiv2::ExifData> exifDataOriginal,
		IImageSaver::ImageSaveProgressCallback callback, void* user_data);

#if HAVE_GDK_PIXBUF
static int save_jpeg_file_pixbuf(std::string filename, GdkPixbuf* pixbuf,
		std::shared_ptr<Exiv2::ExifData> exifData,
		std::shared_ptr<const Exiv2::ExifData> exifDataOriginal,
		IImageSaver::ImageSaveProgressCallback callback, void* user_data);
#endif

bool ImageSaverJPEG::SaveImage(QuiverFile quiverFile,
			GdkTexture *texture /* = NULL */,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{
	std::shared_ptr<Exiv2::ExifData> exifData = quiverFile.GetExifData();
	std::shared_ptr<const Exiv2::ExifData> exifDataOriginal = quiverFile.GetExifDataOriginal();
	int rval = save_jpeg_file(quiverFile.GetURI(), texture, exifData, exifDataOriginal, cb, user_data);
	quiverFile.Reload();
	return (0 == rval);
}

bool ImageSaverJPEG::SaveImageAs(QuiverFile quiverFile, std::string strFileName, 
			GdkTexture *texture /* = NULL */,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{ (void)quiverFile; (void)strFileName;  (void)texture;  (void)cb;  (void)user_data; 
	return true;
}

#if HAVE_GDK_PIXBUF
bool ImageSaverJPEG::SaveImage(QuiverFile quiverFile,
			GdkPixbuf *pixbuf,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{
	std::shared_ptr<Exiv2::ExifData> exifData = quiverFile.GetExifData();
	std::shared_ptr<const Exiv2::ExifData> exifDataOriginal = quiverFile.GetExifDataOriginal();
	int rval = save_jpeg_file_pixbuf(quiverFile.GetURI(), pixbuf, exifData, exifDataOriginal, cb, user_data);
	quiverFile.Reload();
	return (0 == rval);
}

bool ImageSaverJPEG::SaveImageAs(QuiverFile quiverFile, std::string strFileName, 
			GdkPixbuf *pixbuf,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{ (void)quiverFile; (void)strFileName;  (void)pixbuf;  (void)cb;  (void)user_data; 
	return true;
}
#endif


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
			GdkTexture *texture /* = NULL */,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{ (void)cb;  (void)user_data; 
	bool bRetVal = false;
	ImageSaverMap::iterator itr;
	itr = m_mapImageSavers.find(quiverFile.GetMimeType());
	if (m_mapImageSavers.end() != itr)
	{
		// found the mime type
		bRetVal = itr->second->SaveImage(quiverFile, texture);
	}
	return bRetVal;
}

bool ImageSaveManager::SaveImageAs(QuiverFile quiverFile, std::string strFileName,
			GdkTexture *texture /* = NULL */,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{ (void)cb;  (void)user_data; 
	bool bRetVal = false;
	ImageSaverMap::iterator itr;
	itr = m_mapImageSavers.find(quiverFile.GetMimeType());
	if (m_mapImageSavers.end() != itr)
	{
		// found the mime type
		bRetVal = itr->second->SaveImageAs(quiverFile, strFileName, texture);
	}
	return bRetVal;
}

#if HAVE_GDK_PIXBUF
bool ImageSaveManager::SaveImage(QuiverFile quiverFile, 
			GdkPixbuf *pixbuf,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{ (void)cb;  (void)user_data; 
	bool bRetVal = false;
	ImageSaverMap::iterator itr;
	itr = m_mapImageSavers.find(quiverFile.GetMimeType());
	if (m_mapImageSavers.end() != itr)
	{
		// found the mime type
		bRetVal = itr->second->SaveImage(quiverFile, pixbuf);
	}
	return bRetVal;
}

bool ImageSaveManager::SaveImageAs(QuiverFile quiverFile, std::string strFileName,
			GdkPixbuf *pixbuf,
			ImageSaveProgressCallback cb /* = NULL */,
			void* user_data /*= NULL*/)
{ (void)cb;  (void)user_data; 
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
#endif




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
}

extern "C"
{
EXTERN(void) jpeg_gio_dest JPP((j_compress_ptr cinfo, GOutputStream * outfile));
EXTERN(void) jpeg_gio_src JPP((j_decompress_ptr cinfo, GInputStream * infile));
}

/* Fold the datums the user actually edited into the JPEG at @pPath.

   The original EXIF block is copied through libjpeg verbatim, so reading the
   file back here recovers its own byte order ("II" or "MM") together with every
   tag's original datatype and any MakerNote.  Only datums whose value differs
   from the load-time snapshot are written, and each keeps the datatype already
   in the destination, so no untouched tag can change type or endianness just
   because the file was saved.  Orientation is pinned to SHORT regardless, which
   also repairs files an older save left as SLONG. */
static int jpeg_apply_exif_metadata(const char* pPath,
		const std::shared_ptr<Exiv2::ExifData>& pExifData,
		const std::shared_ptr<const Exiv2::ExifData>& pExifDataOriginal)
{
	if (NULL == pExifData.get() || pExifData->empty())
	{
		return 0;
	}

	int rc = 0;
	try
	{
		auto image = Exiv2::ImageFactory::open(pPath);
		image->readMetadata();

		Exiv2::ExifData& dst = image->exifData();
		for (Exiv2::ExifData::const_iterator src = pExifData->begin();
				src != pExifData->end(); ++src)
		{
			/* Orientation is always rewritten.  Besides carrying the edit that
			 * pins its datatype to SHORT, so saving a file an older save left
			 * as SLONG repairs it whether or not this save was asked to move
			 * the value at all. */
			const bool bOrientation = ("Exif.Image.Orientation" == src->key());

			if (!bOrientation && NULL != pExifDataOriginal.get())
			{
				Exiv2::ExifData::const_iterator orig =
					pExifDataOriginal->findKey(Exiv2::ExifKey(src->key()));
				if (orig != pExifDataOriginal->end() &&
						orig->value().toString() == src->value().toString())
				{
					continue;
				}
			}

			/* An entry the destination already had keeps its own datatype; one
			 * being added takes the type it was written with.  operator[]
			 * cannot be asked - it hands back an "undefined" placeholder that
			 * reads nothing. */
			const Exiv2::ExifKey key(src->key());
			const bool bPresent = (dst.end() != dst.findKey(key));

			// operator[] creates the entry if it doesn't exist
			Exiv2::Exifdatum& datum = dst[src->key()];

			Exiv2::TypeId typeId = bPresent ? datum.typeId() : src->typeId();
			if (bOrientation)
			{
				typeId = Exiv2::unsignedShort;
			}

			Exiv2::Value::UniquePtr value = Exiv2::Value::create(typeId);
			if (0 != value->read(src->value().toString()))
			{
				continue;
			}
			datum.setValue(value.get());
		}

		image->writeMetadata();
	}
	catch (...)
	{
		rc = -1;
	}
	return rc;
}

static int save_jpeg_file(std::string filename, GdkTexture* texture,
		std::shared_ptr<Exiv2::ExifData> exifData,
		std::shared_ptr<const Exiv2::ExifData> exifDataOriginal,
		IImageSaver::ImageSaveProgressCallback callback, void* user_data)
{
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
	if (NULL == in)
	{
		g_object_unref(ginfile);
		g_object_unref(goutfile);
		return -1;
	}

	/* open outfile */
	out = G_OUTPUT_STREAM(g_file_replace(goutfile, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL));
	if (NULL == out)
	{
		g_object_unref(ginfile);
		g_object_unref(goutfile);
		g_object_unref(in);
		return -1;
	}

	struct jpeg_decompress_struct src;
	struct jpeg_compress_struct   dst;

	// set up the progress monitors
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

	src.err = jpeg_std_error(&jdsterr);
	jpeg_create_decompress(&src);
	jpeg_gio_src(&src, in);

	/* setup dst */
	dst.err = jpeg_std_error(&jdsterr);
	jpeg_create_compress(&dst);

	// hook up the progress monitors
	src.progress = &dcomp_progress.progress_mgr;
	dst.progress = &comp_progress.progress_mgr;

	jpeg_gio_dest(&dst, out);

	jvirt_barray_ptr* src_coef_arrays;

/* The source EXIF block rides through untouched; jpeg_apply_exif_metadata()
	 * edits it in place afterwards.  Rebuilding it here is what used to force
	 * big-endian onto every file and reshape tags nobody had touched. */
	jcopy_markers_setup(&src, JCOPYOPT_ALL);
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
		
	(void)perfect;

	src_coef_arrays = jpeg_read_coefficients(&src);

	if (NULL == texture)
	{
		jpeg_copy_critical_parameters(&src, &dst);
		jpeg_write_coefficients(&dst, src_coef_arrays);

		/* Copy to the output file any extra markers that we want to preserve */
		jcopy_markers_execute(&src, &dst, JCOPYOPT_ALL);
	}
	else
	{
		int width = gdk_texture_get_width(texture);
		int height = gdk_texture_get_height(texture);
		dst.image_width = width;
		dst.image_height = height;
		dst.input_components = 3;
		dst.in_color_space = JCS_RGB;

		jpeg_set_defaults(&dst);

		jpeg_start_compress(&dst, TRUE);

		jcopy_markers_execute(&src, &dst, JCOPYOPT_ALL);

		std::vector<guchar> rgba(width * height * 4);
		GdkTextureDownloader *dl = gdk_texture_downloader_new(texture);
		gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
		gdk_texture_downloader_download_into(dl, rgba.data(), width * 4);
		gdk_texture_downloader_free(dl);

		std::vector<JSAMPLE> row_buffer(width * 3);
		JSAMPROW row_pointer[1] = { row_buffer.data() };

		while (dst.next_scanline < dst.image_height)
		{
			const guchar* src_row = &rgba[dst.next_scanline * width * 4];
			for (int x = 0; x < width; ++x)
			{
				row_buffer[x * 3 + 0] = src_row[x * 4 + 0];
				row_buffer[x * 3 + 1] = src_row[x * 4 + 1];
				row_buffer[x * 3 + 2] = src_row[x * 4 + 2];
			}
			jpeg_write_scanlines(&dst, row_pointer, 1);
		}
	}


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

	/* Edit the EXIF in the finished file rather than the marker stream, so the
	 * block's own byte order and untouched tags survive verbatim. */
	if (0 != jpeg_apply_exif_metadata(strTmpFile.c_str(), exifData, exifDataOriginal))
	{
		rc = -1;
	}

	/* g_file_replace() created the temp with G_FILE_CREATE_PRIVATE (0600) and
	 * the move below applies the source file's permissions to the target, so an
	 * overwrite would quietly strip the original's mode.  Give the temp the
	 * original's mode first. */
	GFileInfo* info = g_file_query_info(ginfile,
			G_FILE_ATTRIBUTE_UNIX_MODE,
			G_FILE_QUERY_INFO_NONE,
			NULL,
			NULL);
	if (NULL != info)
	{
		if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_MODE))
		{
			g_chmod(strTmpFile.c_str(),
					g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE) & 07777);
		}
		g_object_unref(info);
	}

	// move tmp file to original file name
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

#if HAVE_GDK_PIXBUF
static int save_jpeg_file_pixbuf(std::string filename, GdkPixbuf* pixbuf,
		std::shared_ptr<Exiv2::ExifData> exifData,
		std::shared_ptr<const Exiv2::ExifData> exifDataOriginal,
		IImageSaver::ImageSaveProgressCallback callback, void* user_data)
{
	if (NULL != pixbuf)
	{
		GBytes* bytes = g_bytes_new(gdk_pixbuf_get_pixels(pixbuf),
			static_cast<gsize>(gdk_pixbuf_get_rowstride(pixbuf)) * gdk_pixbuf_get_height(pixbuf));
		GdkMemoryFormat format = gdk_pixbuf_get_has_alpha(pixbuf) ? GDK_MEMORY_R8G8B8A8 : GDK_MEMORY_R8G8B8;
		GdkTexture* tex = gdk_memory_texture_new(
			gdk_pixbuf_get_width(pixbuf),
			gdk_pixbuf_get_height(pixbuf),
			format,
			bytes,
			gdk_pixbuf_get_rowstride(pixbuf));
		g_bytes_unref(bytes);

		int rval = save_jpeg_file(filename, tex, exifData, exifDataOriginal, callback, user_data);
		g_object_unref(tex);
		return rval;
	}
	return save_jpeg_file(filename, static_cast<GdkTexture*>(NULL), exifData, exifDataOriginal, callback, user_data);
}
#endif


