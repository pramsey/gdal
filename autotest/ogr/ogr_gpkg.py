#!/usr/bin/env python
# -*- coding: utf-8 -*-
###############################################################################
# $Id$
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Test GeoPackage driver functionality.
# Author:   Paul Ramsey <pramsey@boundlessgeom.com>
# 
###############################################################################
# Copyright (c) 2004, Paul Ramsey <pramsey@boundlessgeom.com>
# 
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
###############################################################################

import os
import sys
import string
import shutil

# Make sure we run from the directory of the script
if os.path.basename(sys.argv[0]) == os.path.basename(__file__):
    if os.path.dirname(sys.argv[0]) != '':
        os.chdir(os.path.dirname(sys.argv[0]))

sys.path.append( '../pymod' )

from osgeo import ogr, osr, gdal
import gdaltest
import ogrtest

###############################################################################
# Create a fresh database.

def ogr_gpkg_1():

    gdaltest.gpkg_ds = None

    print("in test 1");

    try:
        gpkg_dr = ogr.GetDriverByName( 'GPKG' )
        print("gpkg_dr is")
        print(gpkg_dr)
        if gpkg_dr is None:
            return 'skip'
    except:
        return 'skip'

    print("done loading driver")

    try:
        os.remove( 'tmp/gpkg_test.gpkg' )
    except:
        pass

    # This is to speed-up the runtime of tests on EXT4 filesystems
    # Do not use this for production environment if you care about data safety
    # w.r.t system/OS crashes, unless you know what you are doing.
    #gdal.SetConfigOption('OGR_SQLITE_SYNCHRONOUS', 'OFF')

    gdaltest.gpkg_ds = gpkg_dr.CreateDataSource( 'tmp/gpkg_test.gpkg' )

    if gdaltest.gpkg_ds is not None:
        return 'success'
    else:
        return 'fail'


###############################################################################


gdaltest_list = [ 
    ogr_gpkg_1,
]

if __name__ == '__main__':

    gdaltest.setup_run( 'ogr_gpkg' )

    gdaltest.run_tests( gdaltest_list )

    gdaltest.summarize()

