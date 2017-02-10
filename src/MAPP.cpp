#include "MAPP.h"
#include <Python/Python.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#define PY_ARRAY_UNIQUE_SYMBOL ARRAY_API
#include <numpy/arrayobject.h>
#include <mpi.h>
#include "comm.h"
#include "example.h"
#include "atoms_styles.h"
#include "md_styles.h"
#include "min_styles.h"
#include "read_styles.h"
#define GET_FILE(file_name) reinterpret_cast<PyFileObject*>(PySys_GetObject((char*)#file_name))->f_fp
using namespace MAPP_NS;
/*--------------------------------------------*/
PyMethodDef MAPP::methods[]={[0 ... 4]={NULL}};
/*--------------------------------------------*/
void MAPP::setup_methods()
{
    methods[0].ml_name="pause_slave_out";
    methods[0].ml_meth=(PyCFunction)pause_out;
    methods[0].ml_flags=METH_NOARGS;
    methods[0].ml_doc="pauses stdout & stderr of non-root processes";
    
    methods[1].ml_name="resume_slave_out";
    methods[1].ml_meth=(PyCFunction)resume_out;
    methods[1].ml_flags=METH_NOARGS;
    methods[1].ml_doc="resumes stdout & stderr of non-root processes";
    
    ReadCFGMD::ml_cfg_md(methods[2]);
    ReadCFGDMD::ml_cfg_dmd(methods[3]);
}
/*--------------------------------------------
 
 --------------------------------------------*/
PyObject* MAPP::pause_out(PyObject* self)
{
    MPI_Barrier(MPI_COMM_WORLD);
    if(!glbl_rank) Py_RETURN_NONE;
    if(!__devnull__)
        __devnull__=fopen(devnull_path,"w");

    GET_FILE(stdout)=__devnull__;
    GET_FILE(stderr)=__devnull__;
    mapp_out=__devnull__;
    mapp_err=__devnull__;
    Py_RETURN_NONE;
}
/*--------------------------------------------
 
 --------------------------------------------*/
PyObject* MAPP::resume_out(PyObject*)
{
    MPI_Barrier(MPI_COMM_WORLD);
    GET_FILE(stdout)=__stdout__;
    GET_FILE(stderr)=__stderr__;
    mapp_out=__stdout__;
    mapp_err=__stderr__;
    if(__devnull__)
    {
        fclose(__devnull__);
        __devnull__=NULL;
    }
    Py_RETURN_NONE;
}
/*--------------------------------------------*/
const char* MAPP::devnull_path;
int MAPP::glbl_rank=0;
FILE* MAPP_NS::MAPP::__devnull__(NULL);
FILE* MAPP_NS::MAPP::__stdout__(NULL);
FILE* MAPP_NS::MAPP::__stderr__(NULL);
FILE* MAPP_NS::MAPP::mapp_out(NULL);
FILE* MAPP_NS::MAPP::mapp_err(NULL);
/*--------------------------------------------*/
void MAPP::init_module(void)
{
    PyObject* posixpath=PyImport_ImportModule("posixpath");
    PyObject* devnull_path_op=PyObject_GetAttrString(posixpath,"devnull");
    devnull_path=PyString_AsString(devnull_path_op);
    Py_DECREF(devnull_path_op);
    Py_DECREF(posixpath);
    glbl_rank=Communication::get_rank();
    
    import_array();
    setup_methods();
    PyObject* module=Py_InitModule3("mapp",methods,"MIT Atomistic Parallel Package");
    if(module==NULL) return;
    
    mapp_out=__stdout__=GET_FILE(stdout);
    mapp_err=__stderr__=GET_FILE(stderr);
    

    
    MAPP_MPI::setup_tp();
    if(PyType_Ready(&MAPP_MPI::TypeObject)<0) return;
    Py_INCREF(&MAPP_MPI::TypeObject);
    PyModule_AddObject(module,"mpi",reinterpret_cast<PyObject*>(&MAPP_MPI::TypeObject));
    
    
    Atoms::setup_tp();
    if(PyType_Ready(&Atoms::TypeObject)<0) return;
    Py_INCREF(&Atoms::TypeObject);
    PyModule_AddObject(module,"atoms",reinterpret_cast<PyObject*>(&Atoms::TypeObject));
    
    AtomsMD::setup_tp();
    if(PyType_Ready(&AtomsMD::TypeObject)<0) return;
    Py_INCREF(&AtomsMD::TypeObject);
    PyModule_AddObject(module,"atoms_md",reinterpret_cast<PyObject*>(&AtomsMD::TypeObject));
    
    AtomsDMD::setup_tp();
    if(PyType_Ready(&AtomsDMD::TypeObject)<0) return;
    Py_INCREF(&AtomsDMD::TypeObject);
    PyModule_AddObject(module,"atoms_dmd",reinterpret_cast<PyObject*>(&AtomsDMD::TypeObject));
    
    MDNVT::setup_tp();
    if(PyType_Ready(&MDNVT::TypeObject)<0) return;
    Py_INCREF(&MDNVT::TypeObject);
    PyModule_AddObject(module,"md_nvt",reinterpret_cast<PyObject*>(&MDNVT::TypeObject));
    
    
    MDNST::setup_tp();
    if(PyType_Ready(&MDNST::TypeObject)<0) return;
    Py_INCREF(&MDNST::TypeObject);
    PyModule_AddObject(module,"md_nst",reinterpret_cast<PyObject*>(&MDNST::TypeObject));
    
    
    MDMuVT::setup_tp();
    if(PyType_Ready(&MDMuVT::TypeObject)<0) return;
    Py_INCREF(&MDMuVT::TypeObject);
    PyModule_AddObject(module,"md_muvt",reinterpret_cast<PyObject*>(&MDMuVT::TypeObject));
    
    
    MinCG::setup_tp();
    if(PyType_Ready(&MinCG::TypeObject)<0) return;
    Py_INCREF(&MinCG::TypeObject);
    PyModule_AddObject(module,"min_cg",reinterpret_cast<PyObject*>(&MinCG::TypeObject));
    
    
    MinLBFGS::setup_tp();
    if(PyType_Ready(&MinLBFGS::TypeObject)<0) return;
    Py_INCREF(&MinLBFGS::TypeObject);
    PyModule_AddObject(module,"min_lbfgs",reinterpret_cast<PyObject*>(&MinLBFGS::TypeObject));
    
    
    MinCGDMD::setup_tp();
    if(PyType_Ready(&MinCGDMD::TypeObject)<0) return;
    Py_INCREF(&MinCGDMD::TypeObject);
    PyModule_AddObject(module,"min_cg_dmd",reinterpret_cast<PyObject*>(&MinCGDMD::TypeObject));
    
    ExamplePython::setup_tp();
    if(PyType_Ready(&ExamplePython::TypeObject)<0) return;
    Py_INCREF(&ExamplePython::TypeObject);
    PyModule_AddObject(module,"xmpl",reinterpret_cast<PyObject*>(&ExamplePython::TypeObject));
                          
}