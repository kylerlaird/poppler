/* poppler-document.cc: glib wrapper for poppler
 * Copyright (C) 2005, Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <goo/GooList.h>
#include <splash/SplashBitmap.h>
#include <GlobalParams.h>
#include <PDFDoc.h>
#include <Outline.h>
#include <ErrorCodes.h>
#include <UnicodeMap.h>
#include <GfxState.h>
#include <SplashOutputDev.h>
#include <Stream.h>
#include <FontInfo.h>
#include <PDFDocEncoding.h>

#include "poppler.h"
#include "poppler-private.h"
#include "poppler-enums.h"

enum {
	PROP_0,
	PROP_TITLE,
	PROP_FORMAT,
	PROP_AUTHOR,
	PROP_SUBJECT,
	PROP_KEYWORDS,
	PROP_CREATOR,
	PROP_PRODUCER,
	PROP_CREATION_DATE,
	PROP_MOD_DATE,
	PROP_LINEARIZED,
	PROP_PAGE_LAYOUT,
	PROP_PAGE_MODE,
	PROP_VIEWER_PREFERENCES,
	PROP_PERMISSIONS,
};

typedef struct _PopplerDocumentClass PopplerDocumentClass;
struct _PopplerDocumentClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (PopplerDocument, poppler_document, G_TYPE_OBJECT);

PopplerDocument *
poppler_document_new_from_file (const char  *uri,
				const char  *password,
				GError     **error)
{
  PopplerDocument *document;
  PDFDoc *newDoc;
  GooString *filename_g;
  GooString *password_g;
  int err;
  char *filename;

  document = (PopplerDocument *) g_object_new (POPPLER_TYPE_DOCUMENT, NULL);
  
  if (!globalParams) {
    globalParams = new GlobalParams("/etc/xpdfrc");
    globalParams->setupBaseFontsFc(NULL);
  }

  filename = g_filename_from_uri (uri, NULL, error);
  if (!filename)
    return NULL;

  filename_g = new GooString (filename);
  g_free (filename);

  password_g = NULL;
  if (password != NULL)
    password_g = new GooString (password);

  newDoc = new PDFDoc(filename_g, password_g, password_g);
  if (password_g)
    delete password_g;

  if (!newDoc->isOk()) {
    err = newDoc->getErrorCode();
    delete newDoc;
    if (err == errEncrypted) {
      g_set_error (error, POPPLER_ERROR,
		   POPPLER_ERROR_ENCRYPTED,
		   "Document is encrypted.");
    } else {
      g_set_error (error, G_FILE_ERROR,
		   G_FILE_ERROR_FAILED,
		   "Failed to load document (error %d) '%s'\n",
		   err,
		   uri);
    }

    return NULL;
  }

  document->doc = newDoc;

#if defined (HAVE_CAIRO)
  document->output_dev = new CairoOutputDev ();
#elif defined (HAVE_SPLASH)
  SplashColor white;
  white.rgb8 = splashMakeRGB8 (0xff, 0xff, 0xff);
  document->output_dev = new SplashOutputDev(splashModeRGB8, gFalse, white);
#endif
  document->output_dev->startDoc(document->doc->getXRef ());

  return document;
}

gboolean
poppler_document_save (PopplerDocument  *document,
		       const char       *uri,
		       GError          **error)
{
  char *filename;
  gboolean retval = FALSE;

  g_return_val_if_fail (POPPLER_IS_DOCUMENT (document), FALSE);

  filename = g_filename_from_uri (uri, NULL, error);
  if (filename != NULL) {
    GooString *fname = new GooString (filename);

    retval = document->doc->saveAs (fname);
  }

  return retval;
}

static void
poppler_document_finalize (GObject *object)
{
  PopplerDocument *document = POPPLER_DOCUMENT (object);

  delete document->output_dev;
  delete document->doc;
}

int
poppler_document_get_n_pages (PopplerDocument *document)
{
  g_return_val_if_fail (POPPLER_IS_DOCUMENT (document), 0);

  return document->doc->getNumPages();
}

PopplerPage *
poppler_document_get_page (PopplerDocument  *document,
			   int               index)
{
  Catalog *catalog;
  Page *page;

  g_return_val_if_fail (0 <= index &&
			index < poppler_document_get_n_pages (document),
			NULL);

  catalog = document->doc->getCatalog();
  page = catalog->getPage (index + 1);

  return _poppler_page_new (document, page, index);
}

PopplerPage *
poppler_document_get_page_by_label (PopplerDocument  *document,
				    const char       *label)
{
  Catalog *catalog;
  GooString label_g(label);
  int index;

  catalog = document->doc->getCatalog();
  if (!catalog->labelToIndex (&label_g, &index))
    return NULL;

  return poppler_document_get_page (document, index);
}

static gboolean
has_unicode_marker (GooString *string)
{
  return ((string->getChar (0) & 0xff) == 0xfe &&
	  (string->getChar (1) & 0xff) == 0xff);
}

static void
info_dict_get_string (Dict *info_dict, const gchar *key, GValue *value)
{
  Object obj;
  GooString *goo_value;
  gchar *result;

  if (!info_dict->lookup ((gchar *)key, &obj)->isString ()) {
    obj.free ();
    return;
  }

  goo_value = obj.getString ();

  if (has_unicode_marker (goo_value)) {
    result = g_convert (goo_value->getCString () + 2,
			goo_value->getLength () - 2,
			"UTF-8", "UTF-16BE", NULL, NULL, NULL);
  } else {
    int len;
    gunichar *ucs4_temp;
    int i;
    
    len = goo_value->getLength ();
    ucs4_temp = g_new (gunichar, len + 1);
    for (i = 0; i < len; ++i) {
      ucs4_temp[i] = pdfDocEncoding[(unsigned char)goo_value->getChar(i)];
    }
    ucs4_temp[i] = 0;

    result = g_ucs4_to_utf8 (ucs4_temp, -1, NULL, NULL, NULL);

    g_free (ucs4_temp);
  }

  obj.free ();

  g_value_set_string (value, result);

  g_free (result);
}

static void
info_dict_get_date (Dict *info_dict, const gchar *key, GValue *value) 
{
  Object obj;
  GooString *goo_value;
  int year, mon, day, hour, min, sec;
  int scanned_items;
  struct tm *time;
  gchar *date_string;
  GTime result;

  if (!info_dict->lookup ((gchar *)key, &obj)->isString ()) {
    obj.free ();
    return;
  }

  goo_value = obj.getString (); 

  if (has_unicode_marker (goo_value)) {
    date_string = g_convert (goo_value->getCString () + 2,
			goo_value->getLength () - 2,
			"UTF-8", "UTF-16BE", NULL, NULL, NULL);		
  } else {
    date_string = g_strndup (goo_value->getCString (), goo_value->getLength ());
  }

  /* See PDF Reference 1.3, Section 3.8.2 for PDF Date representation */

  if (date_string [0] == 'D' && date_string [1] == ':')
		date_string += 2;
	
  /* FIXME only year is mandatory; parse optional timezone offset */
  scanned_items = sscanf (date_string, "%4d%2d%2d%2d%2d%2d",
		&year, &mon, &day, &hour, &min, &sec);

  if (scanned_items != 6)
    return;

  /* Workaround for y2k bug in Distiller 3, hoping that it won't
   * be used after y2.2k */
  if (year < 1930 && strlen (date_string) > 14) {
    int century, years_since_1900;
    scanned_items = sscanf (date_string, "%2d%3d%2d%2d%2d%2d%2d",
		&century, &years_since_1900, &mon, &day, &hour, &min, &sec);
						
    if (scanned_items != 7)
      return;
	
    year = century * 100 + years_since_1900;
  }

  time = g_new0 (struct tm, 1);
	
  time->tm_year = year - 1900;
  time->tm_mon = mon - 1;
  time->tm_mday = day;
  time->tm_hour = hour;
  time->tm_min = min;
  time->tm_sec = sec;
  time->tm_wday = -1;
  time->tm_yday = -1;
  time->tm_isdst = -1; /* 0 = DST off, 1 = DST on, -1 = don't know */
 
  /* compute tm_wday and tm_yday and check date */
  if (mktime (time) == (time_t) - 1) {
    return;
  } else {
  	result = mktime (time);
  }       
    
  obj.free ();
  
  g_value_set_int (value, result);
}

