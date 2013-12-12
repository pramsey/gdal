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

    /* gpkg_contents query has to work */
    /* gpkg_contents.table_name is supposed to be unique */
    if ( err != OGRERR_NONE || oResultContents.nRowCount != 1 )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "%s", oResultContents.pszErrMsg );
        SQLResultFree(&oResultContents);
        return err;        
    }

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
        SQLResultFree(&oResultContents);
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
        SQLResultFree(&oResultContents);
        SQLResultFree(&oResultGeomCols);
        SQLResultFree(&oResultTable);
        return err;
    }
    
    /* Populate feature definition from table description */
    m_poFeatureDefn = new OGRFeatureDefn( m_pszTableName );
    m_poFeatureDefn->Reference();
    
    int bHasZ = atoi(SQLResultGetValue(&oResultGeomCols, 4, 0));
    int iSrsId = atoi(SQLResultGetValue(&oResultGeomCols, 3, 0));
    char *pszGeomColsType = SQLResultGetValue(&oResultGeomCols, 2, 0);
    int iRecord;
    for ( iRecord = 0; iRecord < oResultTable.nRowCount; iRecord++ )
    {
        char *pszName = SQLResultGetValue(&oResultTable, 1, iRecord);
        char *pszType = SQLResultGetValue(&oResultTable, 2, iRecord);
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
                             "geometry column type in table and gpkg_geometry_columns is inconsistent");
                    SQLResultFree(&oResultContents);
                    SQLResultFree(&oResultGeomCols);
                    SQLResultFree(&oResultTable);
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
                             "feature definition already has mutliple geometry fields?!?");
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
            OGRFieldDefn oField(pszName, oType);
            m_poFeatureDefn->AddFieldDefn(&oField);
        }
    }

    char *pszMinX = SQLResultGetValue(&oResultContents, 5, 0);
    char *pszMinY = SQLResultGetValue(&oResultContents, 6, 0);
    char *pszMaxX = SQLResultGetValue(&oResultContents, 7, 0);
    char *pszMaxY = SQLResultGetValue(&oResultContents, 8, 0);
    
	/* All the extrema have to be non-NULL for this to make sense */
    if ( pszMinX && pszMinY && pszMaxX && pszMaxY )
    {
        m_poExtent = new OGREnvelope();
        m_poExtent->MinX = atof(pszMinX);
        m_poExtent->MinY = atof(pszMinY);
        m_poExtent->MaxX = atof(pszMaxX);
        m_poExtent->MaxY = atof(pszMaxY);
    }

    SQLResultFree(&oResultContents);
    SQLResultFree(&oResultGeomCols);
    SQLResultFree(&oResultTable);

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
    m_poDS = poDS;
    m_poExtent = NULL;
    m_poFeatureDefn = NULL;
}


/************************************************************************/
/*                      ~OGRGeoPackageLayer()                           */
/************************************************************************/

OGRGeoPackageLayer::~OGRGeoPackageLayer()
{
    if ( m_pszTableName )
        CPLFree( m_pszTableName );
        
    if ( m_poExtent )
        delete m_poExtent;
    
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

OGRErr OGRGeoPackageLayer::CreateFeature( OGRFeature *poFeater )
{


	return OGRERR_NONE;
}

/************************************************************************/
/*                      TestCapability()                                */
/************************************************************************/

int OGRGeoPackageLayer::TestCapability ( const char * pszCap )
{
    if ( EQUAL(pszCap, OLCCreateField) )
    {
         return TRUE;
    }
    if ( EQUAL(pszCap, OLCSequentialWrite) )
    {
         return TRUE;
    }
    return FALSE;
}