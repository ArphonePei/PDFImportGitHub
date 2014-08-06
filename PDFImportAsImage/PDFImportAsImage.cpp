
#include "stdafx.h"

#include "fitz_CPP.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern "C" HWND adsw_acadMainWnd();

#define PATH_MAX 511

resbuf* getFileNameInput()
{
	BOOL ret = FALSE;
	resbuf* rbFileName = NULL;
	if(acedGetFileNavDialog(NULL, NULL, _T("pdf"), NULL, 0, &rbFileName) == RTNORM)
	{
		return rbFileName;
	}
	else
	{
		return NULL;
	}
}

void AcString2Utf8Char(AcString& str, char* utf8Name, int len=PATH_MAX);
void AcString2Utf8Char(AcString& str, char* utf8Name, int len)
{
	const wchar_t* wname = str.kwszPtr();
	WideCharToMultiByte(CP_UTF8, 0, wname, -1, utf8Name, len, NULL, NULL);
}

BOOL render(AcString& filename, int pagenumber, int zoom, int rotation, AcString& outputName)
{
	// Create a context to hold the exception stack and various caches.
	fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	BOOL ret = TRUE;
	fz_try(ctx)
	{
		// Open the PDF, XPS or CBZ document.
		char utf8Name[PATH_MAX];
		AcString2Utf8Char(filename, utf8Name);
		fz_document *doc = fz_open_document(ctx, utf8Name);

		// Retrieve the number of pages (not used in this example).
		int pagecount = fz_count_pages(doc);
		if (pagenumber > pagecount)
		{
			pagenumber = pagecount;
		}

		// Load the page we want. Page numbering starts from zero.
		fz_page *page = fz_load_page(doc, pagenumber - 1);

		// Calculate a transform to use when rendering. This transform
		// contains the scale and rotation. Convert zoom percentage to a
		// scaling factor. Without scaling the resolution is 72 dpi.
		fz_matrix transform;
		fz_rotate(&transform, rotation);
		fz_pre_scale(&transform, zoom / 100.0f, zoom / 100.0f);

		// Take the page bounds and transform them by the same matrix that
		// we will use to render the page.
		fz_rect bounds;
		fz_bound_page(doc, page, &bounds);
		fz_transform_rect(&bounds, &transform);

		// Create a blank pixmap to hold the result of rendering. The
		// pixmap bounds used here are the same as the transformed page
		// bounds, so it will contain the entire page. The page coordinate
		// space has the origin at the top left corner and the x axis
		// extends to the right and the y axis extends down.
		fz_irect bbox;
		fz_round_rect(&bbox, &bounds);
		fz_pixmap *pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), &bbox);
		fz_clear_pixmap_with_value(ctx, pix, 0xff);

		// A page consists of a series of objects (text, line art, images,
		// gradients). These objects are passed to a device when the
		// interpreter runs the page. There are several devices, used for
		// different purposes:
		//
		//	draw device -- renders objects to a target pixmap.
		//
		//	text device -- extracts the text in reading order with styling
		//	information. This text can be used to provide text search.
		//
		//	list device -- records the graphic objects in a list that can
		//	be played back through another device. This is useful if you
		//	need to run the same page through multiple devices, without
		//	the overhead of parsing the page each time.

		// Create a draw device with the pixmap as its target.
		// Run the page with the transform.
		fz_device *dev = fz_new_draw_device(ctx, pix);
		fz_run_page(doc, page, dev, &transform, NULL);
		fz_free_device(dev);

		outputName.format(_T("%s_%d.png"), filename.substr(filename.length()-4).kACharPtr(), pagenumber);
		ACHAR result[PATH_MAX];
		if (RTNORM != acedFindFile(outputName, result))
		{
			// Save the pixmap to a file.
			AcString2Utf8Char(outputName, utf8Name);
			fz_write_png(ctx, pix, utf8Name, 0);
		}

		// Clean up.
		fz_drop_pixmap(ctx, pix);
		fz_free_page(doc, page);
		fz_close_document(doc);
	}
	fz_catch(ctx)
	{
		ret = FALSE;
	}

	fz_free_context(ctx);
	return ret;
}