static PopplerPageLayout
convert_page_layout (Catalog::PageLayout pageLayout)
{
  switch (pageLayout)
    {
    case Catalog::pageLayoutSinglePage:
      return POPPLER_PAGE_LAYOUT_SINGLE_PAGE;
    case Catalog::pageLayoutOneColumn:
      return POPPLER_PAGE_LAYOUT_ONE_COLUMN;
    case Catalog::pageLayoutTwoColumnLeft:
      return POPPLER_PAGE_LAYOUT_TWO_COLUMN_LEFT;
    case Catalog::pageLayoutTwoColumnRight:
      return POPPLER_PAGE_LAYOUT_TWO_COLUMN_RIGHT;
    case Catalog::pageLayoutTwoPageLeft:
      return POPPLER_PAGE_LAYOUT_TWO_PAGE_LEFT;
    case Catalog::pageLayoutTwoPageRight:
      return POPPLER_PAGE_LAYOUT_TWO_PAGE_RIGHT;
    case Catalog::pageLayoutNone:
    default:
      return POPPLER_PAGE_LAYOUT_UNSET;
    }
}

static PopplerPageMode
convert_page_mode (Catalog::PageMode pageMode)
{
  switch (pageMode)
    {
    case Catalog::pageModeOutlines:
      return POPPLER_PAGE_MODE_USE_OUTLINES;
    case Catalog::pageModeThumbs:
      return POPPLER_PAGE_MODE_USE_THUMBS;
    case Catalog::pageModeFullScreen:
      return POPPLER_PAGE_MODE_FULL_SCREEN;
    case Catalog::pageModeOC:
      return POPPLER_PAGE_MODE_USE_OC;
    case Catalog::pageModeAttach:
      return POPPLER_PAGE_MODE_USE_ATTACHMENTS;
    case Catalog::pageModeNone:
    default:
      return POPPLER_PAGE_MODE_UNSET;
    }
}

