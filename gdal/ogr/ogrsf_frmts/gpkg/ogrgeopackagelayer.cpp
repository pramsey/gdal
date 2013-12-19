/******************************************************************************
 * $Id$
 *
 * Project:  GeoPackage Translator
 * Purpose:  Implements OGRGeoPackageLayer class
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


OGRErr OGRGeoPackageLayer::SaveExtent()
{
    if ( ! m_bExtentChanged || ! m_poExtent ) 
        return OGRERR_NONE;

    sqlite3* poDb = m_poDS->GetDatabaseHandle();

    if ( ! poDb ) return OGRERR_FAILURE;

    char *pszSQL = sqlite3_mprintf(
                "UPDATE gpkg_contents SET "
                "min_x = %g, min_y = %g, "
                "max_x = %g, max_y = %g "
                "WHERE table_name = '%q' AND "
                "Lower(data_type) = 'features'",
                m_poExtent->MinX, m_poExtent->MinY,
                m_poExtent->MaxX, m_poExtent->MaxY,
                m_pszTableName);

    OGRErr err = SQLCommand(poDb, pszSQL);
    sqlite3_free(pszSQL);
    m_bExtentChanged = FALSE;
    
    return err;
}

OGRErr OGRGeoPackageLayer::UpdateExtent( const OGREnvelope *poExtent )
{
    if ( ! m_poExtent )
    {
        m_poExtent = new OGREnvelope( *poExtent );
    }
    m_poExtent->Merge( *poExtent );
    m_bExtentChanged = TRUE;
    return OGRERR_NONE;
}

OGRErr OGRGeoPackageLayer::BuildColumns()
{
    if ( ! m_poFeatureDefn || ! m_pszFidColumn )
    {
        return OGRERR_FAILURE;
    }

    /* Always start with a primary key */
    CPLString soColumns = m_pszFidColumn;

    /* Add a geometry column if there is one (just one) */
    if ( m_poFeatureDefn->GetGeomFieldCount() )
    {
        soColumns += ", ";
        soColumns += m_poFeatureDefn->GetGeomFieldDefn(0)->GetNameRef();
    }

    /* Add all the attribute columns */
    for( int i = 0; i < m_poFeatureDefn->GetFieldCount(); i++ )
    {
        soColumns += ", ";
        soColumns += m_poFeatureDefn->GetFieldDefn(i)->GetNameRef();
    }

    m_soColumns = soColumns;    
    return OGRERR_NONE;    
}

OGRErr OGRGeoPackageLayer::ReadFeature( sqlite3_stmt *poQuery, OGRFeature **ppoFeature )
{
    int iColOffset = 0;
    
    if ( ! m_poFeatureDefn )
        return OGRERR_FAILURE;

    OGRFeature *poFeature = new OGRFeature( m_poFeatureDefn );
    
    /* Primary key is always first column in our SQL call */
    poFeature->SetFID(sqlite3_column_int(poQuery, iColOffset++));
    
    /* If a geometry column exists, it's next */
    /* Add a geometry column if there is one (just the first one) */
    if ( m_poFeatureDefn->GetGeomFieldCount() )
    {
        OGRSpatialReference* poSrs = m_poFeatureDefn->GetGeomFieldDefn(0)->GetSpatialRef();
        int iGpkgSize = sqlite3_column_bytes(poQuery, iColOffset);
        GByte *pabyGpkg = (GByte *)sqlite3_column_blob(poQuery, iColOffset);
        OGRGeometry *poGeom = GPkgGeometryToOGR(pabyGpkg, iGpkgSize, poSrs);
        if ( ! poGeom )
        {
            delete poFeature;
            CPLError( CE_Failure, CPLE_AppDefined, "Unable to read geometry");
            return OGRERR_FAILURE;
        }
        poFeature->SetGeometryDirectly( poGeom );
        iColOffset++;
    }
    
    /* Read all the attribute columns */
    for( int i = 0; i < m_poFeatureDefn->GetFieldCount(); i++ )
    {
        int iSqliteType = SQLiteFieldFromOGR(m_poFeatureDefn->GetFieldDefn(i)->GetType());
        int j = iColOffset+i;
        
        switch(iSqliteType)
        {
            case SQLITE_INTEGER:
            {
                int iVal = sqlite3_column_int(poQuery, j);
                poFeature->SetField(i, iVal);
                break;
            }
            case SQLITE_FLOAT:
            {
                double dVal = sqlite3_column_double(poQuery, j);
                poFeature->SetField(i, dVal);
                break;
            }
            case SQLITE_BLOB:
            {
                int iBlobSize = sqlite3_column_bytes(poQuery, j);
                GByte *pabyBlob = (GByte *)sqlite3_column_blob(poQuery, j);
                poFeature->SetField(i, iBlobSize, pabyBlob);
                break;
            }
            default: /* SQLITE_TEXT */
            {
                const char *pszVal = (const char *)sqlite3_column_text(poQuery, j);
                poFeature->SetField(i, pszVal);
                break;
            }
        }
    }
    
    /* Pass result back to the caller */
    *ppoFeature = poFeature;

    return OGRERR_NONE;
}

