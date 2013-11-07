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
OGRErr OGRGeoPackageDataSource::ExecSQL(const char * pszSQL)
{
    CPLAssert( m_poDb != NULL );
    CPLAssert( pszSQL != NULL );

    char *pszErrMsg = NULL;
    int rc = sqlite3_exec(m_poDb, pszSQL, NULL, NULL, &pszErrMsg);
    
    if ( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "sqlite3_exec(%s) failed: %s",
                  pszSQL, pszErrMsg );
        return OGRERR_FAILURE;
    }
    
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

    /* Requirement 3: File name has to end in "gpkg" */
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

    /* Requirement 2: A GeoPackage SHALL contain 0x47503130 ("GP10" in ASCII) */
    /* in the application id */
    /* http://opengis.github.io/geopackage/#_file_format */
    if ( OGRERR_NONE != PragmaCheck("application_id", CPLSPrintf("%d", GPKG_APPLICATION_ID), 1) )
        return FALSE;
        
    /* Requirement 6: The SQLite PRAGMA integrity_check SQL command SHALL return “ok” */
    /* http://opengis.github.io/geopackage/#_file_integrity */
    if ( OGRERR_NONE != PragmaCheck("integrity_check", "ok", 1) )
        return FALSE;
    
    /* Requirement 7: The SQLite PRAGMA foreign_key_check() SQL with no */
    /* parameter value SHALL return an empty result set */
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
    CPLString osCommand;

	/* The OGRGeoPackageDriver has already confirmed that the pszFilename */
	/* is not already in use, so try to create the file */
    if ( OpenOrCreate(pszFilename) != OGRERR_NONE )
        return FALSE;

    /* Requirement 2: A GeoPackage SHALL contain 0x47503130 ("GP10" in ASCII) in the application id */
    /* http://opengis.github.io/geopackage/#_file_format */
    const char *pszPragma = CPLSPrintf("PRAGMA application_id = %d", GPKG_APPLICATION_ID);
    if ( OGRERR_NONE != ExecSQL(pszPragma) )
        return FALSE;
        
    /* Requirement 10: A GeoPackage SHALL include a gpkg_spatial_ref_sys table */
    /* http://opengis.github.io/geopackage/#spatial_ref_sys */
    const char *pszSpatialRefSys = 
        "CREATE TABLE gpkg_spatial_ref_sys ("
        "srs_name TEXT NOT NULL,"
        "srs_id INTEGER NOT NULL PRIMARY KEY,"
        "organization TEXT NOT NULL,"
        "organization_coordsys_id INTEGER NOT NULL,"
        "definition  TEXT NOT NULL,"
        "description TEXT"
        ")";
    if ( OGRERR_NONE != ExecSQL(pszSpatialRefSys) )
        return FALSE;

    /* Requirement 11: The gpkg_spatial_ref_sys table in a GeoPackage SHALL */
    /* contain a record for EPSG:4326, the geodetic WGS84 SRS */
    /* http://opengis.github.io/geopackage/#spatial_ref_sys */
    const char *pszSpatialRefSysRecord = 
        "INSERT INTO gpkg_spatial_ref_sys ("
        "srs_name, srs_id, organization, organization_coordsys_id, definition, description"
        ") VALUES ("
        "'WGS 84 geodetic', 4326, 'EPSG', 4326, '"
        "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563,AUTHORITY[\"EPSG\",\"7030\"]],AUTHORITY[\"EPSG\",\"6326\"]],PRIMEM[\"Greenwich\",0,AUTHORITY[\"EPSG\",\"8901\"]],UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],AUTHORITY[\"EPSG\",\"4326\"]]"
        "', 'longitude/latitude coordinates in decimal degrees on the WGS 84 spheroid'"
        ")";    
    if ( OGRERR_NONE != ExecSQL(pszSpatialRefSysRecord) )
        return FALSE;

    /* Requirement 11: The gpkg_spatial_ref_sys table in a GeoPackage SHALL */
    /* contain a record with an srs_id of -1, an organization of “NONE”, */
    /* an organization_coordsys_id of -1, and definition “undefined” */
    /* for undefined Cartesian coordinate reference systems */
    /* http://opengis.github.io/geopackage/#spatial_ref_sys */
    pszSpatialRefSysRecord = 
        "INSERT INTO gpkg_spatial_ref_sys ("
        "srs_name, srs_id, organization, organization_coordsys_id, definition, description"
        ") VALUES ("
        "'Undefined cartesian SRS', -1, 'NONE', -1, 'undefined', 'undefined cartesian coordinate reference system'"
        ")";    
    if ( OGRERR_NONE != ExecSQL(pszSpatialRefSysRecord) )
        return FALSE;

    /* Requirement 11: The gpkg_spatial_ref_sys table in a GeoPackage SHALL */
    /* contain a record with an srs_id of 0, an organization of “NONE”, */
    /* an organization_coordsys_id of 0, and definition “undefined” */
    /* for undefined geographic coordinate reference systems */
    /* http://opengis.github.io/geopackage/#spatial_ref_sys */
    pszSpatialRefSysRecord = 
        "INSERT INTO gpkg_spatial_ref_sys ("
        "srs_name, srs_id, organization, organization_coordsys_id, definition, description"
        ") VALUES ("
        "'Undefined geographic SRS', 0, 'NONE', 0, 'undefined', 'undefined geographic coordinate reference system'"
        ")";    
    if ( OGRERR_NONE != ExecSQL(pszSpatialRefSysRecord) )
        return FALSE;
    
    /* Requirement 13: A GeoPackage file SHALL include a gpkg_contents table */
    /* http://opengis.github.io/geopackage/#_contents */
    const char *pszContents =
        "CREATE TABLE gpkg_contents ("
        "table_name TEXT NOT NULL PRIMARY KEY,"
        "data_type TEXT NOT NULL,"
        "identifier TEXT UNIQUE,"
        "description TEXT DEFAULT '',"
        "last_change DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ',CURRENT_TIMESTAMP)),"
        "min_x DOUBLE, min_y DOUBLE,"
        "max_x DOUBLE, max_y DOUBLE,"
        "srs_id INTEGER,"
        "CONSTRAINT fk_gc_r_srs_id FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys(srs_id)"
        ")";
    if ( OGRERR_NONE != ExecSQL(pszContents) )
        return FALSE;

    /* Requirement 21: A GeoPackage with a gpkg_contents table row with a “features” */
    /* data_type SHALL contain a gpkg_geometry_columns table or updateable view */
    /* http://opengis.github.io/geopackage/#_geometry_columns */
    const char *pszGeometryColumns =        
        "CREATE TABLE gpkg_geometry_columns ("
        "table_name TEXT NOT NULL,"
        "column_name TEXT NOT NULL,"
        "geometry_type_name TEXT NOT NULL,"
        "srs_id INTEGER NOT NULL,"
        "z TINYINT NOT NULL,"
        "m TINYINT NOT NULL,"
        "CONSTRAINT pk_geom_cols PRIMARY KEY (table_name, column_name),"
        "CONSTRAINT uk_gc_table_name UNIQUE (table_name),"
        "CONSTRAINT fk_gc_tn FOREIGN KEY (table_name) REFERENCES gpkg_contents(table_name),"
        "CONSTRAINT fk_gc_srs FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys (srs_id)"
        ")";
    if ( OGRERR_NONE != ExecSQL(pszGeometryColumns) )
        return FALSE;

    return TRUE;
}
