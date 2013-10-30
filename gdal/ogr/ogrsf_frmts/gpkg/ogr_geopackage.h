/******************************************************************************
 * $Id$
 *
 * Project:  GeoPackage Translator
 * Purpose:  Definition of classes for OGR GeoPackage driver.
 * Author:   Paul Ramsey, pramsey@boundlessgeo.com
 *
 ******************************************************************************
 * Copyright (c) 2013, Paul Ramsey <pramsey@boundlessgeo.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#ifndef _OGR_GEOPACKAGE_H_INCLUDED
#define _OGR_GEOPACKAGE_H_INCLUDED

#include "ogrsf_frmts.h"
#include "sqlite3.h"

/************************************************************************/
/*                           OGRGeoPackageDataSource                       */
/************************************************************************/

class OGRGeoPackageDataSource : public OGRDataSource
{
    char*               m_pszName;
    OGRLayer**          m_papoLayers;
    int                 m_nLayers;
    int                 m_bUpdate;
    sqlite3*            m_poDb;
    
    
/*
    int                 bReadWrite;
    int                 bUseHTTPS;
    int                 bMustCleanPersistant;
    int                 FetchSRSId( OGRSpatialReference * poSRS );
*/

    public:
                        OGRGeoPackageDataSource();
                        ~OGRGeoPackageDataSource();

    virtual const char* GetName() { return m_pszName; }
    virtual int         GetLayerCount() { return m_nLayers; }
    virtual OGRLayer*   GetLayer( int );
    virtual int         TestCapability( const char * );

    int                 Open( const char * pszFilename, int bUpdate );
    int                 Create( const char * pszFilename, char **papszOptions );

/*

    virtual OGRLayer*   GetLayer( int );
    virtual OGRLayer    *GetLayerByName(const char *);


    virtual OGRLayer   *CreateLayer( const char *pszName,
                                     OGRSpatialReference *poSpatialRef = NULL,
                                     OGRwkbGeometryType eGType = wkbUnknown,
                                     char ** papszOptions = NULL );

    virtual OGRErr      DeleteLayer(int);

    virtual OGRLayer*   ExecuteSQL( const char *pszSQLCommand,
                                    OGRGeometry *poSpatialFilter,
                                    const char *pszDialect );
                                    
    virtual void        ReleaseResultSet( OGRLayer * poLayer );

    int                 IsReadWrite() const { return bReadWrite; }
*/
};


/************************************************************************/
/*                           OGRGeoPackageDriver                        */
/************************************************************************/

class OGRGeoPackageDriver : public OGRSFDriver
{
    public:
        OGRGeoPackageDriver();
        ~OGRGeoPackageDriver();
        virtual const char*         GetName();
        virtual OGRDataSource*      Open( const char *, int );
		virtual OGRDataSource*      CreateDatasource( const char * pszFilename, char **papszOptions );
		virtual OGRErr              DeleteDataSource( const char * pszFilename );
        virtual int                 TestCapability( const char * ) { return FALSE; }
};




#endif /* _OGR_GEOPACKAGE_H_INCLUDED */