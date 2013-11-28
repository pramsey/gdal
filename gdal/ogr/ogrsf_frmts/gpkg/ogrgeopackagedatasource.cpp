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
#include "ogrgeopackageutility.h"



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
        CPLError( CE_Failure, CPLE_AppDefined, 
                  "unable to execute PRAGME %s", pszPragma);
        return OGRERR_FAILURE;
    }
    
    if ( nRowCount != nRowsExpected )
    {
        CPLError( CE_Failure, CPLE_AppDefined, 
                  "bad result for PRAGMA %s, got %d rows, expected %d", pszPragma, nRowCount, nRowsExpected);
        return OGRERR_FAILURE;        
    }
    
    char *pszGot = papszResult[1];
    if ( ! EQUAL(pszGot, pszExpected) )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "invalid %s (expected '%s', got '%s')",
                  pszPragma, pszExpected, pszGot);
        return OGRERR_FAILURE;
    }
    
    sqlite3_free_table(papszResult);
    
    return OGRERR_NONE; 
}


OGRSpatialReference* OGRGeoPackageDataSource::GetSpatialRef(int iSrsId)
{
    SQLResult oResult;
    
    char *pszSQL = sqlite3_mprintf(
                     "SELECT definition FROM gpkg_spatial_ref_sys WHERE "
                     "srs_id = %d",
                     iSrsId );
    
    OGRErr err = SQLQuery(m_poDb, pszSQL, &oResult);
    if ( err != OGRERR_NONE || oResult.nRowCount != 1 )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "unable to read srs_id '%d' from gpkg_spatial_ref_sys",
                  iSrsId);
        SQLResultFree(&oResult);
        return NULL;
    }
    
    char *pszWkt = SQLResultGetValue(&oResult, 0, 0);
    if ( ! pszWkt )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "null definition for srs_id '%d' in gpkg_spatial_ref_sys",
                  iSrsId);
        SQLResultFree(&oResult);
        return NULL;
    }
    
    OGRSpatialReference *poSpatialRef = new OGRSpatialReference();
    err = poSpatialRef->importFromWkt(&pszWkt);
    
    if ( err != OGRERR_NONE )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "unable to srs_id '%d' definition wkt: %s",
                  iSrsId, pszWkt);
        SQLResultFree(&oResult);
        return NULL;
    }
    
    return poSpatialRef;
}


