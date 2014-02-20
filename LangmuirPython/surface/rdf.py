# -*- coding: utf-8 -*-
"""
@author: adam
"""
import langmuir as lm
import numpy as np
import argparse
import sys
import os

desc = """
Perform RDF on surface or KPFM image.  This script takes some time.
"""

def get_arguments(args=None):
    parser = argparse.ArgumentParser()
    parser.description = desc
    parser.add_argument(dest='ifile', default='image.pkl', type=str,
        nargs='?', metavar='input', help='input file')
    parser.add_argument('--stub', default='', type=str, metavar='stub',
        help='output file stub')
    opts = parser.parse_args(args)
    if not os.path.exists(opts.ifile):
        parser.print_help()
        print >> sys.stderr, '\nfile does not exist: %s' % opts.ifile
        sys.exit(-1)
    return opts

if __name__ == '__main__':
    work = os.getcwd()
    opts = get_arguments()

    image = lm.surface.load(opts.ifile)
    image = image / np.sum(image)

    xsize = image.shape[0]
    ysize = image.shape[1]
    zsize = image.shape[2]
    grid  = lm.grid.Grid(xsize, ysize, zsize)
    mesh  = lm.grid.PrecalculatedMesh(grid)

    w1 = np.zeros(grid.shape)
    w2 = np.zeros(grid.shape)
    xi, yi, zi = mesh.mxi, mesh.myi, mesh.mzi
    for i, ((xj, yj, zj), vi) in enumerate(np.ndenumerate(image)):
        dx = abs(xj - xi)
        dy = abs(yj - yi)
        dz = abs(zj - zi)
        w1[dx,dy,dz] += image[xi,yi,zi]
        w2[dx,dy,dz] += image[xj,yj,zj]
        if i % 100 == 0:
            print '%.5f %%' % ((i + 1)/float(grid.size))
    print '%.5f %%' % (1)

    handle = lm.common.format_output(stub=opts.stub, name='rdf', ext='pkl')
    handle = lm.common.save_pkl(mesh.r1, handle)
    handle = lm.common.save_pkl(w1, handle)
    handle = lm.common.save_pkl(w2, handle)
    print 'saved: %s {r1, w1, w2}' % handle.name