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
    pszName = NULL;

    papoLayers = NULL;
    nLayers = 0;
}

/************************************************************************/
/*                       ~OGRGeoPackageDataSource()                     */
/************************************************************************/

OGRGeoPackageDataSource::~OGRGeoPackageDataSource()
{
    for( int i = 0; i < nLayers; i++ )
        delete papoLayers[i];
        
    CPLFree( papoLayers );
    CPLFree( pszName );
}


/************************************************************************/
/*                       OpenOrCreateDB()                                */
/************************************************************************/
int OGRGeoPackageDataSource::OpenOrCreateDB(int flags)
{
    CPLAssert( m_pszName != NULL );

    int rc = sqlite3_open( m_pszName, &m_poDb );

    if( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "sqlite3_open(%s) failed: %s",
                  pszName, sqlite3_errmsg( hDB ) );
        return FALSE;
    }

    int nRowCount = 0, nColCount = 0;
    char** papszResult = NULL;
    sqlite3_get_table( hDB,
                       "SELECT name, sql FROM sqlite_master "
                       "WHERE (type = 'trigger' OR type = 'view') AND ("
                       "sql LIKE '%%ogr_geocode%%' OR "
                       "sql LIKE '%%ogr_datasource_load_layers%%' OR "
                       "sql LIKE '%%ogr_GetConfigOption%%' OR "
                       "sql LIKE '%%ogr_SetConfigOption%%' )",
                       &papszResult, &nRowCount, &nColCount,
                       NULL );

    sqlite3_free_table(papszResult);
    papszResult = NULL;

    if( nRowCount > 0 )
    {
        if( !CSLTestBoolean(CPLGetConfigOption("ALLOW_OGR_SQL_FUNCTIONS_FROM_TRIGGER_AND_VIEW", "NO")) )
        {
            CPLError( CE_Failure, CPLE_OpenFailed, "%s", 
                "A trigger and/or view calls a OGR extension SQL function that could be used to "
                "steal data, or use network bandwith, without your consent.\n"
                "The database will not be opened unless the ALLOW_OGR_SQL_FUNCTIONS_FROM_TRIGGER_AND_VIEW "
                "configuration option to YES.");
            return FALSE;
        }
    }

    const char* pszSqliteJournal = CPLGetConfigOption("OGR_SQLITE_JOURNAL", NULL);
    if (pszSqliteJournal != NULL)
    {
        char* pszErrMsg = NULL;
        char **papszResult;
        int nRowCount, nColCount;

        const char* pszSQL = CPLSPrintf("PRAGMA journal_mode = %s",
                                        pszSqliteJournal);

        rc = sqlite3_get_table( hDB, pszSQL,
                                &papszResult, &nRowCount, &nColCount,
                                &pszErrMsg );
        if( rc == SQLITE_OK )
        {
            sqlite3_free_table(papszResult);
        }
        else
        {
            sqlite3_free( pszErrMsg );
        }
    }

    const char* pszSqlitePragma = CPLGetConfigOption("OGR_SQLITE_PRAGMA", NULL);
    if (pszSqlitePragma != NULL)
    {
        char** papszTokens = CSLTokenizeString2( pszSqlitePragma, ",", CSLT_HONOURSTRINGS );
        for(int i=0; papszTokens[i] != NULL; i++ )
        {
            char* pszErrMsg = NULL;
            char **papszResult;
            int nRowCount, nColCount;

            const char* pszSQL = CPLSPrintf("PRAGMA %s", papszTokens[i]);

            rc = sqlite3_get_table( hDB, pszSQL,
                                    &papszResult, &nRowCount, &nColCount,
                                    &pszErrMsg );
            if( rc == SQLITE_OK )
            {
                sqlite3_free_table(papszResult);
            }
            else
            {
                sqlite3_free( pszErrMsg );
            }
        }
        CSLDestroy(papszTokens);
    }

    if (!SetCacheSize())
        return FALSE;

    if (!SetSynchronous())
        return FALSE;

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
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "File '%s' does not end in '.gpkg'",
                  pszFilename );
        return FALSE;
    }

    /* Check that the filename exists and is a file */
    VSIStatBuf stat;
    if( CPLStat( pszFilename, &stat ) != 0 || !VSI_ISREG(stat.st_mode) )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Cannot open file '%s' for reading",
                  pszFilename );
        return FALSE;
    }

    /* See if we can open the SQLite database */
    m_poDb = 
    
    OGRSQLiteInitSpatialite();
    
    

    return TRUE;
}



/************************************************************************/
/*                                Create()                              */
/************************************************************************/

int OGRGeoPackageDataSource::Create( const char * pszNameIn, char **papszOptions )
{
    int rc;
    CPLString osCommand;
    char *pszErrMsg = NULL;

    pszName = CPLStrdup( pszNameIn );

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
        rc = sqlite3_exec( hDB, osCommand, NULL, NULL, &pszErrMsg );
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
        rc = sqlite3_exec( hDB, osCommand, NULL, NULL, &pszErrMsg );
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
        rc = sqlite3_exec( hDB, osCommand, NULL, NULL, &pszErrMsg );
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