int OGRGeoPackageDataSource::GetSrsId(const OGRSpatialReference * poSRS)
{
    char *pszWKT = NULL;
    char *pszSQL = NULL;
    int nSRSId = UNDEFINED_SRID;
    int nMaxSRSId;
    const char* pszAuthorityName;
    int nAuthorityCode;
    OGRErr err;

    if( poSRS == NULL )
        return UNDEFINED_SRID;

    OGRSpatialReference oSRS(*poSRS);

    pszAuthorityName = oSRS.GetAuthorityName(NULL);

    if ( pszAuthorityName == NULL || strlen(pszAuthorityName) == 0 )
    {
        // Try to force identify an EPSG code                                    
        oSRS.AutoIdentifyEPSG();

        pszAuthorityName = oSRS.GetAuthorityName(NULL);
        if (pszAuthorityName != NULL && EQUAL(pszAuthorityName, "EPSG"))
        {
            const char* pszAuthorityCode = oSRS.GetAuthorityCode(NULL);
            if ( pszAuthorityCode != NULL && strlen(pszAuthorityCode) > 0 )
            {
                /* Import 'clean' SRS */
                oSRS.importFromEPSG( atoi(pszAuthorityCode) );

                pszAuthorityName = oSRS.GetAuthorityName(NULL);
            }
        }
    }
    // Check whether the EPSG authority code is already mapped to a
    // SRS ID.                                                         
    if ( pszAuthorityName != NULL && strlen(pszAuthorityName) > 0 )
    {
        // For the root authority name 'EPSG', the authority code
        // should always be integral
        nAuthorityCode = atoi( oSRS.GetAuthorityCode(NULL) );

        pszSQL = sqlite3_mprintf(
                         "SELECT srid FROM gpkg_spatial_ref_sys WHERE "
                         "upper(organization) = upper('%q') AND organization_coordsys_id = %d",
                         pszAuthorityName, nAuthorityCode );
        
        // Got a match? Return it!
        nSRSId = SQLGetInteger(m_poDb, pszSQL, &err);
        sqlite3_free(pszSQL);
        
        if ( OGRERR_NONE == err )
            return nSRSId;     
    }

    // Translate SRS to WKT.                                           
    if( oSRS.exportToWkt( &pszWKT ) != OGRERR_NONE )
    {
        CPLFree(pszWKT);
        return UNDEFINED_SRID;
    }

    // Get the current maximum srid in the srs table.                  
    nMaxSRSId = SQLGetInteger(m_poDb, "SELECT MAX(srs_id) FROM gpkg_spatial_ref_sys", &err);
    if ( OGRERR_NONE != err )
    {
        return UNDEFINED_SRID;        
    }
    
    nSRSId = nMaxSRSId + 1;
    
    // Add new SRS row to gpkg_spatial_ref_sys
    if( pszAuthorityName != NULL )
    {
        int nAuthorityCode = atoi( oSRS.GetAuthorityCode(NULL) );
        pszSQL = sqlite3_mprintf(
                 "INSERT INTO gpkg_spatial_ref_sys "
                 "(srs_name,srs_id,organization,organization_coordsys_id,definition) "
                 "VALUES ('%s', %d, upper('%s'), %d, '%q')",
                 "", nSRSId, pszAuthorityName, nAuthorityCode, pszWKT
                 );
    }
    else
    {
        pszSQL = sqlite3_mprintf(
                 "INSERT INTO gpkg_spatial_ref_sys "
                 "(srs_name,srs_id,organization,organization_coordsys_id,definition) "
                 "VALUES ('%s', %d, upper('%s'), %d, '%q')",
                 "", nSRSId, "NONE", nSRSId, pszWKT
                 );
    }

    // Add new row to gpkg_spatial_ref_sys
    err = SQLCommand(m_poDb, pszSQL);

    // Free everything that was allocated.
    CPLFree(pszWKT);    
    sqlite3_free(pszSQL);
    
    return nSRSId;
}


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
/*                          GetDatabaseHandle()                         */
/************************************************************************/

sqlite3* OGRGeoPackageDataSource::GetDatabaseHandle()
{
    return m_poDb;
}

/************************************************************************/
/*                                Create()                              */
/************************************************************************/

int OGRGeoPackageDataSource::Create( const char * pszFilename, char **papszOptions )
{
    CPLString osCommand;
    const char *pszSpatialRefSysRecord;

	/* The OGRGeoPackageDriver has already confirmed that the pszFilename */
	/* is not already in use, so try to create the file */
    if ( OpenOrCreate(pszFilename) != OGRERR_NONE )
        return FALSE;

    /* Requirement 2: A GeoPackage SHALL contain 0x47503130 ("GP10" in ASCII) in the application id */
    /* http://opengis.github.io/geopackage/#_file_format */
    const char *pszPragma = CPLSPrintf("PRAGMA application_id = %d", GPKG_APPLICATION_ID);
    
    if ( OGRERR_NONE != SQLCommand(m_poDb, pszPragma) )
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
        
    if ( OGRERR_NONE != SQLCommand(m_poDb, pszSpatialRefSys) )
        return FALSE;

    /* Requirement 11: The gpkg_spatial_ref_sys table in a GeoPackage SHALL */
    /* contain a record for EPSG:4326, the geodetic WGS84 SRS */
    /* http://opengis.github.io/geopackage/#spatial_ref_sys */
    pszSpatialRefSysRecord = 
        "INSERT INTO gpkg_spatial_ref_sys ("
        "srs_name, srs_id, organization, organization_coordsys_id, definition, description"
        ") VALUES ("
        "'WGS 84 geodetic', 4326, 'EPSG', 4326, '"
        "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563,AUTHORITY[\"EPSG\",\"7030\"]],AUTHORITY[\"EPSG\",\"6326\"]],PRIMEM[\"Greenwich\",0,AUTHORITY[\"EPSG\",\"8901\"]],UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],AUTHORITY[\"EPSG\",\"4326\"]]"
        "', 'longitude/latitude coordinates in decimal degrees on the WGS 84 spheroid'"
        ")";  
          
    if ( OGRERR_NONE != SQLCommand(m_poDb, pszSpatialRefSysRecord) )
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
           
    if ( OGRERR_NONE != SQLCommand(m_poDb, pszSpatialRefSysRecord) )
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
           
    if ( OGRERR_NONE != SQLCommand(m_poDb, pszSpatialRefSysRecord) )
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
        
    if ( OGRERR_NONE != SQLCommand(m_poDb, pszContents) )
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
        
    if ( OGRERR_NONE != SQLCommand(m_poDb, pszGeometryColumns) )
        return FALSE;

    return TRUE;
}


