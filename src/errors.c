/*-------------------------------------------------------------------------
 *
 * The Multicorn Foreign Data Wrapper allows you to fetch foreign data in
 * Python in your PostgreSQL server.
 *
 * This module contains error handling functions.
 *
 * This software is released under the postgresql licence
 *
 *-------------------------------------------------------------------------
 */
#include "multicorn.h"
#include "bytesobject.h"
#include "access/xact.h"

void reportException(PyObject *pErrType,
				PyObject *pErrValue,
				PyObject *pErrTraceback);

void reportMulticornException(PyObject *pErrValue);

PGDLLEXPORT void
errorCheck()
{
	PyObject   *pErrType,
			   *pErrValue,
			   *pErrTraceback;

	PyErr_Fetch(&pErrType, &pErrValue, &pErrTraceback);
	if (pErrType)
	{
		// if the error value has a property _is_multicorn_exception and a boolean value True, then we don't report the
		// error as a generic exception with a stack trace -- instead we just take the message, code(severity), hint,
		// and detail, and log it to Postgres.  These exceptions are generated in utils.py to intercept ERROR/FATAL log
		// messages.  So, first detect whether that's the case, and call a new reporting function...
		PyObject *is_multicorn_exception = PyObject_GetAttrString(pErrValue, "_is_multicorn_exception");
		if (is_multicorn_exception != NULL && PyObject_IsTrue(is_multicorn_exception))
		{
			Py_DECREF(is_multicorn_exception);
			Py_DECREF(pErrType);
			Py_DECREF(pErrTraceback);
			reportMulticornException(pErrValue);
		}
		else
		{
			reportException(pErrType, pErrValue, pErrTraceback);
		}
	}
}

void
reportException(PyObject *pErrType, PyObject *pErrValue, PyObject *pErrTraceback)
{
	char	   *errName,
			   *errValue,
			   *errTraceback = "";
	PyObject   *traceback_list;
	PyObject   *pTemp;
	PyObject   *tracebackModule = PyImport_ImportModule("traceback");
	PyObject   *format_exception = PyObject_GetAttrString(tracebackModule, "format_exception");
	PyObject   *newline = PyString_FromString("\n");
	int			severity;

	PyErr_NormalizeException(&pErrType, &pErrValue, &pErrTraceback);
	pTemp = PyObject_GetAttrString(pErrType, "__name__");
	errName = PyString_AsString(pTemp);
	errValue = PyString_AsString(PyObject_Str(pErrValue));
	if (pErrTraceback != NULL)
	{
		traceback_list = PyObject_CallFunction(format_exception, "(O,O,O)", pErrType, pErrValue, pErrTraceback);
		errTraceback = PyString_AsString(PyObject_CallMethod(newline, "join", "(O)", traceback_list));
		Py_DECREF(pErrTraceback);
		Py_DECREF(traceback_list);
	}

	if (IsAbortedTransactionBlockState())
	{
		severity = WARNING;
	}
	else
	{
		severity = ERROR;
	}
#if PG_VERSION_NUM >= 130000
	if (errstart(severity, TEXTDOMAIN))
#else
	if (errstart(severity, __FILE__, __LINE__, PG_FUNCNAME_MACRO, TEXTDOMAIN))
#endif
	{
#if PG_VERSION_NUM >= 130000
		if (errstart(severity, TEXTDOMAIN))
#else
		if (errstart(severity, __FILE__, __LINE__, PG_FUNCNAME_MACRO, TEXTDOMAIN))
#endif
			errmsg("Error in python: %s", errName);
		errdetail("%s", errValue);
		errdetail_log("%s", errTraceback);
	}
	Py_DECREF(pErrType);
	Py_DECREF(pErrValue);
	Py_DECREF(format_exception);
	Py_DECREF(tracebackModule);
	Py_DECREF(newline);
	Py_DECREF(pTemp);
#if PG_VERSION_NUM >= 130000
		errfinish(__FILE__, __LINE__, PG_FUNCNAME_MACRO);
#else
		errfinish(0);
#endif
}

void reportMulticornException(PyObject* pErrValue)
{
	int severity;
	PyObject *message = PyObject_GetAttrString(pErrValue, "message");
	PyObject *hint = PyObject_GetAttrString(pErrValue, "hint");
	PyObject *detail = PyObject_GetAttrString(pErrValue, "detail");
	PyObject *code = PyObject_GetAttrString(pErrValue, "code");
	int level = PyLong_AsLong(code);

	// Matches up with REPORT_CODES in utils.py
	switch (level)
	{
		case 3:
			severity = ERROR;
			break;
		default:
		case 4:
			severity = FATAL;
			break;
	}

	PG_TRY();
	{

	#if PG_VERSION_NUM >= 130000
		if (errstart(severity, TEXTDOMAIN))
	#else
		if (errstart(severity, __FILE__, __LINE__, PG_FUNCNAME_MACRO, TEXTDOMAIN))
	#endif
		{
			errmsg("%s", PyString_AsString(message));
			if (hint != NULL && hint != Py_None)
			{
				char* hintstr = PyString_AsString(hint);
				errhint("%s", hintstr);
			}
			if (detail != NULL && detail != Py_None)
			{
				char* detailstr = PyString_AsString(detail);
				errdetail("%s", detailstr);
			}
	#if PG_VERSION_NUM >= 130000
			errfinish(__FILE__, __LINE__, PG_FUNCNAME_MACRO);
	#else
			errfinish(0);
	#endif
		}

	}
	PG_CATCH();
	{
		Py_DECREF(message);
		Py_DECREF(hint);
		Py_DECREF(detail);
		Py_DECREF(code);
		Py_DECREF(pErrValue);
		PG_RE_THROW();
	}
	PG_END_TRY();
}
