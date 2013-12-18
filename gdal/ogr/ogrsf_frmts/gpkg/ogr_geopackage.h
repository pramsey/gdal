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

/* 1.1.1: A GeoPackage SHALL contain 0x47503130 ("GP10" in ASCII) in the application id */
/* http://opengis.github.io/geopackage/#_file_format */
/* 0x47503130 = 1196437808 */
#define GPKG_APPLICATION_ID 1196437808

#define UNDEFINED_SRID 0



  

/************************************************************************/
/*                           OGRGeoPackageDriver                        */
/************************************************************************/

class OGRGeoPackageDriver : public OGRSFDriver
{
    public:
                            ~OGRGeoPackageDriver();
        const char*         GetName();
        OGRDataSource*      Open( const char *, int );
        OGRDataSource*      CreateDataSource( const char * pszFilename, char **papszOptions );
        OGRErr              DeleteDataSource( const char * pszFilename );
        int                 TestCapability( const char * );
};


/************************************************************************/
/*                           OGRGeoPackageDataSource                    */
/************************************************************************/

class OGRGeoPackageDataSource : public OGRDataSource
{
    char*               m_pszFileName;
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

        virtual const char* GetName() { return m_pszFileName; }
        virtual int         GetLayerCount() { return m_nLayers; }
        int                 Open( const char * pszFilename, int bUpdate );
        int                 Create( const char * pszFilename, char **papszOptions );
        OGRLayer*           GetLayer( int iLayer );
        int                 DeleteLayer( int iLayer );
        OGRLayer*           CreateLayer( const char * pszLayerName,
                                         OGRSpatialReference * poSpatialRef,
                                         OGRwkbGeometryType eGType,
                                         char **papszOptions );
        int                 TestCapability( const char * );
        
        virtual int         IsReadOnly() { return m_bUpdate; }
        int                 GetSrsId( const OGRSpatialReference * poSRS );
        OGRSpatialReference* GetSpatialRef( int iSrsId );
        sqlite3*            GetDatabaseHandle();
        OGRErr              AddColumn( const char * pszTableName, 
                                       const char * pszColumnName, 
                                       const char * pszColumnType );

    private:
    
        OGRErr              PragmaCheck(const char * pszPragma, const char * pszExpected, int nRowsExpected);
        bool                CheckApplicationId(const char * pszFileName);
        OGRErr              SetApplicationId();

    
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
/*                           OGRGeoPackageLayer                         */
/************************************************************************/

class OGRGeoPackageLayer : public OGRLayer
{
    char*                       m_pszTableName;
    char*                       m_pszFidColumn;
    int                         m_iSrs;
    OGRGeoPackageDataSource*    m_poDS;
    OGREnvelope*                m_poExtent;
    CPLString                   m_soColumns;
    CPLString                   m_soFilter;
    OGRBoolean                  m_bExtentChanged;
    OGRFeatureDefn*             m_poFeatureDefn;
    sqlite3_stmt*               m_poQueryStatement;
    
    
    public:
    
                        OGRGeoPackageLayer( OGRGeoPackageDataSource *poDS,
                                            const char * pszTableName );
                        ~OGRGeoPackageLayer();

    /* OGR API methods */
    OGRFeatureDefn*     GetLayerDefn() { return m_poFeatureDefn; }
    int                 TestCapability( const char * );
    OGRErr              CreateField( OGRFieldDefn *poField, int bApproxOK = TRUE );
    void                ResetReading();
	OGRErr				CreateFeature( OGRFeature *poFeater );
    OGRErr              SetAttributeFilter( const char *pszQuery );
    OGRFeature*         GetNextFeature();
    // void                SetSpatialFilter( int iGeomField, OGRGeometry * poGeomIn );


    /* GPKG methods */
    OGRErr              ReadFeature( sqlite3_stmt *poQuery, OGRFeature **ppoFeature );
    OGRErr              ReadTableDefinition();
    OGRErr              UpdateExtent( const OGREnvelope *poExtent );
    OGRErr              SaveExtent();
    OGRErr              BuildColumns();


/*    
    virtual OGRErr      SetFeature( OGRFeature *poFeature );



    virtual OGRFeature *GetFeature( long nFeatureId );
    virtual int         GetFeatureCount( int );

    virtual void        SetSpatialFilter( OGRGeometry *poGeom ) { SetSpatialFilter(0, poGeom); }
    virtual void        SetSpatialFilter( int iGeomField, OGRGeometry *poGeom );

    virtual OGRErr      SetAttributeFilter( const char * );

    virtual OGRErr      DeleteFeature( long nFID );
    virtual OGRErr      CreateFeature( OGRFeature *poFeature );

                                     int bApproxOK = TRUE );
    virtual OGRErr      CreateGeomField( OGRGeomFieldDefn *poGeomField,
    virtual OGRErr      DeleteField( int iField );
    virtual OGRErr      AlterFieldDefn( int iField, OGRFieldDefn* poNewFieldDefn, int nFlags );
*/

};



#endif /* _OGR_GEOPACKAGE_H_INCLUDED */