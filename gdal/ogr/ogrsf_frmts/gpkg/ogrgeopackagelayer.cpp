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


/* Requirement 20: A GeoPackage SHALL store feature table geometries */
/* with the basic simple feature geometry types (Geometry, Point, */
/* LineString, Polygon, MultiPoint, MultiLineString, MultiPolygon, */
/* GeomCollection) */
/* http://opengis.github.io/geopackage/#geometry_types */
OGRwkbGeometryType OGRGeoPackageLayer::GetOGRGeometryType(const char *pszGpkgType, int bHasZ)
{
    OGRwkbGeometryType oType;
    
    if ( EQUAL("Geometry", pszGpkgType) )
        oType = wkbUnknown;
    else if ( EQUAL("Point", pszGpkgType) )
        oType =  wkbPoint;
    else if ( EQUAL("LineString", pszGpkgType) )
        oType =  wkbLineString;
    else if ( EQUAL("Polygon", pszGpkgType) )
        oType =  wkbPolygon;
    else if ( EQUAL("MultiPoint", pszGpkgType) )
        oType =  wkbMultiPoint;
    else if ( EQUAL("MultiLineString", pszGpkgType) )
        oType =  wkbMultiLineString;
    else if ( EQUAL("MultiPolygon", pszGpkgType) )
        oType =  wkbMultiPolygon;
    else if ( EQUAL("GeometryCollection", pszGpkgType) )
        oType =  wkbGeometryCollection;
    else
        oType =  wkbNone;

    if ( (oType != wkbNone) && bHasZ )
    {
        unsigned int oi = oType;
        oi &= wkb25DBit;
        oType = (OGRwkbGeometryType)oi;
    }

    return oType;
}

/* Requirement 20: A GeoPackage SHALL store feature table geometries */
/* with the basic simple feature geometry types (Geometry, Point, */
/* LineString, Polygon, MultiPoint, MultiLineString, MultiPolygon, */
/* GeomCollection) */
/* http://opengis.github.io/geopackage/#geometry_types */
const char* OGRGeoPackageLayer::GetGpkgGeometryType(OGRwkbGeometryType oType)
{
    oType = wkbFlatten(oType);
    
    switch(oType)
    {
        case wkbPoint:
            return "point";
        case wkbLineString:
            return "linestring";
        case wkbPolygon:
            return "polygon";
        case wkbMultiPoint:
            return "multipoint";
        case wkbMultiLineString:
            return "multilinestring";
        case wkbMultiPolygon:
            return "multipolygon";
        case wkbGeometryCollection:
            return "geometrycollection";
        default:
            return NULL;
    }
}

/* Requirement 5: The columns of tables in a GeoPackage SHALL only be */
/* declared using one of the data types specified in table GeoPackage */
/* Data Types. */
/* http://opengis.github.io/geopackage/#table_column_data_types */
OGRFieldType OGRGeoPackageLayer::GetOGRFieldType(const char *pszGpkgType)
{
    /* Integer types */
    if ( STRNCASECMP("INTEGER", pszGpkgType, 7) )
        return OFTInteger;
    else if ( STRNCASECMP("INT", pszGpkgType, 3) )
        return OFTInteger;
    else if ( STRNCASECMP("MEDIUMINT", pszGpkgType, 9) )
        return OFTInteger;
    else if ( STRNCASECMP("SMALLINT", pszGpkgType, 8) )
        return OFTInteger;
    else if ( STRNCASECMP("TINYINT", pszGpkgType, 7) )
        return OFTInteger;
    else if ( STRNCASECMP("BOOLEAN", pszGpkgType, 7) )
        return OFTInteger;

    /* Real types */
    else if ( STRNCASECMP("FLOAT", pszGpkgType, 5) )
        return OFTReal;
    else if ( STRNCASECMP("DOUBLE", pszGpkgType, 6) )
        return OFTReal;
    else if ( STRNCASECMP("REAL", pszGpkgType, 4) )
        return OFTReal;
        
    /* String/binary types */
    else if ( STRNCASECMP("TEXT", pszGpkgType, 4) )
        return OFTString;
    else if ( STRNCASECMP("BLOB", pszGpkgType, 4) )
        return OFTBinary;
        
    /* Date types */
    else if ( STRNCASECMP("DATE", pszGpkgType, 4) )
        return OFTDate;
    else if ( STRNCASECMP("DATETIME", pszGpkgType, 8) )
        return OFTDateTime;

    /* Illegal! */
    else 
        return (OGRFieldType)(OFTMaxType + 1);
}

/* Requirement 5: The columns of tables in a GeoPackage SHALL only be */
/* declared using one of the data types specified in table GeoPackage */
/* Data Types. */
/* http://opengis.github.io/geopackage/#table_column_data_types */
const char* OGRGeoPackageLayer::GetGPkgFieldType(OGRFieldType nType)
{
    switch(nType)
    {
        case OFTInteger:
            return "INTEGER";
        case OFTReal:
            return "REAL";
        case OFTString:
            return "TEXT";
        case OFTBinary:
            return "BLOB";
        case OFTDate:
            return "DATE";
        case OFTDateTime:
            return "DATETIME";
        default:
            return NULL;
    }
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

    if ( err != OGRERR_NONE || oResultTable.nRowCount > 0 )
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
        OGRFieldType oType = GetOGRFieldType(pszType);

        /* Not a standard field type... */
        if ( oType > OFTMaxType )
        {
            /* Maybe it's a geometry type? */
            OGRwkbGeometryType oGeomType = GetOGRGeometryType(pszType, bHasZ);
            if ( oGeomType != wkbNone )
            {
                OGRwkbGeometryType oGeomTypeGeomCols = GetOGRGeometryType(pszGeomColsType, bHasZ);
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
                OGRGeomFieldDefn oGeomField(pszName, oGeomType);
                
                /* Read the SRS */
                OGRSpatialReference *poSRS = m_poDS->GetSpatialRef(iSrsId);
                if ( poSRS )
                    oGeomField.SetSpatialRef(poSRS);
                
                /* Add geometry type (only one per table, per GPKG rules */
                m_poFeatureDefn->AddGeomFieldDefn(&oGeomField);
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
    {
        m_poFeatureDefn->Release();
    }
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
                                   GetGPkgFieldType(poField->GetType()));

    if ( err != OGRERR_NONE )
        return err;
    
    m_poFeatureDefn->AddFieldDefn( poField );
    return OGRERR_NONE;
}