OGRErr OGRGeoPackageLayer::ReadTableDefinition()
{
    OGRErr err;
    SQLResult oResultTable;
    SQLResult oResultContents;
    SQLResult oResultGeomCols;
    char* pszSQL;
    sqlite3* poDb = m_poDS->GetDatabaseHandle();

    /* Check that the table name is registered in gpkg_contents */
    pszSQL = sqlite3_mprintf(
                "SELECT table_name, data_type, identifier, "
                "description, min_x, min_y, max_x, max_y, srs_id "
                "FROM gpkg_contents "
                "WHERE table_name = '%q' AND "
                "Lower(data_type) = 'features'",
                m_pszTableName);
                
    err = SQLQuery(poDb, pszSQL, &oResultContents);
    sqlite3_free(pszSQL);
    
    char *pszMinX = SQLResultGetValue(&oResultContents, 5, 0);
    char *pszMinY = SQLResultGetValue(&oResultContents, 6, 0);
    char *pszMaxX = SQLResultGetValue(&oResultContents, 7, 0);
    char *pszMaxY = SQLResultGetValue(&oResultContents, 8, 0);
    
	/* All the extrema have to be non-NULL for this to make sense */
    OGREnvelope oExtent;
    if ( pszMinX && pszMinY && pszMaxX && pszMaxY )
    {
        oExtent.MinX = atof(pszMinX);
        oExtent.MinY = atof(pszMinY);
        oExtent.MaxX = atof(pszMaxX);
        oExtent.MaxY = atof(pszMaxY);
    }

    /* gpkg_contents query has to work */
    /* gpkg_contents.table_name is supposed to be unique */
    if ( err != OGRERR_NONE || oResultContents.nRowCount != 1 )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "%s", oResultContents.pszErrMsg );
        SQLResultFree(&oResultContents);
        return err;        
    }

    /* Done with info from gpkg_contents now */
    SQLResultFree(&oResultContents);

    /* Check that the table name is registered in gpkg_geometry_columns */
    pszSQL = sqlite3_mprintf(
                "SELECT table_name, column_name, geometry_type_name, "
                "srs_id, z "
                "FROM gpkg_geometry_columns "
                "WHERE table_name = '%q'",
                m_pszTableName);
                
    err = SQLQuery(poDb, pszSQL, &oResultGeomCols);
    sqlite3_free(pszSQL);

    /* gpkg_geometry_columns query has to work */
    /* gpkg_geometry_columns.table_name is supposed to be unique */
    if ( err != OGRERR_NONE || oResultGeomCols.nRowCount != 1 )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "%s", oResultGeomCols.pszErrMsg );
        SQLResultFree(&oResultGeomCols);
        return err;
    }

    /* Use the "PRAGMA TABLE_INFO()" call to get table definition */
    /*  #|name|type|nullable|default|pk */
    /*  0|id|integer|0||1 */
    /*  1|name|varchar|0||0 */    
    pszSQL = sqlite3_mprintf("pragma table_info('%q')", m_pszTableName);
    err = SQLQuery(poDb, pszSQL, &oResultTable);
    sqlite3_free(pszSQL);

    if ( err != OGRERR_NONE || oResultTable.nRowCount == 0 )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "%s", oResultTable.pszErrMsg );
        SQLResultFree(&oResultGeomCols);
        SQLResultFree(&oResultTable);
        return err;
    }
    
    /* Populate feature definition from table description */
    m_poFeatureDefn = new OGRFeatureDefn( m_pszTableName );
    m_poFeatureDefn->Reference();
    
    int bHasZ = SQLResultGetValueAsInteger(&oResultGeomCols, 4, 0);
    int iSrsId = SQLResultGetValueAsInteger(&oResultGeomCols, 3, 0);
    char *pszGeomColsType = SQLResultGetValue(&oResultGeomCols, 2, 0);
    int iRecord;
    OGRBoolean bFidFound = FALSE;
    m_iSrs = iSrsId;
    
    for ( iRecord = 0; iRecord < oResultTable.nRowCount; iRecord++ )
    {
        char *pszName = SQLResultGetValue(&oResultTable, 1, iRecord);
        char *pszType = SQLResultGetValue(&oResultTable, 2, iRecord);
        OGRBoolean bFid = SQLResultGetValueAsInteger(&oResultTable, 5, iRecord);
        OGRFieldType oType = GPkgFieldToOGR(pszType);

        /* Not a standard field type... */
        if ( oType > OFTMaxType )
        {
            /* Maybe it's a geometry type? */
            OGRwkbGeometryType oGeomType = GPkgGeometryTypeToWKB(pszType, bHasZ);
            if ( oGeomType != wkbNone )
            {
                OGRwkbGeometryType oGeomTypeGeomCols = GPkgGeometryTypeToWKB(pszGeomColsType, bHasZ);
                /* Enforce consistency between table and metadata */
                if ( oGeomType != oGeomTypeGeomCols )
                {
                    CPLError(CE_Failure, CPLE_AppDefined, 
                             "geometry column type in '%s.%s' is not consistent with type in gpkg_geometry_columns", 
                             m_pszTableName, pszName);
                    SQLResultFree(&oResultTable);
                    SQLResultFree(&oResultGeomCols);
                    return OGRERR_FAILURE;
                }
                
                if ( m_poFeatureDefn->GetGeomFieldCount() == 0 )
                {
                    OGRGeomFieldDefn oGeomField(pszName, oGeomType);
                    m_poFeatureDefn->AddGeomFieldDefn(&oGeomField);
                }
                else if ( m_poFeatureDefn->GetGeomFieldCount() == 1 )
                {
                    m_poFeatureDefn->GetGeomFieldDefn(0)->SetType(oGeomType);
                    m_poFeatureDefn->GetGeomFieldDefn(0)->SetName(pszName);
                }
                else
                {
                    CPLError(CE_Failure, CPLE_AppDefined, 
                             "table '%s' has geometry fields? not legal in gpkg", 
                             m_pszTableName);
                    SQLResultFree(&oResultTable);
                    SQLResultFree(&oResultGeomCols);
                    return OGRERR_FAILURE;
                }

                /* Read the SRS */
                OGRSpatialReference *poSRS = m_poDS->GetSpatialRef(iSrsId);
                if ( poSRS )
                {
                    m_poFeatureDefn->GetGeomFieldDefn(0)->SetSpatialRef(poSRS);
                }
            }
            else
            {
                // CPLError( CE_Failure, CPLE_AppDefined, "invalid field type '%s'", pszType );
                // SQLResultFree(&oResultTable);
                CPLError(CE_Warning, CPLE_AppDefined, 
                         "geometry column '%s' of type '%s' ignored", pszName, pszType);
            }
            
        }
        else
        {
            /* Is this the FID column? */
            if ( bFid )
            {
                bFidFound = TRUE;
                m_pszFidColumn = CPLStrdup(pszName);
            }
            else
            {
                OGRFieldDefn oField(pszName, oType);
                m_poFeatureDefn->AddFieldDefn(&oField);
            }
        }
    }

    /* Wait, we didn't find a FID? */
    /* Game over, all valid tables must have a FID */
    if ( ! bFidFound )
    {
        CPLError(CE_Failure, CPLE_AppDefined, 
                 "no primary key defined for table '%s'", m_pszTableName);
        return OGRERR_FAILURE;
        
    }

    m_poExtent = new OGREnvelope(oExtent);

    SQLResultFree(&oResultTable);
    SQLResultFree(&oResultGeomCols);

    return OGRERR_NONE;
}


