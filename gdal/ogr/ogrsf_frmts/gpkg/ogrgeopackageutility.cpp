/******************************************************************************
 * $Id$
 *
 * Project:  GeoPackage Translator
 * Purpose:  Utility functions for OGR GeoPackage driver.
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
 
 
#include "ogrgeopackageutility.h"


/* Runs a SQL command and ignores the result (good for INSERT/UPDATE/CREATE) */
OGRErr SQLCommand(sqlite3 * poDb, const char * pszSQL)
{
    CPLAssert( poDb != NULL );
    CPLAssert( pszSQL != NULL );

    char *pszErrMsg = NULL;
    int rc = sqlite3_exec(poDb, pszSQL, NULL, NULL, &pszErrMsg);
    
    if ( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "sqlite3_exec(%s) failed: %s",
                  pszSQL, pszErrMsg );
        return OGRERR_FAILURE;
    }
    
    return OGRERR_NONE;
}


OGRErr SQLResultInit(SQLResult * poResult)
{
    poResult->papszResult = NULL;
    poResult->pszErrMsg = NULL;
    poResult->nRowCount = 0;
    poResult->nColCount = 0;
    poResult->rc = 0;
    return OGRERR_NONE;
}


OGRErr SQLQuery(sqlite3 * poDb, const char * pszSQL, SQLResult * poResult)
{
    CPLAssert( poDb != NULL );
    CPLAssert( pszSQL != NULL );
    CPLAssert( poResult != NULL );

    SQLResultInit(poResult);

    poResult->rc = sqlite3_get_table(
        poDb, pszSQL,
        &(poResult->papszResult), 
        &(poResult->nRowCount), 
        &(poResult->nColCount), 
        &(poResult->pszErrMsg) );
    
    if( poResult->rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_AppDefined, 
                  "sqlite3_get_table(%s) failed: %s", pszSQL, poResult->pszErrMsg );
        return OGRERR_FAILURE;
    }

    return OGRERR_NONE;
}


OGRErr SQLResultFree(SQLResult * poResult)
{
    if ( poResult->papszResult )
        sqlite3_free_table(poResult->papszResult);

    if ( poResult->pszErrMsg )
        sqlite3_free(poResult->pszErrMsg);
        
    return OGRERR_NONE;
}

char* SQLResultGetColumn(const SQLResult * poResult, int iColNum)
{
    if ( ! poResult ) 
        return NULL;
        
    if ( iColNum < 0 || iColNum >= poResult->nColCount )
        return NULL;
    
    return poResult->papszResult[iColNum];
}

char* SQLResultGetValue(const SQLResult * poResult, int iColNum, int iRowNum)
{
    int nCols = poResult->nColCount;
    int nRows = poResult->nRowCount;
    
    if ( ! poResult ) 
        return NULL;
        
    if ( iColNum < 0 || iColNum >= nCols )
        return NULL;

    if ( iRowNum < 0 || iRowNum >= nRows )
        return NULL;
        
    return poResult->papszResult[ nCols + iRowNum * nCols + iColNum ];
}

/* Returns the first row of first column of SQL as integer */
int SQLGetInteger(sqlite3 * poDb, const char * pszSQL, OGRErr *err)
{
    CPLAssert( poDb != NULL );
    
    sqlite3_stmt *poStmt;
    int rc, i;
    
    /* Prepare the SQL */
    rc = sqlite3_prepare_v2(poDb, pszSQL, strlen(pszSQL), &poStmt, NULL);
    if ( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "sqlite3_prepare_v2(%s) failed: %s",
                  pszSQL, sqlite3_errmsg( poDb ) );
        if ( err ) *err = OGRERR_FAILURE;
        return 0;
    }
    
    /* Execute and fetch first row */
    rc = sqlite3_step(poStmt);
    if ( rc != SQLITE_ROW )
    {
        if ( err ) *err = OGRERR_FAILURE;
        return 0;
    }
    
    /* Read the integer from the row */
    i = sqlite3_column_int(poStmt, 0);
    sqlite3_finalize(poStmt);
    
    if ( err ) *err = OGRERR_NONE;
    return i;
}


