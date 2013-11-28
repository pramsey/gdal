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