/************************************************************************/
/*                      OGRGeoPackageLaye()                             */
/************************************************************************/

OGRGeoPackageLayer::OGRGeoPackageLayer(
                    OGRGeoPackageDataSource *poDS,
                    const char * pszTableName)
{
    m_pszTableName = CPLStrdup(pszTableName);
    m_pszFidColumn = NULL;
    m_poDS = poDS;
    m_poExtent = NULL;
    m_bExtentChanged = FALSE;
    m_poFeatureDefn = NULL;
    m_poQueryStatement = NULL;
    m_soColumns = "";
    m_soFilter = "";
}


/************************************************************************/
/*                      ~OGRGeoPackageLayer()                           */
/************************************************************************/

OGRGeoPackageLayer::~OGRGeoPackageLayer()
{
    /* Save metadata back to the database */
    SaveExtent();

    /* Clean up resources in memory */
    if ( m_pszTableName )
        CPLFree( m_pszTableName );
    
    if ( m_poExtent )
        delete m_poExtent;
    
    if ( m_poQueryStatement )
        sqlite3_finalize(m_poQueryStatement);
    
    if ( m_poFeatureDefn )
        m_poFeatureDefn->Release();
}


/************************************************************************/
/*                      CreateField()                                   */
/************************************************************************/

OGRErr OGRGeoPackageLayer::CreateField( OGRFieldDefn *poField, int bApproxOK )
{
    
    if ( ! m_poFeatureDefn || ! m_pszTableName )
    {
        CPLError(CE_Failure, CPLE_AppDefined, 
                 "feature definition or table name is null");
        return OGRERR_FAILURE;
    }

    OGRErr err = m_poDS->AddColumn(m_pszTableName, 
                                   poField->GetNameRef(),
                                   GPkgFieldFromOGR(poField->GetType()));

    if ( err != OGRERR_NONE )
        return err;
    
    m_poFeatureDefn->AddFieldDefn( poField );
    return OGRERR_NONE;
}