/* Requirement 20: A GeoPackage SHALL store feature table geometries */
/* with the basic simple feature geometry types (Geometry, Point, */
/* LineString, Polygon, MultiPoint, MultiLineString, MultiPolygon, */
/* GeomCollection) */
/* http://opengis.github.io/geopackage/#geometry_types */
OGRwkbGeometryType GPkgGeometryTypeToWKB(const char *pszGpkgType, int bHasZ)
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
const char* GPkgGeometryTypeFromWKB(OGRwkbGeometryType oType)
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
OGRFieldType GPkgFieldToOGR(const char *pszGpkgType)
{
    /* Integer types */
    if ( STRNCASECMP("INTEGER", pszGpkgType, 7) == 0 )
        return OFTInteger;
    else if ( STRNCASECMP("INT", pszGpkgType, 3) == 0 )
        return OFTInteger;
    else if ( STRNCASECMP("MEDIUMINT", pszGpkgType, 9) == 0 )
        return OFTInteger;
    else if ( STRNCASECMP("SMALLINT", pszGpkgType, 8) == 0 )
        return OFTInteger;
    else if ( STRNCASECMP("TINYINT", pszGpkgType, 7) == 0 )
        return OFTInteger;
    else if ( STRNCASECMP("BOOLEAN", pszGpkgType, 7) == 0 )
        return OFTInteger;

    /* Real types */
    else if ( STRNCASECMP("FLOAT", pszGpkgType, 5) == 0 )
        return OFTReal;
    else if ( STRNCASECMP("DOUBLE", pszGpkgType, 6) == 0 )
        return OFTReal;
    else if ( STRNCASECMP("REAL", pszGpkgType, 4) == 0 )
        return OFTReal;
        
    /* String/binary types */
    else if ( STRNCASECMP("TEXT", pszGpkgType, 4) == 0 )
        return OFTString;
    else if ( STRNCASECMP("BLOB", pszGpkgType, 4) == 0 )
        return OFTBinary;
        
    /* Date types */
    else if ( STRNCASECMP("DATE", pszGpkgType, 4) == 0 )
        return OFTDate;
    else if ( STRNCASECMP("DATETIME", pszGpkgType, 8) == 0 )
        return OFTDateTime;

    /* Illegal! */
    else 
        return (OGRFieldType)(OFTMaxType + 1);
}

/* Requirement 5: The columns of tables in a GeoPackage SHALL only be */
/* declared using one of the data types specified in table GeoPackage */
/* Data Types. */
/* http://opengis.github.io/geopackage/#table_column_data_types */
const char* GPkgFieldFromOGR(OGRFieldType nType)
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

/* Requirement 19: A GeoPackage SHALL store feature table geometries 
*  with or without optional elevation (Z) and/or measure (M) values in SQL 
*  BLOBs using the Standard GeoPackageBinary format specified in table GeoPackage 
*  SQL Geometry Binary Format and clause Geometry Encoding.
*
*   GeoPackageBinaryHeader {
*     byte[2] magic = 0x4750; 
*     byte version;           
*     byte flags;             
*     int32 srs_id;
*     double[] envelope;      
*    }
* 
*   StandardGeoPackageBinary {
*     GeoPackageBinaryHeader header; 
*     WKBGeometry geometry;          
*   }
*/

// OGRGeometry->IsEmpty() for empty test

// static size_t GPkgGeometrySize(const OGRGeometry *poGeom) 
// {
//     static size_t szHeader = 2 + 1 + 1 + 4 
//     size_t szGPkg = 
// }
//     
// GByte* GPkgGeometryFromOGC(const OGRGeometry *poGeom)
// {
//     
// }

