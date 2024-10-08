/*-------------------------------------------------------------------------
 *
 * The Multicorn Foreign Data Wrapper allows you to fetch foreign data in
 * Python in your PostgreSQL.
 *
 * This module contains helpers meant to be called from python code.
 *
 * This software is released under the postgresql licence
 *
 *-------------------------------------------------------------------------
 */
#include <Python.h>
#include "postgres.h"
#include "multicorn.h"
#include "miscadmin.h"


struct module_state
{
	PyObject   *error;
};

#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))

static PyObject *
log_to_postgres(PyObject *self, PyObject *args, PyObject *kwargs)
{
	char	   *message = NULL;
	char	   *hintstr = NULL,
			   *detailstr = NULL;
	int			level = 1;
	int			severity;
	PyObject   *hint,
			   *p_message,
			   *detail;

	if (!PyArg_ParseTuple(args, "O|i", &p_message, &level))
	{
		errorCheck();
		Py_INCREF(Py_None);
		return Py_None;
	}
	if (PyBytes_Check(p_message))
	{
		message = PyBytes_AsString(p_message);
	}
	else if (PyUnicode_Check(p_message))
	{
		message = strdup(PyUnicode_AsPgString(p_message));
	}
	else
	{

		PyObject   *temp = PyObject_Str(p_message);

		errorCheck();
		message = strdup(PyString_AsString(temp));
		errorCheck();
		Py_DECREF(temp);
	}
	switch (level)
	{
		case 0:
			severity = DEBUG1;
			break;
		case 1:
			severity = NOTICE;
			break;
		case 2:
			severity = WARNING;
			break;
		case 3:
			severity = ERROR;
			break;
		case 4:
			severity = FATAL;
			break;
		default:
			severity = INFO;
			break;
	}
	hint = PyDict_GetItemString(kwargs, "hint");
	detail = PyDict_GetItemString(kwargs, "detail");

	if (errstart(severity, TEXTDOMAIN))
	{
		errmsg("%s", message);
		if (hint != NULL && hint != Py_None)
		{
			hintstr = PyString_AsString(hint);
			errhint("%s", hintstr);
		}
		if (detail != NULL && detail != Py_None)
		{
			detailstr = PyString_AsString(detail);
			errdetail("%s", detailstr);
		}
		Py_DECREF(args);
		Py_DECREF(kwargs);
		errfinish(__FILE__, __LINE__, PG_FUNCNAME_MACRO);
	}
	else
	{
		Py_DECREF(args);
		Py_DECREF(kwargs);
	}
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
py_check_interrupts(PyObject *self, PyObject *args, PyObject *kwargs)
{
	CHECK_FOR_INTERRUPTS();
	Py_INCREF(Py_None);
	return Py_None;
}


#pragma GCC diagnostic push
/* The CPython documentation requires the casting. */
#pragma GCC diagnostic ignored "-Wcast-function-type"
static PyMethodDef UtilsMethods[] = {
	{"_log_to_postgres", (PyCFunction) log_to_postgres, METH_VARARGS | METH_KEYWORDS, "Log to postresql client"},
	{"check_interrupts", (PyCFunction) py_check_interrupts, METH_VARARGS | METH_KEYWORDS, "Gives control back to PostgreSQL"},
	{NULL, NULL, 0, NULL}
};
#pragma GCC diagnostic pop

static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	"multicorn._utils",
	NULL,
	sizeof(struct module_state),
	UtilsMethods,
	NULL,
	NULL,
	NULL,
	NULL
};

#define INITERROR return NULL

#pragma GCC diagnostic push
/* Useless message. */
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
/* Presumably st is a placeholder for something? */
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
PyObject *
PyInit__utils(void)
{
	PyObject   *module = PyModule_Create(&moduledef);
	struct module_state *st;

	if (module == NULL)
		INITERROR;
	st = GETSTATE(module);

	return module;
}
#pragma GCC diagnostic pop