/************************************************************************/
/*                      CreateFeature()                                 */
/************************************************************************/

OGRErr OGRGeoPackageLayer::CreateFeature( OGRFeature *poFeature )
{
    if ( ! m_poFeatureDefn || ! m_pszTableName )
    {
        CPLError(CE_Failure, CPLE_AppDefined, 
                 "feature definition or table name is null");
        return OGRERR_FAILURE;
    }

    if( NULL == poFeature )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "NULL pointer to OGRFeature passed to CreateFeature()" );
        return OGRERR_FAILURE;
    }

    /* Construct a SQL INSERT statement, prepare it, bind */
    /* values to the parameters, and then execute */
    CPLString osCommand;
    OGRBoolean bNeedComma = FALSE;
    OGRBoolean bEmptyInsert = FALSE;
    osCommand.Printf( "INSERT INTO %s (", m_pszTableName );
    int nFieldCount = 0;
    int nBindCount = 1;

    // if( poFeature->GetFID() != OGRNullFID && pszFIDColumn != NULL )

    /* Geometry column name */
    OGRFeatureDefn *poFeatureDefn = poFeature->GetDefnRef();
    OGRGeomFieldDefn *poGeomFieldDefn = poFeatureDefn->GetGeomFieldDefn(0);
    OGRGeometry *poGeom = poFeature->GetGeomFieldRef(0);

    /* Non-null geometry, so add a slot for it in the SQL */
    if( poGeom )
    {
        osCommand += poGeomFieldDefn->GetNameRef();
        nFieldCount++;
        bNeedComma = TRUE;
    }

    /* Add attribute column names (except FID) to the SQL */
    for( int i = 0; i < poFeatureDefn->GetFieldCount(); i++ )
    {
        if( !poFeature->IsFieldSet( i ) )
            continue;

        if( !bNeedComma ) bNeedComma = TRUE;
        else osCommand += ", ";
        
        osCommand += poFeatureDefn->GetFieldDefn(i)->GetNameRef();
        nFieldCount++;
    }

    if ( !bNeedComma )
        bEmptyInsert = TRUE;

    osCommand += ") VALUES (";
    
    /* Add '?' slots to the SQL where we'll bind values later */
    for( int i = 0; i < nFieldCount; i++ )
    {
        if ( i ) osCommand += ", ";
        
        osCommand += "?";
    }
    osCommand += ")";

    /* Prepare the SQL statement */
    sqlite3 *poDb = m_poDS->GetDatabaseHandle();
    sqlite3_stmt *poStmt;
    int err = sqlite3_prepare_v2(poDb, osCommand, -1, &poStmt, NULL);
    if ( err != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "failed to prepare SQL: %s", osCommand.c_str());        
        return OGRERR_FAILURE;
    }
    
    /* Bind data values to the statement, here bind the blob for geometry */
    if ( poGeom )
    {
        GByte *pabyWkb;
        size_t szWkb;
        pabyWkb = GPkgGeometryFromOGR(poGeom, m_iSrs, &szWkb);
        err = sqlite3_bind_blob(poStmt, nBindCount++, pabyWkb, szWkb, NULL);
        if ( err != SQLITE_OK )
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "failed to bind geometry to statement");        
            return OGRERR_FAILURE;            
        }
    }
    
    /* Bind the attributes using appropriate SQLite data types */
    for( int i = 0; i < poFeatureDefn->GetFieldCount(); i++ )
    {
        if( ! poFeature->IsFieldSet(i) )
            continue;
        
        OGRFieldDefn *poFieldDefn = poFeatureDefn->GetFieldDefn(i); 
        
        switch(SQLiteFieldFromOGR(poFieldDefn->GetType()))
        {
            case SQLITE_INTEGER:
            {
                err = sqlite3_bind_int(poStmt, nBindCount++, poFeature->GetFieldAsInteger(i));
                break;
            }
            case SQLITE_FLOAT:
            {
                err = sqlite3_bind_double(poStmt, nBindCount++, poFeature->GetFieldAsDouble(i));
                break;
            }
            case SQLITE_BLOB:
            {
                int szBlob;
                GByte *pabyBlob = poFeature->GetFieldAsBinary(i, &szBlob);
                err = sqlite3_bind_blob(poStmt, nBindCount++, pabyBlob, szBlob, NULL);
                break;
            }
            default:
            {
                const char *pszVal = poFeature->GetFieldAsString(i);
                err = sqlite3_bind_text(poStmt, nBindCount++, pszVal, strlen(pszVal)+1, NULL);
                break;
            }            
        }
        
        if ( err != SQLITE_OK )
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "failed to bind attribute '%s' to statement", poFieldDefn->GetNameRef());
            return OGRERR_FAILURE;       
        }
        
        nBindCount++;
    }

    /* From here execute the statement and check errors */
    err = sqlite3_step(poStmt);
    if ( ! (err == SQLITE_OK || err == SQLITE_DONE) )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "failed to execute insert");
        return OGRERR_FAILURE;       
    }
    
    err = sqlite3_finalize(poStmt);

    /* Update the layer extents with this new object */
    OGREnvelope oEnv;
    poGeom->getEnvelope(&oEnv);
    UpdateExtent(&oEnv);

    /* Retrieve the FID value */
    char *pszSQL = sqlite3_mprintf("SELECT seq FROM sqlite_sequence WHERE name = '%q'", m_pszTableName);
    int iFidNew = SQLGetInteger(poDb, pszSQL, &err);
    sqlite3_free(pszSQL);    
    if ( err == SQLITE_OK )
        poFeature->SetFID(iFidNew);

    /* All done! */
	return OGRERR_NONE;
}

