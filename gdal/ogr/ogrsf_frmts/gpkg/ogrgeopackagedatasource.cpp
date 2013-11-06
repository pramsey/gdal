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
    m_poDb = NULL;
}

/************************************************************************/
/*                       ~OGRGeoPackageDataSource()                     */
/************************************************************************/

OGRGeoPackageDataSource::~OGRGeoPackageDataSource()
{
    for( int i = 0; i < m_nLayers; i++ )
        delete m_papoLayers[i];
        
    if ( m_poDb )
        sqlite3_close(m_poDb);
        
    CPLFree( m_papoLayers );
    CPLFree( m_pszName );
}

OGRErr OGRGeoPackageDataSource::OpenOrCreate(const char * pszFilename)
{
    /* See if we can open the SQLite database */
    int rc = sqlite3_open( pszFilename, &m_poDb );
    if ( rc != SQLITE_OK )
    {
        m_poDb = NULL;
        CPLError( CE_Failure, CPLE_OpenFailed, "sqlite3_open(%s) failed: %s",
                  pszFilename, sqlite3_errmsg( m_poDb ) );
        return OGRERR_FAILURE;
    }
        
    /* Filename is good, store it for future reference */
    m_pszName = CPLStrdup( pszFilename );
    
    return OGRERR_NONE;
}

/* Returns the first row of first column of SQL as integer */
OGRErr OGRGeoPackageDataSource::SelectInt(const char * pszSQL, int *i)
{
    CPLAssert( m_poDb != NULL );
    CPLAssert( i != NULL );
    
    sqlite3_stmt *poStmt;
    int rc;
    
    /* Prepare the SQL */
    rc = sqlite3_prepare_v2(m_poDb, pszSQL, strlen(pszSQL), &poStmt, NULL);
    if ( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_OpenFailed, "sqlite3_prepare_v2(%s) failed: %s",
                  pszSQL, sqlite3_errmsg( m_poDb ) );
        return OGRERR_FAILURE;
    }
    
    /* Execute and fetch first row */
    rc = sqlite3_step(poStmt);
    if ( rc != SQLITE_ROW )
        return OGRERR_FAILURE;
    
    /* Read the integer from the row */
    *i = sqlite3_column_int(poStmt, 0);
    sqlite3_finalize(poStmt);
    
    return OGRERR_NONE;
}

/* Returns the first row of first column of SQL as integer */
OGRErr OGRGeoPackageDataSource::PragmaCheck(const char * pszPragma, const char * pszExpected, int nRowsExpected)
{
    CPLAssert( pszPragma != NULL );
    CPLAssert( pszExpected != NULL );
    CPLAssert( nExpected >= 0 );
    
    char *pszErrMsg = NULL;
    int nRowCount, nColCount, rc;
    char **papszResult;

    rc = sqlite3_get_table( 
        m_poDb,
        CPLSPrintf("PRAGMA %s", pszPragma),
        &papszResult, &nRowCount, &nColCount, &pszErrMsg );
    
    if( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_OpenFailed, 
                  "unable to execute PRAGME %s", pszPragma);
        return OGRERR_FAILURE;
    }
    
    if ( nRowCount != nRowsExpected )
    {
        CPLError( CE_Failure, CPLE_OpenFailed, 
                  "bad result for PRAGMA %s, got %d rows, expected %d", pszPragma, nRowCount, nRowsExpected);
        return OGRERR_FAILURE;        
    }
    
    char *pszGot = papszResult[0];
    if ( ! EQUAL(pszGot, pszExpected) )
    {
        CPLError( CE_Failure, CPLE_OpenFailed, "invalid %s (expected '%s', got '%s')",
                  pszPragma, pszExpected, pszGot);
        return OGRERR_FAILURE;
    }
    
    sqlite3_free_table(papszResult);
    
    return OGRERR_NONE; 
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

int OGRGeoPackageDataSource::Open(const char * pszFilename, int bUpdate )
{
    CPLAssert( m_nLayers == 0 );

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
    if ( OpenOrCreate(pszFilename) != OGRERR_NONE )
        return FALSE;

    /* 1.1.1: A GeoPackage SHALL contain 0x47503130 ("GP10" in ASCII) in the application id */
    /* http://opengis.github.io/geopackage/#_file_format */
    if ( OGRERR_NONE != PragmaCheck("application_id", CPLSPrintf("%d", GPKG_APPLICATION_ID), 1) )
        return FALSE;
        
    /* 1.1.1: The SQLite PRAGMA integrity_check SQL command SHALL return “ok” */
    /* http://opengis.github.io/geopackage/#_file_integrity */
    if ( OGRERR_NONE != PragmaCheck("integrity_check", "ok", 1) )
        return FALSE;
    
    /* 1.1.1: The SQLite PRAGMA foreign_key_check() SQL with no parameter value SHALL return an empty result set */
    /* http://opengis.github.io/geopackage/#_file_integrity */
    /* TBD */
    /* if ( OGRERR_NONE != PragmaCheck("foreign_key_check", "", 0) ) */
    /*    return FALSE; */

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
	/* is not already in use, so try to create the file */
    if ( OpenOrCreate(pszFilename) != OGRERR_NONE )
        return FALSE;

    /* 1.1.1: A GeoPackage SHALL contain 0x47503130 ("GP10" in ASCII) in the application id */
    /* http://opengis.github.io/geopackage/#_file_format */
    const char *pszSQL = CPLSPrintf("PRAGMA application_id = %d", GPKG_APPLICATION_ID);
    rc = sqlite3_exec(m_poDb, pszSQL, NULL, NULL, &pszErrMsg);
    if ( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "sqlite3_exec(%s) failed: %s",
                  pszSQL, pszErrMsg );
        return FALSE;
    }
    
    /* 1.1.2: A GeoPackage SHALL include a gpkg_spatial_ref_sys table */
    /* http://opengis.github.io/geopackage/#spatial_ref_sys */
    const char *pszSpatialRefSys = "CREATE TABLE gpkg_spatial_ref_sys ("
                                   "srs_name TEXT NOT NULL,"
                                   "srs_id INTEGER NOT NULL PRIMARY KEY,"
                                   "organization TEXT NOT NULL,"
                                   "organization_coordsys_id INTEGER NOT NULL,"
                                   "definition  TEXT NOT NULL,"
                                   "description TEXT"
                                   ")";
    rc = sqlite3_exec(m_poDb, pszSpatialRefSys, NULL, NULL, &pszErrMsg);
    if ( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "sqlite3_exec(%s) failed: %s",
                  pszSQL, pszErrMsg );
        return FALSE;
    }
    
    return TRUE;
}