void pdfImport()
{
	Acad::ErrorStatus es = eOk;

	resbuf* rb = getFileNameInput();
	if (!rb)
	{
		return;
	}

	AcString fileName = rb->resval.rstring;
	acutRelRb(rb);

	int page = 0;
	acedInitGet(6, NULL);
	int rc = acedGetInt(_T("\nPlease input the page number <1>: "), &page);

	if (RTNONE == rc)
	{
		page = 1;
	}
	else if (RTNORM != rc)
	{
		return;
	}

	ads_point pt;
	if (RTNORM != acedGetPoint(NULL, _T("\nPlease pick an insertion point: "), pt))
	{
		return;
	}

	resbuf rbUcs, rbWcs;
	rbUcs.restype = rbWcs.restype = RTSHORT;
	rbUcs.resval.rint = 1;
	rbWcs.resval.rint = 0;
	acedTrans(pt, &rbUcs, &rbWcs, 0, pt);
	AcGePoint3d org = asPnt3d(pt);

	AcString imgName;
	if (!render(fileName, page, 200, 0, imgName))
	{
		return;
	}
	ACHAR szName[PATH_MAX];
	if (GetFileTitle(imgName.kwszPtr(), szName, PATH_MAX))
	{
		return;
	}

	AcDbDatabase *pDb = acdbHostApplicationServices()->workingDatabase();
	AcDbRasterImageDef* pImageDef = NULL;
	AcDbObjectId dictID = AcDbRasterImageDef::imageDictionary(pDb);
	if (dictID==AcDbObjectId::kNull)
	{
		es = AcDbRasterImageDef::createImageDictionary(pDb, dictID);
		if(es!= Acad::eOk)
		{
			ads_printf(_T("\nCould not create dictionary\n"));
			return;
		}
	}
	AcDbDictionary* pDict = NULL;
	es = acdbOpenObject((AcDbObject*&)pDict, dictID, AcDb::kForWrite);
	if(es != Acad::eOk)
	{
		ads_printf(_T("\nCould not open dictionary\n"));
		return;
	}

	AcDbObjectId objID;
	BOOL bExist = pDict->has(szName);
	if (bExist)
	{
		pDict->getAt(szName, objID);
		acdbOpenObject((AcDbObject*&)pImageDef, objID, AcDb::kForRead);
		if (!pImageDef)
		{
			return;
		}
	}
	else
	{
		pImageDef = new AcDbRasterImageDef();
		es = pImageDef->setSourceFileName(imgName);
		if(es != Acad::eOk)
		{
			delete pImageDef;
			return;
		}
		es = pImageDef->load();
		pDict->setAt(szName, pImageDef, objID);
	}

	// close Dictionary and Definition. 
	pDict->close();
	AcGeVector2d size = pImageDef->size();
	pImageDef->close();

	AcDbRasterImage* pImage = new AcDbRasterImage;
	es = pImage->setImageDefId(objID);
	if (es != Acad::eOk)
	{
		delete pImage;
		return;
	}

	AcDbBlockTable* pBTbl = NULL;
	pDb->getBlockTable(pBTbl, AcDb::kForWrite);
	if (pBTbl)
	{
		AcDbBlockTableRecord* pMS = NULL;
		pBTbl->getAt(ACDB_MODEL_SPACE, pMS,	AcDb::kForWrite);
		pBTbl->close();
		if (pMS)
		{
			es = pMS->appendAcDbEntity(pImage);
			pMS->close();
		}
	}

	AcDbObjectId entID = pImage->objectId();
	AcGePoint3d TempPoint3d(size.x, 0, 0);
	AcGeVector3d LowerRightVector = TempPoint3d.asVector();
	AcGePoint3d TempPoint3d2(0, size.y, 0);
	AcGeVector3d OnPlaneVector = TempPoint3d2.asVector();
	if (pImage->setOrientation(org, LowerRightVector, OnPlaneVector) !=Adesk::kTrue)
	{
		ads_printf(_T("\nSet Orientation failed."));
		pImage->close();
		return;
	}

	pImage->setDisplayOpt(AcDbRasterImage::kShow, Adesk::kTrue);
	pImage->setDisplayOpt(AcDbRasterImage::kTransparent, Adesk::kTrue);

	AcDbObjectPointer<AcDbRasterImageDefReactor> rasterImageDefReactor;

	// new it 
	rasterImageDefReactor.create();

	// Set the entity to be its owner. 
	es = rasterImageDefReactor->setOwnerId(pImage->objectId());

	// if ok 
	if (es == Acad::eOk)
	{
		AcDbObjectId defReactorId;
		// assign the object an objectId
		es = pDb->addAcDbObject(defReactorId, rasterImageDefReactor.object());

		// if ok
		if (es == Acad::eOk)
		{
			// set the image reactor id
			pImage->setReactorId(defReactorId);

			AcDbObjectPointer<AcDbRasterImageDef> rasterImagedef(pImage->imageDefId(), AcDb::kForWrite);

			// if ok
			if (rasterImagedef.openStatus() == Acad::eOk)
			{
				rasterImagedef->addPersistentReactor(defReactorId);
			}
		}
	}

	pImage->close();
}

void initApp()
{ 
	acedRegCmds->addCommand(_T("ZOSP"), _T("PdfImport"), _T("PdfImport"), ACRX_CMD_MODAL, pdfImport);
}

void unloadApp()
{ 
	acedRegCmds->removeGroup(_T("ZOSP"));  
}

//////////////////////////////////////////////////////////////
//
// Entry points
//
//////////////////////////////////////////////////////////////
extern "C" AcRx::AppRetCode zcrxEntryPoint( AcRx::AppMsgCode msg, void* appId)
{
	switch( msg ) 
	{
	case AcRx::kInitAppMsg: 
		acrxDynamicLinker->unlockApplication(appId);
		acrxDynamicLinker->registerAppMDIAware(appId);
		initApp(); 
		break;
	case AcRx::kUnloadAppMsg: 
		unloadApp(); 
		break;
	case AcRx::kInitDialogMsg:

		break;
	default:
		break;
	}
	return AcRx::kRetOK;
}