/************************************************************************/
/*                         SetAttributeFilter()                         */
/************************************************************************/

OGRErr OGRGeoPackageLayer::SetAttributeFilter( const char *pszQuery )

{
    if( pszQuery == NULL )
        m_soFilter = "";
    else
        m_soFilter = pszQuery;

    ResetReading();
    return OGRERR_NONE;
}


/************************************************************************/
/*                      ResetReading()                                  */
/************************************************************************/

void OGRGeoPackageLayer::ResetReading()
{
    if ( m_poQueryStatement )
    {
        sqlite3_finalize(m_poQueryStatement);
        m_poQueryStatement = NULL;
    }
    
    BuildColumns();
    return;
}


/************************************************************************/
/*                        GetNextFeature()                              */
/************************************************************************/

OGRFeature* OGRGeoPackageLayer::GetNextFeature()
{
    /* There is no active query statement set up, */
    /* so job #1 is to prepare the statement. */
    if ( ! m_poQueryStatement )
    {
        CPLString soSQL;
        soSQL.Printf("SELECT %s FROM %s ", m_soColumns.c_str(), m_pszTableName);
        if ( m_soFilter != "" )
            soSQL += "WHERE " + m_soFilter;
        
        int err = sqlite3_prepare(m_poDS->GetDatabaseHandle(), soSQL.c_str(), -1, &m_poQueryStatement, NULL);
        if ( err != SQLITE_OK )
        {
            m_poQueryStatement = NULL;
            CPLError( CE_Failure, CPLE_AppDefined, "failed to prepare SQL: %s", soSQL.c_str());            
            return NULL;
        }
    }
    
    while ( TRUE )
    {
        int err = sqlite3_step(m_poQueryStatement);
        
        /* Nothing left in statement? NULL return indicates to caller */
        /* that there are no features left */
        if ( err == SQLITE_DONE )
        {
            return NULL;
        }
        /* Got a row, let's read it */
        else if ( err == SQLITE_ROW )
        {
            OGRFeature *poFeature;
            
            /* Fetch the feature */
            if ( ReadFeature(m_poQueryStatement, &poFeature) != OGRERR_NONE )
                return NULL;
            
            if( (m_poFilterGeom == NULL || FilterGeometry(poFeature->GetGeometryRef()) ) &&
                (m_poAttrQuery  == NULL || m_poAttrQuery->Evaluate(poFeature)) )
            {
                return poFeature;                
            }

            /* This feature doesn't pass the filters */
            /* So delete it and loop again to try the next row */
            delete poFeature;
        }
        else 
        {
            /* Got neither a row, nor the end of the query. */
            /* Something terrible has happened, break out of loop */
            /* CPLError( CE_Failure, CPLE_AppDefined, "unable to step through query statement"); */
            return NULL;
        }

    }

}	