/************************************************************************/
/*                              AddColumn()                             */
/************************************************************************/

OGRErr OGRGeoPackageDataSource::AddColumn(const char *pszTableName, const char *pszColumnName, const char *pszColumnType)
{
    char *pszSQL;
    
    pszSQL = sqlite3_mprintf("ALTER TABLE %s ADD COLUMN %s %s", 
                             pszTableName, pszColumnName, pszColumnType);

    OGRErr err = SQLCommand(m_poDb, pszSQL);
    sqlite3_free(pszSQL);
    
    return err;
}


/************************************************************************/
/*                              GetLayer()                              */
/************************************************************************/

OGRLayer* OGRGeoPackageDataSource::GetLayer( int iLayer )

{
    if( iLayer < 0 || iLayer >= m_nLayers )
        return NULL;
    else
        return m_papoLayers[iLayer];
}


/************************************************************************/
/*                           CreateLayer()                              */
/* Options:                                                             */
/*   FID = primary key name                                             */
/*   OVERWRITE = YES|NO, overwrite existing layer?                      */
/*   SPATIAL_INDEX = YES|NO, TBD                                        */
/************************************************************************/

OGRLayer* OGRGeoPackageDataSource::CreateLayer( const char * pszLayerName,
                                      OGRSpatialReference * poSpatialRef,
                                      OGRwkbGeometryType eGType,
                                      char **papszOptions )
{
    int iLayer;
    OGRErr err;

    /* Read GEOMETRY_COLUMN option */
    const char* pszGeomColumnName = CSLFetchNameValue(papszOptions, "GEOMETRY_COLUMN");
    if (pszGeomColumnName == NULL)
        pszGeomColumnName = "geometry";
    
    /* Read FID option */
    const char* pszFIDColumnName = CSLFetchNameValue(papszOptions, "FID");
    if (pszFIDColumnName == NULL)
        pszFIDColumnName = "FID";

    if ( strspn(pszFIDColumnName, "`~!@#$%^&*()_+-={}|[]\\:\";'<>?,./") > 0 )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "The primary key (%s) name may not contain special characters or spaces", 
                 pszFIDColumnName);
        return NULL;
    }

    /* Avoiding gpkg prefixes is not an official requirement, but seems wise */
    if (strncmp(pszLayerName, "gpkg", 4) == 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "The layer name may not begin with 'gpkg' as it is a reserved geopackage prefix");
        return NULL;
    }

    /* Pre-emptively try and avoid sqlite3 syntax errors due to  */
    /* illegal characters */
    if ( strspn(pszLayerName, "`~!@#$%^&*()_+-={}|[]\\:\";'<>?,./") > 0 )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "The layer name may not contain special characters or spaces");
        return NULL;
    }

    /* Check for any existing layers that already use this name */
    for( iLayer = 0; iLayer < m_nLayers; iLayer++ )
    {
        if( EQUAL(pszLayerName, m_papoLayers[iLayer]->GetName()) )
        {
            if( CSLFetchNameValue( papszOptions, "OVERWRITE" ) != NULL &&
                EQUAL(CSLFetchNameValue(papszOptions,"OVERWRITE"),"YES") )
            {
                DeleteLayer( iLayer );
            }
            else
            {
                CPLError( CE_Failure, CPLE_AppDefined,
                          "Layer %s already exists, CreateLayer failed.\n"
                          "Use the layer creation option OVERWRITE=YES to "
                          "replace it.",
                          pszLayerName );
                return NULL;
            }
        }
    }

    /* Read our SRS_ID from the OGRSpatialReference */
    int nSRSId = UNDEFINED_SRID;
    if( poSpatialRef != NULL )
        nSRSId = GetSrsId( poSpatialRef );
        
    /* Requirement 25: The geometry_type_name value in a gpkg_geometry_columns */
    /* row SHALL be one of the uppercase geometry type names specified in */
    /* Geometry Types (Normative). */
    const char *pszGeometryType = OGRToOGCGeomType(eGType);
    
    /* Create the table! */
    char *pszSQL = NULL;
    if ( eGType != wkbNone )
    {
        pszSQL = sqlite3_mprintf(
            "CREATE TABLE %s ( "
            "%s INTEGER PRIMARY KEY AUTOINCREMENT, "
            "%s GEOMETRY )",
             pszLayerName, pszFIDColumnName, pszGeomColumnName);
    }
    else
    {
        pszSQL = sqlite3_mprintf(
            "CREATE TABLE %s ( "
            "%s INTEGER PRIMARY KEY AUTOINCREMENT )",
             pszLayerName, pszFIDColumnName);
    }
    
    err = SQLCommand(m_poDb, pszSQL);
    sqlite3_free(pszSQL);
    if ( OGRERR_NONE != err )
        return NULL;

    /* Only spatial tables need to be registered in the metadata (hmmm) */
    if ( eGType != wkbNone )
    {
        /* Requirement 27: The z value in a gpkg_geometry_columns table row */
        /* SHALL be one of 0 (none), 1 (mandatory), or 2 (optional) */
        int bGeometryTypeHasZ = wkb25DBit & eGType;

        /* Update gpkg_geometry_columns with the table info */
        pszSQL = sqlite3_mprintf(
            "INSERT INTO INTO gpkg_geometry_columns "
            "(table_name,column_name,geometry_type_name,srs_id,z,m)"
            " VALUES "
            "('%q','%q','%q',%d,%d,%d)",
            pszLayerName,pszFIDColumnName,pszGeometryType,
            nSRSId,bGeometryTypeHasZ,0);
    
        err = SQLCommand(m_poDb, pszSQL);
        sqlite3_free(pszSQL);
        if ( err != OGRERR_NONE )
            return NULL;

        /* Update gpkg_contents with the table info */
        pszSQL = sqlite3_mprintf(
            "INSERT INTO INTO gpkg_contents "
            "(table_name,data_type,identifier,last_change,srs_id)"
            " VALUES "
            "('%q','features','%q',strftime('%%Y-%%m-%%dT%%H:%%M:%%fZ',CURRENT_TIMESTAMP),%d)",
            pszLayerName, pszLayerName, nSRSId);
    
        err = SQLCommand(m_poDb, pszSQL);
        sqlite3_free(pszSQL);
        if ( err != OGRERR_NONE )
            return NULL;

    }

    /* This is where spatial index logic will go in the future */
    const char *pszSI = CSLFetchNameValue( papszOptions, "SPATIAL_INDEX" );
    int bCreateSpatialIndex = ( pszSI == NULL || CSLTestBoolean(pszSI) );
    if( eGType != wkbNone && bCreateSpatialIndex )
    {
        /* This is where spatial index logic will go in the future */
    }
    
    /* The database is now all set up, so create a blank layer and read in the */
    /* info from the database. */
    OGRGeoPackageLayer *poLayer = new OGRGeoPackageLayer(this, pszLayerName);
    
    if( OGRERR_NONE != poLayer->ReadTableDefinition() )
    {
        delete poLayer;
        return NULL;
    }
    
    m_papoLayers = (OGRLayer**)CPLRealloc(m_papoLayers,  sizeof(OGRGeoPackageLayer*) * (m_nLayers+1));
    m_papoLayers[m_nLayers++] = poLayer;
    return poLayer;
}