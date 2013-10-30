/******************************************************************************
 * $Id$
 *
 * Project:  GeoPackage Translator
 * Purpose:  Implements OGRGeoPackageDataSource class
 * Author:   Paul Ramsey <pramsey@boundlessgeo.com>
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

#include "ogr_geopackage.h"

CPL_CVSID("$Id$");

/************************************************************************/
/*                        OGRGeoPackageDataSource()                     */
/************************************************************************/

OGRGeoPackageDataSource::OGRGeoPackageDataSource()
{
    m_pszName = NULL;
    m_papoLayers = NULL;
    m_nLayers = 0;
    m_poDB = NULL;
}

/************************************************************************/
/*                       ~OGRGeoPackageDataSource()                     */
/************************************************************************/

OGRGeoPackageDataSource::~OGRGeoPackageDataSource()
{
    for( int i = 0; i < m_nLayers; i++ )
        delete m_papoLayers[i];
        
    if ( m_poDB )
        sqlite3_close(m_poDB);
        
    CPLFree( m_papoLayers );
    CPLFree( m_pszName );
}

/************************************************************************/
/*                       OpenOrCreate()                                 */
/* Utility to open and populate member variables                        */
/************************************************************************/

static int OpenOrCreate(const char * pszFileName)
{
    /* See if we can open the SQLite database */
    int rc = sqlite3_open( pszFilename, &m_poDB );
    if ( rc != SQLITE_OK )
    {
        m_poDB = NULL;
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "sqlite3_open(%s) failed: %s",
                  pszFilename, sqlite3_errmsg( m_poDB ) );
        return FALSE;
    }
        
    /* Filename is good, store it for future reference */
    m_pszName = CPLStrdup( pszFilename );
    
    return TRUE;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

int OGRGeoPackageDataSource::Open(const char * pszFilename, int bUpdate )
{
    CPLAssert( nLayers == 0 );
    
    if ( m_pszName == NULL )
        m_pszName = CPLStrdup( pszFilename );

    m_bUpdate = bUpdate;

    /* 1.1.1: File name has to end in "gpkg" */
    /* http://opengis.github.io/geopackage/#_file_extension_name */
    int nLen = strlen(pszFilename);
    if(! (nLen >= 5 && EQUAL(pszFilename + nLen - 5, ".gpkg")) )
        return FALSE;

    /* Check that the filename exists and is a file */
    VSIStatBuf stat;
    if( CPLStat( pszFilename, &stat ) != 0 || !VSI_ISREG(stat.st_mode) )
        return FALSE;

    /* Try to open the file */
    if ( ! OpenOrCreate(pszFilename) )
        return FALSE;


    
    OGRSQLiteInitSpatialite();
    
    

    return TRUE;
}



/************************************************************************/
/*                                Create()                              */
/************************************************************************/

int OGRGeoPackageDataSource::Create( const char * pszFilename, char **papszOptions )
{
    int rc;
    CPLString osCommand;
    char *pszErrMsg = NULL;

	/* The OGRGeoPackageDriver has already confirmed that the pszFilename */
	/* is not already in use, so try to open the file */
    if ( ! OpenOrCreate(pszFilename) )
        return FALSE;

    /* 1.1.1: A GeoPackage SHALL contain 0x47503130 ("GP10" in ASCII) in the application id */
    /* http://opengis.github.io/geopackage/#_file_format */
    char *sql_application_id = "PRAGMA application_id = 0x47503130";
    rc = sqlite3_exec(m_poDB, sql_application_id, NULL, NULL, &pszErrMsg);
    if ( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "sqlite3_exec(%s) failed: %s",
                  sql_application_id, pszErrMsg );
        return FALSE;
    }
    
    
      sqlite3*,                                  /* An open database */
      const char *sql,                           /* SQL to be evaluated */
      int (*callback)(void*,int,char**,char**),  /* Callback function */
      void *,                                    /* 1st argument to callback */
      char **errmsg                              /* Error msg written here */
    );

        "PRAGMA application_id = value;"








    if (!OpenOrCreateDB(0))
        return FALSE;

/* -------------------------------------------------------------------- */
/*      Create the SpatiaLite metadata tables.                          */
/* -------------------------------------------------------------------- */
    if ( bSpatialite )
    {
        /*
        / SpatiaLite full support: calling InitSpatialMetadata()
        /
        / IMPORTANT NOTICE: on SpatiaLite any attempt aimed
        / to directly CREATE "geometry_columns" and "spatial_ref_sys"
        / [by-passing InitSpatialMetadata() as absolutely required]
        / will severely [and irremediably] corrupt the DB !!!
        */
        
        const char* pszVal = CSLFetchNameValue( papszOptions, "INIT_WITH_EPSG" );
        if( pszVal != NULL && !CSLTestBoolean(pszVal) &&
            OGRSQLiteGetSpatialiteVersionNumber() >= 40 )
            osCommand =  "SELECT InitSpatialMetadata('NONE')";
        else
        {
            /* Since spatialite 4.1, InitSpatialMetadata() is no longer run */
            /* into a transaction, which makes population of spatial_ref_sys */
            /* from EPSG awfully slow. We have to use InitSpatialMetadata(1) */
            /* to run within a transaction */
            if( OGRSQLiteGetSpatialiteVersionNumber() >= 41 )
                osCommand =  "SELECT InitSpatialMetadata(1)";
            else
                osCommand =  "SELECT InitSpatialMetadata()";
        }
        rc = sqlite3_exec( poDB, osCommand, NULL, NULL, &pszErrMsg );
        if( rc != SQLITE_OK )
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                "Unable to Initialize SpatiaLite Metadata: %s",
                    pszErrMsg );
            sqlite3_free( pszErrMsg );
            return FALSE;
        }
    }

/* -------------------------------------------------------------------- */
/*  Create the geometry_columns and spatial_ref_sys metadata tables.    */
/* -------------------------------------------------------------------- */
    else if( bMetadata )
    {
        osCommand =
            "CREATE TABLE geometry_columns ("
            "     f_table_name VARCHAR, "
            "     f_geometry_column VARCHAR, "
            "     geometry_type INTEGER, "
            "     coord_dimension INTEGER, "
            "     srid INTEGER,"
            "     geometry_format VARCHAR )";
        rc = sqlite3_exec( poDB, osCommand, NULL, NULL, &pszErrMsg );
        if( rc != SQLITE_OK )
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "Unable to create table geometry_columns: %s",
                      pszErrMsg );
            sqlite3_free( pszErrMsg );
            return FALSE;
        }

        osCommand =
            "CREATE TABLE spatial_ref_sys        ("
            "     srid INTEGER UNIQUE,"
            "     auth_name TEXT,"
            "     auth_srid TEXT,"
            "     srtext TEXT)";
        rc = sqlite3_exec( poDB, osCommand, NULL, NULL, &pszErrMsg );
        if( rc != SQLITE_OK )
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "Unable to create table spatial_ref_sys: %s",
                      pszErrMsg );
            sqlite3_free( pszErrMsg );
            return FALSE;
        }
    }

/* -------------------------------------------------------------------- */
/*      Optionnaly initialize the content of the spatial_ref_sys table  */
/*      with the EPSG database                                          */
/* -------------------------------------------------------------------- */
    if ( (bSpatialite || bMetadata) &&
         CSLFetchBoolean( papszOptions, "INIT_WITH_EPSG", FALSE ) )
    {
        if (!InitWithEPSG())
            return FALSE;
    }

    return Open(pszName, TRUE);
}