/************************************************************************/
/*                        GetFeature()                                  */
/************************************************************************/

OGRFeature* OGRGeoPackageLayer::GetFeature(long nFID)
{
    /* No FID, no answer. */
    if (nFID == OGRNullFID)
        return NULL;
    
    /* Clear out any existing query */
    ResetReading();

    /* No filters apply, just use the FID */
    CPLString soSQL;
    soSQL.Printf("SELECT %s FROM %s WHERE %s = '%ld'",
                 m_soColumns.c_str(), m_pszTableName, m_pszFidColumn, nFID);

    int err = sqlite3_prepare(m_poDS->GetDatabaseHandle(), soSQL.c_str(), -1, &m_poQueryStatement, NULL);
    if ( err != SQLITE_OK )
    {
        m_poQueryStatement = NULL;
        CPLError( CE_Failure, CPLE_AppDefined, "failed to prepare SQL: %s", soSQL.c_str());            
        return NULL;
    }
    
    /* Should be only one or zero results */
    err = sqlite3_step(m_poQueryStatement);
        
    /* Nothing left in statement? NULL return indicates to caller */
    /* that there are no features left */
    if ( err == SQLITE_DONE )
        return NULL;

    /* Aha, got one */
    if ( err == SQLITE_ROW )
    {
        OGRFeature *poFeature;
        
        /* Fetch the feature */
        if ( ReadFeature(m_poQueryStatement, &poFeature) != OGRERR_NONE )
            return NULL;
            
        if ( poFeature )
            return poFeature;                
        else 
            return NULL;
    }
    
    /* Error out on all other return codes */
    return NULL;
}

/************************************************************************/
/*                      GetFIDColumn()                                  */
/************************************************************************/

const char* OGRGeoPackageLayer::GetFIDColumn()
{
    if ( ! m_pszFidColumn )
        return "";
    else
        return m_pszFidColumn;
}

/************************************************************************/
/*                      TestCapability()                                */
/************************************************************************/

int OGRGeoPackageLayer::TestCapability ( const char * pszCap )
{
    if ( EQUAL(pszCap, OLCCreateField) ||
         EQUAL(pszCap, OLCSequentialWrite) ||
         EQUAL(pszCap, OLCRandomRead) )
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