static void
poppler_document_get_property (GObject    *object,
			       guint       prop_id,
			       GValue     *value,
			       GParamSpec *pspec)
{
  PopplerDocument *document = POPPLER_DOCUMENT (object);
  Object obj;
  Catalog *catalog;
  gchar *str;
  guint flag;

  switch (prop_id)
    {
    case PROP_TITLE:
      document->doc->getDocInfo (&obj);
      if (obj.isDict ())
	info_dict_get_string (obj.getDict(), "Title", value);
      break;
    case PROP_FORMAT:
      str = g_strndup("PDF-", 15); /* allocates 16 chars, pads with \0s */
      g_ascii_formatd (str + 4, 15 + 1 - 4,
		       "%.2g", document->doc->getPDFVersion ());
      g_value_take_string (value, str);
      break;
    case PROP_AUTHOR:
      document->doc->getDocInfo (&obj);
      if (obj.isDict ())
	info_dict_get_string (obj.getDict(), "Author", value);
      break;
    case PROP_SUBJECT:
      document->doc->getDocInfo (&obj);
      if (obj.isDict ())
	info_dict_get_string (obj.getDict(), "Subject", value);
      break;
    case PROP_KEYWORDS:
      document->doc->getDocInfo (&obj);
      if (obj.isDict ())
	info_dict_get_string (obj.getDict(), "Keywords", value);
      break;
    case PROP_CREATOR:
      document->doc->getDocInfo (&obj);
      if (obj.isDict ())
	info_dict_get_string (obj.getDict(), "Creator", value);
      break;
    case PROP_PRODUCER:
      document->doc->getDocInfo (&obj);
      if (obj.isDict ())
	info_dict_get_string (obj.getDict(), "Producer", value);
      break;
    case PROP_CREATION_DATE:
      document->doc->getDocInfo (&obj);
      if (obj.isDict ())
	info_dict_get_date (obj.getDict(), "CreationDate", value);
      break;
    case PROP_MOD_DATE:
      document->doc->getDocInfo (&obj);
      if (obj.isDict ())
	info_dict_get_date (obj.getDict(), "ModDate", value);
	break;
    case PROP_LINEARIZED:
      if (document->doc->isLinearized ()) {	
	  g_value_set_string (value, "Yes");
      }	else {
	  g_value_set_string (value, "No");
      }
      break;
    case PROP_PAGE_LAYOUT:
      catalog = document->doc->getCatalog ();
      if (catalog && catalog->isOk ())
	{
	  PopplerPageLayout page_layout = convert_page_layout (catalog->getPageLayout ());
	  g_value_set_enum (value, page_layout);
	}
      break;
    case PROP_PAGE_MODE:
      catalog = document->doc->getCatalog ();
      if (catalog && catalog->isOk ())
	{
	  PopplerPageMode page_mode = convert_page_mode (catalog->getPageMode ());
	  g_value_set_enum (value, page_mode);
	}
      break;
    case PROP_VIEWER_PREFERENCES:
      /* FIXME: write... */
      g_value_set_flags (value, POPPLER_VIEWER_PREFERENCES_UNSET);
      break;
    case PROP_PERMISSIONS:
      flag = 0;
      if (document->doc->okToPrint ())
	flag |= POPPLER_PERMISSIONS_OK_TO_PRINT;
      if (document->doc->okToChange ())
	flag |= POPPLER_PERMISSIONS_OK_TO_MODIFY;
      if (document->doc->okToCopy ())
	flag |= POPPLER_PERMISSIONS_OK_TO_COPY;
      if (document->doc->okToAddNotes ())
	flag |= POPPLER_PERMISSIONS_OK_TO_ADD_NOTES;
      g_value_set_flags (value, flag);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
poppler_document_class_init (PopplerDocumentClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = poppler_document_finalize;
  gobject_class->get_property = poppler_document_get_property;

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_TITLE,
	   g_param_spec_string ("title",
				"Document Title",
				"The title of the document",
				NULL,
				G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_FORMAT,
	   g_param_spec_string ("format",
				"PDF Format",
				"The PDF version of the document",
				NULL,
				G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_AUTHOR,
	   g_param_spec_string ("author",
				"Author",
				"The author of the document",
				NULL,
				G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_SUBJECT,
	   g_param_spec_string ("subject",
				"Subject",
				"Subjects the document touches",
				NULL,
				G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_KEYWORDS,
	   g_param_spec_string ("keywords",
				"Keywords",
				"Keywords",
				NULL,
				G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_CREATOR,
	   g_param_spec_string ("creator",
				"Creator",
				"The software that created the document",
				NULL,
				G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	  PROP_PRODUCER,
	   g_param_spec_string ("producer",
				"Producer",
				"The software that converted the document",
				NULL,
				G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_CREATION_DATE,
	   g_param_spec_int ("creation-date",
				"Creation Date",
				"The date and time the document was created",
				0, G_MAXINT, 0,
				G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_MOD_DATE,
	   g_param_spec_int ("mod-date",
				"Modification Date",
				"The date and time the document was modified",
				0, G_MAXINT, 0,
				G_PARAM_READABLE));
				
  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_LINEARIZED,
	   g_param_spec_string ("linearized",
				"Fast Web View Enabled",
				"Is the document optimized for web viewing?",
				NULL,
				G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_PAGE_LAYOUT,
	   g_param_spec_enum ("page-layout",
			      "Page Layout",
			      "Initial Page Layout",
			      POPPLER_TYPE_PAGE_LAYOUT,
			      POPPLER_PAGE_LAYOUT_UNSET,
			      G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_PAGE_MODE,
	   g_param_spec_enum ("page-mode",
			      "Page Mode",
			      "Page Mode",
			      POPPLER_TYPE_PAGE_MODE,
			      POPPLER_PAGE_MODE_UNSET,
			      G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_VIEWER_PREFERENCES,
	   g_param_spec_flags ("viewer-preferences",
			       "Viewer Preferences",
			       "Viewer Preferences",
			       POPPLER_TYPE_VIEWER_PREFERENCES,
			       POPPLER_VIEWER_PREFERENCES_UNSET,
			       G_PARAM_READABLE));

  g_object_class_install_property
	  (G_OBJECT_CLASS (klass),
	   PROP_PERMISSIONS,
	   g_param_spec_flags ("permissions",
			       "Permissions",
			       "Permissions",
			       POPPLER_TYPE_PERMISSIONS,
			       POPPLER_PERMISSIONS_FULL,
			       G_PARAM_READABLE));
}

static void
poppler_document_init (PopplerDocument *document)
{
}

/* PopplerIndexIter: For determining the index of a tree */
struct _PopplerIndexIter
{
	PopplerDocument *document;
	GooList *items;
	int index;
};


GType
poppler_index_iter_get_type (void)
{
  static GType our_type = 0;

  if (our_type == 0)
    our_type = g_boxed_type_register_static ("PopplerIndexIter",
					     (GBoxedCopyFunc) poppler_index_iter_copy,
					     (GBoxedFreeFunc) poppler_index_iter_free);

  return our_type;
}

PopplerIndexIter *
poppler_index_iter_copy (PopplerIndexIter *iter)
{
	PopplerIndexIter *new_iter;

	g_return_val_if_fail (iter != NULL, NULL);

	new_iter = g_new0 (PopplerIndexIter, 1);
	*new_iter = *iter;
	new_iter->document = (PopplerDocument *) g_object_ref (new_iter->document);

	return new_iter;
}

PopplerIndexIter *
poppler_index_iter_new (PopplerDocument *document)
{
	PopplerIndexIter *iter;
	Outline *outline;
	GooList *items;

	outline = document->doc->getOutline();
	if (outline == NULL)
		return NULL;

	items = outline->getItems();
	if (items == NULL)
		return NULL;

	iter = g_new0 (PopplerIndexIter, 1);
	iter->document = (PopplerDocument *) g_object_ref (document);
	iter->items = items;
	iter->index = 0;

	return iter;
}

PopplerIndexIter *
poppler_index_iter_get_child (PopplerIndexIter *parent)
{
	PopplerIndexIter *child;
	OutlineItem *item;

	g_return_val_if_fail (parent != NULL, NULL);
	
	item = (OutlineItem *)parent->items->get (parent->index);
	item->open ();
	if (! (item->hasKids() && item->getKids()) )
		return NULL;

	child = g_new0 (PopplerIndexIter, 1);
	child->document = (PopplerDocument *)g_object_ref (parent->document);
	child->items = item->getKids ();

	g_assert (child->items);

	return child;
}

static gchar *
unicode_to_char (Unicode *unicode,
		 int      len)
{
	static UnicodeMap *uMap = NULL;
	if (uMap == NULL) {
		GooString *enc = new GooString("UTF-8");
		uMap = globalParams->getUnicodeMap(enc);
		uMap->incRefCnt ();
		delete enc;
	}
		
	GooString gstr;
	gchar buf[8]; /* 8 is enough for mapping an unicode char to a string */
	int i, n;

	for (i = 0; i < len; ++i) {
		n = uMap->mapUnicode(unicode[i], buf, sizeof(buf));
		gstr.append(buf, n);
	}

	return g_strdup (gstr.getCString ());
}

gboolean
poppler_index_iter_is_open (PopplerIndexIter *iter)
{
	OutlineItem *item;

	item = (OutlineItem *)iter->items->get (iter->index);

	return item->isOpen();
}

PopplerAction *
poppler_index_iter_get_action (PopplerIndexIter  *iter)
{
	OutlineItem *item;
	LinkAction *link_action;
	PopplerAction *action;
	gchar *title;

	g_return_val_if_fail (iter != NULL, NULL);

	item = (OutlineItem *)iter->items->get (iter->index);
	link_action = item->getAction ();

	title = unicode_to_char (item->getTitle(),
				 item->getTitleLength ());

	action = _poppler_action_new (iter->document, link_action, title);
	g_free (title);

	return action;
}

gboolean
poppler_index_iter_next (PopplerIndexIter *iter)
{
	g_return_val_if_fail (iter != NULL, FALSE);

	iter->index++;
	if (iter->index >= iter->items->getLength())
		return FALSE;

	return TRUE;
}

void
poppler_index_iter_free (PopplerIndexIter *iter)
{
	if (iter == NULL)
		return;

	g_object_unref (iter->document);
//	delete iter->items;
	g_free (iter);
	
}

struct _PopplerFontsIter
{
	GooList *items;
	int index;
};

GType
poppler_fonts_iter_get_type (void)
{
  static GType our_type = 0;

  if (our_type == 0)
    our_type = g_boxed_type_register_static ("PopplerFontsIter",
					     (GBoxedCopyFunc) poppler_fonts_iter_copy,
					     (GBoxedFreeFunc) poppler_fonts_iter_free);

  return our_type;
}

const char *
poppler_fonts_iter_get_name (PopplerFontsIter *iter)
{
	GooString *name;
	FontInfo *info;

	info = (FontInfo *)iter->items->get (iter->index);

	name = info->getName();
	if (name != NULL) {
		return info->getName()->getCString();
	} else {
		return NULL;
	}
}

gboolean
poppler_fonts_iter_next (PopplerFontsIter *iter)
{
	g_return_val_if_fail (iter != NULL, FALSE);

	iter->index++;
	if (iter->index >= iter->items->getLength())
		return FALSE;

	return TRUE;
}

PopplerFontsIter *
poppler_fonts_iter_copy (PopplerFontsIter *iter)
{
	PopplerFontsIter *new_iter;

	g_return_val_if_fail (iter != NULL, NULL);

	new_iter = g_new0 (PopplerFontsIter, 1);
	*new_iter = *iter;

	new_iter->items = new GooList ();
	for (int i = 0; i < iter->items->getLength(); i++) {
		FontInfo *info = (FontInfo *)iter->items->get(i);
		new_iter->items->append (new FontInfo (*info));
	}

	return new_iter;
}

void
poppler_fonts_iter_free (PopplerFontsIter *iter)
{
	if (iter == NULL)
		return;

	deleteGooList (iter->items, FontInfo);

	g_free (iter);
}

static PopplerFontsIter *
poppler_fonts_iter_new (GooList *items)
{
	PopplerFontsIter *iter;

	iter = g_new0 (PopplerFontsIter, 1);
	iter->items = items;
	iter->index = 0;

	return iter;
}

PopplerFontInfo *
poppler_font_info_new (PopplerDocument *document)
{
	PopplerFontInfo *font_info;

	g_return_val_if_fail (POPPLER_IS_DOCUMENT (document), NULL);

	font_info = g_new0 (PopplerFontInfo, 1);
	font_info->document = (PopplerDocument *) g_object_ref (document);
	font_info->scanner = new FontInfoScanner(document->doc);

	return font_info;
}

gboolean
poppler_font_info_scan (PopplerFontInfo   *font_info,
			int                n_pages,
			PopplerFontsIter **iter)
{
	GooList *items;

	g_return_val_if_fail (iter != NULL, FALSE);

	items = font_info->scanner->scan(n_pages);

	if (items == NULL) {
		*iter = NULL;
	} else if (items->getLength() == 0) {
		*iter = NULL;
		delete items;
	} else {
		*iter = poppler_fonts_iter_new(items);
	}
	
	return (items != NULL);
}

void
poppler_font_info_free (PopplerFontInfo *font_info)
{
	g_return_if_fail (font_info != NULL);

	delete font_info->scanner;

	g_object_unref (font_info->document);
}

/**
 * poppler_ps_file_new:
 * @document: a #PopplerDocument
 * @filename: the path of the output filename
 * @first_page: the first page to print
 * @n_pages: the number of pages to print
 * 
 * Create a new postscript file to render to
 * 
 * Return value: a PopplerPSFile 
 **/
PopplerPSFile *
poppler_ps_file_new (PopplerDocument *document, const char *filename,
		     int first_page, int n_pages)
{
	PopplerPSFile *ps_file;

	g_return_val_if_fail (POPPLER_IS_DOCUMENT (document), NULL);
	g_return_val_if_fail (filename != NULL, NULL);
	g_return_val_if_fail (n_pages > 0, NULL);

	ps_file = g_new0 (PopplerPSFile, 1);
	ps_file->document = (PopplerDocument *) g_object_ref (document);
	ps_file->out = new PSOutputDev ((char *)filename,
					document->doc->getXRef(),
					document->doc->getCatalog(),
					first_page + 1,
					first_page + 1 + n_pages - 1,
					psModePS);

	return ps_file;
}

/**
 * poppler_ps_file_free:
 * @ps_file: a PopplerPSFile
 * 
 * Free a PopplerPSFile
 * 
 **/
void
poppler_ps_file_free (PopplerPSFile *ps_file)
{
	g_return_if_fail (ps_file != NULL);

	delete ps_file->out;
	g_object_unref (ps_file->document);
	g_free (ps_file);
}


