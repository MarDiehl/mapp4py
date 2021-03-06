import os
import shutil
from math import sqrt
import mapp
from mapp import dmd
import time

if os.path.exists('dumps'): shutil.rmtree('dumps')
os.mkdir('dumps')

boltz = 8.617330350e-5
planck = 4.13566766225 * 0.1 * sqrt(1.60217656535/1.66053904020)



mapp.pause_slave_out();

sim=dmd.atoms.import_cfg(5,"configs/Fe-DMD.cfg")
sim.ff_eam_fs("potentials/Fe-C.eam.fs",[2.8],[2.0])
sim.hP=planck
sim.kB=boltz
sim.temp=300.0

chem=dmd.bdf();
chem.a_tol=1.0e-7;
chem.ntally=10;
chem.max_nsteps=1;
chem.max_ngmres_iters=4;
chem.max_nnewton_iters=10;
chem.max_nsteps=4000;
chem.nreset=50;
chem.export=dmd.export_cfg('dumps/dump',10,sort=True)

start = time.time()
chem.run(sim,10.0**(30));
print "time elapsed: %lf seconds" % (time.time()-start)
